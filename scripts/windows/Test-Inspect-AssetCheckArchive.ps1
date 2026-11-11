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

function Invoke-TestInspector([string] $zip, [int] $count, [string] $sha,
    [string] $scanProject = $project) {
    $options = @{
        ArchivePath = $zip
        ProjectPath = $scanProject
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
    $report.fullyDecodedAndCrcVerified -ne 35 -or
    @($report.memberDigests).Count -ne 35 -or
    $report.installableSelectionVerified -or
    -not $report.inventoryPathsAreNotInstallInstructions -or
    $null -ne $report.expectedDeployableFiles -or
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

# Corrupt the CRC-32 in the central directory for a file that does not
# exist in the project. A preflight that reads only colliding files will
# miss this case. The deliberately altered ZIP gets its own synthetic SHA.
$oneNew = New-TestArchive 'synthetic-new-entry.zip' @(
    @{Name='new-member.txt'; Text='member content with CRC'}
)
$rawCrc = [IO.File]::ReadAllBytes($oneNew)
$centralOffset = -1
for ($i=0; $i -lt $rawCrc.Length-46; ++$i) {
    if ($rawCrc[$i] -eq 0x50 -and $rawCrc[$i+1] -eq 0x4b -and
        $rawCrc[$i+2] -eq 0x01 -and $rawCrc[$i+3] -eq 0x02) {
        $centralOffset = $i
        break
    }
}
if ($centralOffset -lt 0) { throw 'Test ZIP has no central directory signature.' }
$rawCrc[$centralOffset + 16] = [byte]($rawCrc[$centralOffset + 16] -bxor 0x40)
$badCrcZip = Join-Path $root 'synthetic-wrong-member-crc.zip'
[IO.File]::WriteAllBytes($badCrcZip, $rawCrc)
$badCrcHash = (Get-FileHash -LiteralPath $badCrcZip -Algorithm SHA256).Hash.ToLowerInvariant()
Assert-Rejected 'CRC-32 mismatch for absent target' $badCrcZip 1 $badCrcHash 'CRC-32 mismatch'
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

# Test a directory entry against an existing regular file. A ZIP directory
# entry has no installation content, but may still be unsafe to materialize.
$dirZip = New-TestArchive 'directory-meets-file.zip' @(
    @{Name='occupied/'; Text=''},
    @{Name='safe.txt'; Text='still safe'}
)
$dirSha = (Get-FileHash -LiteralPath $dirZip -Algorithm SHA256).Hash.ToLowerInvariant()
$dirReport = Invoke-TestInspector $dirZip 1 $dirSha
if ($dirReport.conflicts -ne 1 -or $dirReport.newFiles -ne 1 -or
    -not $dirReport.requiresManualReview) {
    throw 'An explicit ZIP directory covering a local file was not detected.'
}
Write-Host 'PASS: explicit ZIP directory colliding with a local file is flagged.'

# A junction is a reparse point even if it points at a valid external folder.
# The read-only inspector must never treat descendants of that junction as
# new files eligible for installation.
$outside = Join-Path $root 'outside-target'
New-Item -ItemType Directory -Path $outside | Out-Null
$junction = Join-Path $project 'linked'
New-Item -ItemType Junction -Path $junction -Target $outside | Out-Null
$linkFileZip = New-TestArchive 'junction-child.zip' @(
    @{Name='linked/new.txt';Text='external write must never happen'}
)
$linkFileSha = (Get-FileHash -LiteralPath $linkFileZip -Algorithm SHA256).Hash.ToLowerInvariant()
$linkFileReport = Invoke-TestInspector $linkFileZip 1 $linkFileSha
if ($linkFileReport.unsafeExistingReparsePaths -ne 1 -or
    $linkFileReport.newFiles -ne 0 -or
    -not $linkFileReport.requiresManualReview) {
    throw 'Junction descendant was not marked as a blocked target.'
}
$linkDirectoryZip = New-TestArchive 'junction-directory.zip' @(
    @{Name='linked/';Text=''},
    @{Name='safe.txt';Text='safe path'}
)
$linkDirectorySha = (Get-FileHash -LiteralPath $linkDirectoryZip -Algorithm SHA256).Hash.ToLowerInvariant()
$linkDirectoryReport = Invoke-TestInspector $linkDirectoryZip 1 $linkDirectorySha
if ($linkDirectoryReport.unsafeExistingReparsePaths -ne 1 -or
    $linkDirectoryReport.newFiles -ne 1 -or
    -not $linkDirectoryReport.requiresManualReview) {
    throw 'Explicit ZIP directory on a junction was not blocked.'
}
if (@(Get-ChildItem -LiteralPath $outside -Force).Count -ne 0) {
    throw 'The inspector unexpectedly wrote content beyond the fixture project.'
}
Write-Host 'PASS: junction descendants and explicit ZIP directories are blocked.'

# This project root is an ordinary directory; its PARENT is a junction.
# Blocking only the final project directory is insufficient.
$realParent = Join-Path $root 'actual-parent-directory'
$ordinaryProject = Join-Path $realParent 'nested'
New-Item -ItemType Directory -Path $ordinaryProject -Force | Out-Null
$linkedParent = Join-Path $root 'linked-parent-directory'
New-Item -ItemType Junction -Path $linkedParent -Target $realParent | Out-Null
$projectViaJunction = Join-Path $linkedParent 'nested'
$ancestorRejected = $false
try {
    $null = Invoke-TestInspector $goodZip 35 $goodSha $projectViaJunction
}
catch {
    if ($_.Exception.Message -notmatch 'project path traverses a reparse point') {
        throw "Unexpected ancestral junction diagnostic: $_"
    }
    $ancestorRejected = $true
}
if (-not $ancestorRejected) {
    throw 'The project with a junction ancestor was not blocked.'
}
if (@(Get-ChildItem -LiteralPath $ordinaryProject -Force).Count -ne 0) {
    throw 'The inspector unexpectedly modified the project behind an ancestor junction.'
}
Write-Host 'PASS: project behind a junction parent is rejected before file inventory.'

# Test clean and dirty Git states against a second disposable project. Only
# this throwaway fixture gets an initial Git commit, never the user's repo.
$gitCommand = Get-Command git -CommandType Application | Select-Object -First 1
if (-not $gitCommand) { throw 'Git is required for Git-state safety tests.' }
$gitProject = Join-Path $root 'git-project'
New-Item -ItemType Directory -Path $gitProject | Out-Null
& $gitCommand.Source init --quiet $gitProject
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize synthetic Git fixture.' }
$tracked = Join-Path $gitProject 'tracked.txt'
[IO.File]::WriteAllText($tracked, 'reference', $utf8)
& $gitCommand.Source -C $gitProject add -- tracked.txt
if ($LASTEXITCODE -ne 0) { throw 'Git could not add the synthetic fixture.' }
& $gitCommand.Source -C $gitProject -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet -m 'Synthetic CI fixture'
if ($LASTEXITCODE -ne 0) { throw 'Git could not commit the synthetic fixture.' }
$gitZip = New-TestArchive 'git-state.zip' @(
    @{Name='tracked.txt';Text='reference'}
)
$gitSha = (Get-FileHash -LiteralPath $gitZip -Algorithm SHA256).Hash.ToLowerInvariant()
$gitIndex = Join-Path $gitProject '.git\index'
$indexBefore = (Get-FileHash -LiteralPath $gitIndex -Algorithm SHA256).Hash
$cleanReport = Invoke-TestInspector $gitZip 1 $gitSha $gitProject
$indexAfter = (Get-FileHash -LiteralPath $gitIndex -Algorithm SHA256).Hash
if ($cleanReport.gitState -cne 'clean' -or $cleanReport.requiresManualReview -or
    $cleanReport.existingIdentical -ne 1 -or $indexBefore -cne $indexAfter) {
    throw 'Clean synthetic Git tree was misreported or index changed.'
}
[IO.File]::WriteAllText((Join-Path $gitProject 'untracked.txt'), 'local data', $utf8)
$untrackedReport = Invoke-TestInspector $gitZip 1 $gitSha $gitProject
if ($untrackedReport.gitState -notlike 'dirty or untracked*' -or
    -not $untrackedReport.requiresManualReview) {
    throw 'Untracked Git file was not classified as requiring review.'
}
[IO.File]::WriteAllText($tracked, 'changed source', $utf8)
$modifiedReport = Invoke-TestInspector $gitZip 1 $gitSha $gitProject
if ($modifiedReport.gitState -notlike 'dirty or untracked*' -or
    $modifiedReport.conflicts -ne 1 -or
    -not $modifiedReport.requiresManualReview) {
    throw 'Modified Git source was not marked as a conflict.'
}
Write-Host 'PASS: clean, untracked and modified Git states detected without Git index changes.'

$summary = [ordered]@{
    scope = 'synthetic fixture tests only; never the authenticated AssetCheck ZIP'
    runner = $env:RUNNER_OS
    expectedReleaseSha256Unchanged = 'b8e6c9abc2d611bfad391facf36f3da0e71993322c18087991ad9f314c51a589'
    positiveMatches = 1
    localConflictsDetected = 1
    malformedArchivesRejected = 16
    absentEntryCrcTamperingRejected = 1
    membersFullyVerifiedInPositiveCase = 35
    parentFileConflictsDetected = 1
    explicitDirectoryConflictsDetected = 1
    junctionTypesBlocked = 2
    gitStatesVerified = 3
    ancestorJunctionsRejected = 1
    syntheticInventoryNeverAuthorizesDeployment = $true
    originalStudentFilesTouched = $false
    realArchiveInstalled = $false
}
$qa = Join-Path $repo 'out\qa\assetcheck-package-preflight'
New-Item -ItemType Directory -Path $qa -Force | Out-Null
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $qa 'synthetic-results.json') -Encoding utf8
Write-Host 'PASS: 10 non-destructive inspections, 16 malformed ZIPs rejected, and no deployment assumed from the inventory.'
# Git's expected non-worktree exit status remains in LASTEXITCODE even after
# all PowerShell assertions pass. Finish the synthetic CI job explicitly.
exit 0
