[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string] $ArchivePath,
    [Parameter(Mandatory=$true)] [string] $SelectionPath,
    [string] $ProjectPath = 'E:\Projects\LGAEDProjects\02_construccion_depuracion_y_practica_profesional\AssetCheck',
    [switch] $SyntheticFixture,
    [string] $SyntheticExpectedSha256 = '',
    [int] $SyntheticExpectedFiles = 0
)

# READ-ONLY. Compares an EXTERNALLY REVIEWED candidate list against every
# authenticated ZIP entry and the pre-existing project. It never authorizes
# -Aplicar, extracts sources, or infers approval from a filename/count.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$expectedInstallable = 31
$expectedExcluded = 4
if (-not [IO.Path]::IsPathFullyQualified($SelectionPath) -or
    -not (Test-Path -LiteralPath $SelectionPath -PathType Leaf)) {
    throw 'Provide the full path of a readable, separately reviewed 31-path list.'
}

$inspector = Join-Path $PSScriptRoot 'Inspect-AssetCheckArchive.ps1'
$params = @{
    ArchivePath = $ArchivePath
    ProjectPath = $ProjectPath
}
if ($SyntheticFixture) {
    $params.SyntheticFixture = $true
    $params.SyntheticExpectedSha256 = $SyntheticExpectedSha256
    $params.SyntheticExpectedFiles = $SyntheticExpectedFiles
}
elseif ($SyntheticExpectedSha256 -or $SyntheticExpectedFiles) {
    throw 'Production cannot override the pinned release identity.'
}
$inventoryJson = & $inspector @params | Out-String
$inventory = $inventoryJson | ConvertFrom-Json
if ($inventory.archiveFiles -ne ($expectedInstallable + $expectedExcluded) -or
    $inventory.fullyDecodedAndCrcVerified -ne $inventory.archiveFiles -or
    @($inventory.memberDigests).Count -ne $inventory.archiveFiles) {
    throw 'A complete, CRC-verified 35-file ZIP inventory is required.'
}
if ($inventory.installableSelectionVerified -or
    -not $inventory.inventoryPathsAreNotInstallInstructions -or
    $inventory.installationPerformed) {
    throw 'The inspection output improperly claims installation authorization.'
}

# Exact UTF-8 lines. Do not case-fold or trim paths: a spelling change must
# be brought back for review, not silently accepted as equivalent.
$raw = [IO.File]::ReadAllText([IO.Path]::GetFullPath($SelectionPath),
    [Text.UTF8Encoding]::new($false))
$rows = [regex]::Split($raw, '\r?\n')
if ($rows.Count -gt 0 -and $rows[$rows.Count - 1] -eq '') {
    $rows = @($rows | Select-Object -SkipLast 1)
}
if ($rows.Count -ne $expectedInstallable) {
    throw "Expected exactly $expectedInstallable candidate paths, found $($rows.Count)."
}
$archiveNames = [Collections.Generic.Dictionary[string,string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
foreach ($entry in $inventory.memberDigests) {
    if (-not $archiveNames.TryAdd([string]$entry.path, [string]$entry.path)) {
        throw 'The inventory contains case-insensitive duplicate ZIP members.'
    }
}
$selected = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
foreach ($name in $rows) {
    if ([string]::IsNullOrWhiteSpace($name) -or
        $name -cne $name.Trim() -or
        $name.Contains('\') -or $name.StartsWith('/') -or
        $name -match '[\x00-\x1f]') {
        throw "Noncanonical candidate path: '$name'"
    }
    if (-not $selected.Add($name)) {
        throw "Repeated candidate path: '$name'"
    }
    $canonical = ''
    if (-not $archiveNames.TryGetValue($name, [ref]$canonical)) {
        throw "Candidate path does not exist in the pinned ZIP inventory: '$name'"
    }
    if ($canonical -cne $name) {
        throw "Candidate spelling differs from ZIP case: '$name'"
    }
}
$excluded = @(
    foreach ($entry in $inventory.memberDigests) {
        if (-not $selected.Contains([string]$entry.path)) { [string]$entry.path }
    }
)
if ($excluded.Count -ne $expectedExcluded) {
    throw "Expected precisely four auxiliary exclusions, found $($excluded.Count)."
}
$selectionDigest = (Get-FileHash -LiteralPath $SelectionPath -Algorithm SHA256).Hash.ToLowerInvariant()
$result = [ordered]@{
    scope = 'Read-only 31-candidate inventory comparison; NOT authorization to install'
    archiveSha256 = $inventory.archiveSha256
    authenticatedRelease = $inventory.authenticatedReference
    selectionListSha256 = $selectionDigest
    candidatesMatchedExactly = $selected.Count
    auxiliaryExclusionsToReview = $excluded
    auxiliaryExclusionsCount = $excluded.Count
    selectedPaths = @($rows)
    projectGitState = $inventory.gitState
    localConflicts = $inventory.conflicts
    localReparsePaths = $inventory.unsafeExistingReparsePaths
    projectRequiresManualReview = $inventory.requiresManualReview
    approvedByAuthor = $false
    installerApplyAuthorized = $false
    bytesWrittenToProject = 0
    referenceProjectInstalled = $false
}
$result | ConvertTo-Json -Depth 8
