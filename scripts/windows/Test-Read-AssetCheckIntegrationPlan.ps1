[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Use synthetic integration-plan fixtures only on hosted Windows.'
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$root = Join-Path $env:RUNNER_TEMP 'assetcheck-inert-plan-test'
$project = Join-Path $root 'project'
if (Test-Path -LiteralPath $root) { throw 'Synthetic fixture root already exists.' }
New-Item -ItemType Directory -Path $project -Force | Out-Null
$inspector = Join-Path $PSScriptRoot 'Read-AssetCheckIntegrationPlan.ps1'
$utf8 = [Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.IO.Compression

function New-Fixture([string] $name, [string] $script, [bool] $withIntegrator) {
    $path = Join-Path $root $name
    $file = [IO.File]::Create($path)
    try {
        $zip = [IO.Compression.ZipArchive]::new($file, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            $entries = @(
                @{Name='docs/a.txt';Text='documentation'},
                @{Name='src/b.cpp';Text='int main() { return 0; }'}
            )
            $extraCount = if ($withIntegrator) { 32 } else { 33 }
            for ($i = 0; $i -lt $extraCount; ++$i) {
                $entries += @{Name=('tests/fixture_{0:d2}.txt' -f $i);Text=('case {0}' -f $i)}
            }
            if ($withIntegrator) {
                $entries += @{Name='scripts/Integrar-AssetCheck.ps1';Text=$script}
            }
            if ($entries.Count -ne 35) { throw 'Synthetic 35-entry count mismatch.' }
            foreach ($member in $entries) {
                $entry = $zip.CreateEntry($member.Name)
                $writer = [IO.StreamWriter]::new($entry.Open(), $utf8)
                try { $writer.Write($member.Text) }
                finally { $writer.Dispose() }
            }
        }
        finally { $zip.Dispose() }
    }
    finally { $file.Dispose() }
    return $path
}

function Inspect-Fixture([string] $path) {
    $sha = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $args = @{
        ArchivePath = $path
        ProjectPath = $project
        SyntheticFixture = $true
        SyntheticExpectedSha256 = $sha
        SyntheticExpectedFiles = 35
    }
    return (& $inspector @args | Out-String) | ConvertFrom-Json
}

# An executable throw statement is intentionally embedded as proof that
# ParseInput only reads code without running it.
$script = @'
param([switch]$Aplicar, [string]$Raiz)
$paths = @('docs/a.txt', 'src/b.cpp')
throw 'THIS_MUST_NOT_EXECUTE'
'@
$zip = New-Fixture 'valid-integrator.zip' $script $true
$before = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
$valid = Inspect-Fixture $zip
$after = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if (-not $valid.embeddedScriptFound -or
    $valid.embeddedScriptPath -cne 'scripts/Integrar-AssetCheck.ps1' -or
    $valid.embeddedScriptParseErrors -ne 0 -or
    $valid.archiveFilesVerified -ne 35 -or
    @($valid.parameterNames).Count -ne 2 -or
    $valid.parameterNames -cnotcontains 'Aplicar' -or
    $valid.parameterNames -cnotcontains 'Raiz' -or
    @($valid.literalInventoryPathMentions).Count -ne 2 -or
    $valid.literalInventoryPathMentions -cnotcontains 'docs/a.txt' -or
    $valid.literalInventoryPathMentions -cnotcontains 'src/b.cpp' -or
    -not $valid.literalMentionsAreNotAnAllowlist -or
    $valid.actual31PathSelectionKnown -or
    $valid.excludedFourPathsVerified -or
    $valid.installationAuthorized -or
    $valid.bytesWrittenToStudentProject -ne 0 -or
    $before -cne $after) {
    throw ('Unexpected valid AST report: ' + ($valid | ConvertTo-Json -Depth 8))
}
Write-Host 'PASS: embedded script parsed without executing its commands.'

$missingZip = New-Fixture 'no-integrator.zip' '' $false
$missing = Inspect-Fixture $missingZip
if ($missing.embeddedScriptFound -or
    @($missing.parameterNames).Count -ne 0 -or
    @($missing.literalInventoryPathMentions).Count -ne 0 -or
    $missing.actual31PathSelectionKnown -or $missing.installationAuthorized) {
    throw 'Missing integrator was not reported correctly.'
}
Write-Host 'PASS: ZIP without integration script remains unapproved.'

$invalidZip = New-Fixture 'broken-integrator.zip' 'param([switch]$Aplicar' $true
$invalidRejected = $false
try { $null = Inspect-Fixture $invalidZip }
catch {
    if ($_.Exception.Message -notmatch 'syntax errors') {
        throw "Unexpected syntax diagnostic: $_"
    }
    $invalidRejected = $true
}
if (-not $invalidRejected) { throw 'Malformed PowerShell was accepted.' }
Write-Host 'PASS: malformed integration script rejected without execution.'

$pinnedRejected = $false
try { $null = & $inspector -ArchivePath $zip -ProjectPath $project }
catch {
    if ($_.Exception.Message -notmatch 'SHA-256 mismatch') {
        throw "Unexpected pinned release error: $_"
    }
    $pinnedRejected = $true
}
if (-not $pinnedRejected) { throw 'Synthetic ZIP was accepted as release.' }
Write-Host 'PASS: production release SHA cannot be replaced by fixture data.'

if (@(Get-ChildItem -LiteralPath $project -Force).Count -ne 0) {
    throw 'The read-only inspection wrote into the project folder.'
}
$results = [ordered]@{
    scope = 'Synthetic fixtures only, not authenticated AssetCheck ZIP'
    scriptParsedWithoutExecution = $true
    missingScriptReported = $true
    malformedScriptRejected = $true
    productionHashPinned = $true
    studentProjectFilesWritten = 0
    actualSelectionOf31FilesKnown = $false
}
$dir = Join-Path $repo 'out\qa\assetcheck-package-preflight'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $dir 'synthetic-integrator-review.json') -Encoding utf8
Write-Host 'PASS: four read-only integration-plan checks completed.'
exit 0
