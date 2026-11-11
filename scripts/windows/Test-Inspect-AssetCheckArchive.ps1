[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This is a synthetic fixture test for disposable GitHub-hosted Windows.'
}

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$inspector = Join-Path $PSScriptRoot 'Inspect-AssetCheckArchive.ps1'
$root = Join-Path $env:RUNNER_TEMP 'assetcheck-package-preflight-fixture'
$project = Join-Path $root 'project'
if (Test-Path -LiteralPath $root) { throw 'Synthetic fixture directory already exists.' }
New-Item -ItemType Directory -Path $project -Force | Out-Null

Add-Type -AssemblyName System.IO.Compression
$utf8 = [Text.UTF8Encoding]::new($false)
function New-TestArchive([string] $fileName, [object[]] $members) {
    $path = Join-Path $root $fileName
    $file = [IO.File]::Create($path)
    try {
        $zip = [IO.Compression.ZipArchive]::new(
            $file, [IO.Compression.ZipArchiveMode]::Create, $true
        )
        try {
            foreach ($member in $members) {
                $entry = $zip.CreateEntry($member.Name)
                if ($member.ContainsKey('ExternalAttributes')) {
                    $entry.ExternalAttributes = $member.ExternalAttributes
                }
                $writer = [IO.StreamWriter]::new($entry.Open(), $utf8)
                try { $writer.Write([string]$member.Text) }
                finally { $writer.Dispose() }
            }
        }
        finally { $zip.Dispose() }
    }
    finally { $file.Dispose() }
    return $path
}

function Invoke-TestInspector([string] $zip, [int] $count, [string] $sha) {
    $options = @{
        ArchivePath = $zip
        ProjectPath = $project
        SyntheticFixture = $true
        SyntheticExpectedSha256 = $sha
        SyntheticExpectedFiles = $count
    }
    $outputText = & $inspector @options | Out-String
    return $outputText | ConvertFrom-Json
}

function Assert-Rejected([string] $description, [string] $zip,
    [int] $count, [string] $sha, [string] $pattern) {
    $rejected = $false
    try {
        $null = Invoke-TestInspector $zip $count $sha
    }
    catch {
        if ($_.Exception.Message -notmatch $pattern) {
            throw "Unexpected error for '$description': $_"
        }
        $rejected = $true
    }
    if (-not $rejected) { throw "Unsafe archive was accepted: $description" }
    Write-Host "REJECTED: $description"
}

$files = [Collections.Generic.List[object]]::new()
[void]$files.Add(@{Name='src/registro.cpp'; Text='unchanged reference'})
for ($i=1; $i -le 34; ++$i) {
    [void]$files.Add(@{
        Name=('tests/case_{0:d3}.txt' -f $i)
        Text=('case-{0}' -f $i)
    })
}
$goodZip = New-TestArchive 'synthetic-35.zip' $files.ToArray()
$goodSha = (Get-FileHash -LiteralPath $goodZip -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Path (Join-Path $project 'src') | Out-Null
[IO.File]::WriteAllText((Join-Path $project 'src\registro.cpp'), 'unchanged reference', $utf8)
$report = Invoke-TestInspector $goodZip 35 $goodSha
if ($report.archiveFiles -ne 35 -or $report.existingIdentical -ne 1 -or
    $report.newFiles -ne 34 -or $report.conflicts -ne 0 -or
    -not $report.noFilesWereWritten -or $report.installationPerformed) {
    throw "Unexpected valid-ZIP inspection: $($report | ConvertTo-Json -Compress)"
}
Write-Host 'PASS: 35-entry package; 1 exact match, 34 new, zero conflicts.'

[IO.File]::WriteAllText((Join-Path $project 'src\registro.cpp'), 'local custom work', $utf8)
$localBefore = (Get-FileHash -LiteralPath (Join-Path $project 'src\registro.cpp') -Algorithm SHA256).Hash
$conflicted = Invoke-TestInspector $goodZip 35 $goodSha
$localAfter = (Get-FileHash -LiteralPath (Join-Path $project 'src\registro.cpp') -Algorithm SHA256).Hash
if ($conflicted.conflicts -ne 1 -or $conflicted.existingIdentical -ne 0 -or
    $conflicted.newFiles -ne 34 -or -not $conflicted.requiresManualReview -or
    $localBefore -cne $localAfter) {
    throw 'Local conflict was not reported safely, or source bytes were changed.'
}
Write-Host 'PASS: conflicting local work is reported and preserved byte-for-byte.'

Assert-Rejected 'wrong release digest' $goodZip 35 ('0' * 64) 'SHA-256 mismatch'
Assert-Rejected 'file-count mismatch' $goodZip 34 $goodSha 'Expected 34 files'
$malicious = @(
    @{File='traversal.zip'; Members=@(@{Name='../escape.txt';Text='no'}); Error='Unsafe Windows ZIP component'},
    @{File='absolute.zip'; Members=@(@{Name='C:/Windows/win.ini';Text='no'}); Error='Unsafe Windows ZIP component'},
    @{File='backslash.zip'; Members=@(@{Name='src\hidden.txt';Text='no'}); Error='Unsafe or noncanonical ZIP'},
    @{File='reserved.zip'; Members=@(@{Name='CON.txt';Text='no'}); Error='Unsafe Windows ZIP component'},
    @{File='duplicate.zip'; Members=@(
        @{Name='A.txt';Text='1'},
        @{Name='a.TXT';Text='2'}
    ); Error='duplicate ZIP entry'},
    @{File='ads.zip'; Members=@(@{Name='src/a.txt:stream';Text='no'}); Error='Unsafe Windows ZIP component'},
    @{File='dotdot.zip'; Members=@(@{Name='src/../escape.txt';Text='no'}); Error='Unsafe Windows ZIP component'},
    @{File='git-metadata.zip'; Members=@(@{Name='.git/config';Text='no'}); Error='Git metadata'},
    @{File='cmake-cache.zip'; Members=@(@{Name='build/CMakeCache.txt';Text='no'}); Error='Git metadata'},
    @{File='symlink.zip'; Members=@(@{
        Name='external';Text='../../elsewhere';
        ExternalAttributes=[int](0xA1FF -shl 16)
    }); Error='symbolic link or special file'},
    @{File='file-before-child.zip'; Members=@(
        @{Name='nest';Text='file'},
        @{Name='nest/child.txt';Text='must not fit'}
    ); Error='ZIP file is also an ancestor directory'},
    @{File='child-before-file.zip'; Members=@(
        @{Name='nest/child.txt';Text='child'},
        @{Name='nest';Text='cannot become a file'}
    ); Error='ZIP file collides with a directory'},
    @{File='explicit-dir-collision.zip'; Members=@(
        @{Name='nest/';Text=''},
        @{Name='nest';Text='cannot become a file'}
    ); Error='duplicate ZIP entry|ZIP file collides with a directory'}
)
foreach ($bad in $malicious) {
    $zip = New-TestArchive $bad.File $bad.Members
    $digest = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Rejected $bad.File $zip $bad.Members.Count $digest $bad.Error
}
$unexpected = Get-ChildItem -LiteralPath $project -Recurse -File
if (@($unexpected).Count -ne 1) {
    throw 'The inspector wrote extra content to the pre-existing project.'
}

# A regular file at a parent directory position must never be mistaken for
# a missing destination. Inspecting it must not alter its existing bytes.
$occupied = Join-Path $project 'occupied'
[IO.File]::WriteAllText($occupied, 'preserve local parent file', $utf8)
$parentBefore = (Get-FileHash -LiteralPath $occupied -Algorithm SHA256).Hash
$parentZip = New-TestArchive 'existing-parent-file.zip' @(
    @{Name='occupied/child.txt';Text='not installable'}
)
$parentSha = (Get-FileHash -LiteralPath $parentZip -Algorithm SHA256).Hash.ToLowerInvariant()
$parentResult = Invoke-TestInspector $parentZip 1 $parentSha
$parentAfter = (Get-FileHash -LiteralPath $occupied -Algorithm SHA256).Hash
if ($parentResult.conflicts -ne 1 -or $parentResult.newFiles -ne 0 -or
    -not $parentResult.requiresManualReview -or
    $parentResult.installationPerformed -or $parentBefore -cne $parentAfter) {
    throw 'Existing parent file was not preserved or not flagged as a conflict.'
}
Write-Host 'PASS: existing parent file prevents a child path without overwriting.'

$summary = [ordered]@{
    scope = 'synthetic fixture tests only; never the authenticated AssetCheck ZIP'
    runner = $env:RUNNER_OS
    expectedReleaseSha256Unchanged = 'b8e6c9abc2d611bfad391facf36f3da0e71993322c18087991ad9f314c51a589'
    positiveMatches = 1
    localConflictsDetected = 1
    malformedArchivesRejected = 15
    parentFileConflictsDetected = 1
    originalStudentFilesTouched = $false
    realArchiveInstalled = $false
}
$qa = Join-Path $repo 'out\qa\assetcheck-package-preflight'
New-Item -ItemType Directory -Path $qa -Force | Out-Null
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $qa 'synthetic-results.json') -Encoding utf8
Write-Host 'PASS: 3 non-destructive inspections and 15 malicious/mismatched archives rejected.'
# Git's expected non-worktree exit status remains in LASTEXITCODE even after
# all PowerShell assertions pass. Finish the synthetic CI job explicitly.
exit 0
