# Restore only pinned, verified local Kokoro resources. No downloads or app resets.
[CmdletBinding()]
param(
    [string]$Adb = 'C:\Users\GameDev\AppData\Local\Android\Sdk\platform-tools\adb.exe',
    [ValidatePattern('^[A-Za-z0-9_.:-]+$')][string]$Serial = 'ARGK6R3417001970',
    [switch]$VerifyLocalOnly
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $repository 'app\src\main\java\com\learngraphicsprogramming\app\narration'
$package = 'com.learngraphicsprogramming.app'
$artifacts = [ordered]@{}

function Add-Artifact([string]$Name, [long]$Bytes, [string]$Sha256, [string]$Directory) {
    if ($Name -notmatch '^[A-Za-z0-9_][A-Za-z0-9_.-]*$' -or $Name.Contains('..')) {
        throw "Unsafe manifest filename: $Name"
    }
    if ($artifacts.Contains($Name) -or $Bytes -le 0 -or $Sha256 -notmatch '^[0-9a-f]{64}$') {
        throw "Duplicate or invalid artifact metadata: $Name"
    }
    $artifacts[$Name] = [pscustomobject]@{
        Name=$Name; Bytes=$Bytes; Sha256=$Sha256; LocalPath=(Join-Path $Directory $Name)
    }
}

# Read the same metadata as Android, rather than maintain a second digest list.
# Fail if the literal syntax changes, so a partial parser cannot restore a subset.
$manifest = Get-Content -LiteralPath (Join-Path $source 'KokoroModelManifest.kt') -Raw
$revisionMatch = [regex]::Match($manifest, 'const val revision = "([0-9a-f]{40})"')
if (-not $revisionMatch.Success) { throw 'Pinned Kokoro revision not found.' }
$revision = $revisionMatch.Groups[1].Value
$downloads = Join-Path $repository "build\kokoro\downloads\$revision"
$english = Join-Path $repository 'build\kokoro\english-install'
$artifactPattern = 'Artifact\("([^"\r\n]+)",\s*"[^"\r\n]*",\s*([\d_]+)L,\s*"([0-9a-f]{64})"'
foreach ($entry in @(
    [pscustomobject]@{ Text=$manifest; Directory=$downloads },
    [pscustomobject]@{ Text=(Get-Content -LiteralPath (Join-Path $source 'EnglishAuxResources.kt') -Raw); Directory=$english }
)) {
    $parsedArtifacts = [regex]::Matches($entry.Text, $artifactPattern)
    if ($parsedArtifacts.Count -eq 0 -or $parsedArtifacts.Count -ne [regex]::Matches($entry.Text, 'Artifact\("').Count) {
        throw 'Resource manifest syntax changed; inspect the restore parser before continuing.'
    }
    foreach ($match in $parsedArtifacts) {
        Add-Artifact $match.Groups[1].Value ([long]$match.Groups[2].Value.Replace('_','')) $match.Groups[3].Value $entry.Directory
    }
}
$catalog = Get-Content -LiteralPath (Join-Path $source 'KokoroVoiceCatalog.kt') -Raw
$voiceSize = [regex]::Match($catalog, '"\$id\.bin",\s*"voices/\$id\.bin",\s*([\d_]+)L')
$voices = [regex]::Matches($catalog, 'KokoroVoice\("([^"\r\n]+)",\s*[^,]+,\s*"([0-9a-f]{64})"')
if (-not $voiceSize.Success -or $voices.Count -eq 0 -or $voices.Count -ne [regex]::Matches($catalog, 'KokoroVoice\("').Count) {
    throw 'Voice manifest syntax changed; inspect the restore parser before continuing.'
}
foreach ($voice in $voices) {
    Add-Artifact ($voice.Groups[1].Value + '.bin') ([long]$voiceSize.Groups[1].Value.Replace('_','')) $voice.Groups[2].Value $downloads
}
$misaki = Get-Content -LiteralPath (Join-Path $source 'phonemes\EnglishMisakiG2P.kt') -Raw
$dictionaries = [regex]::Matches($misaki, '"([^"\r\n]+)" to Dictionary\(([\d_]+)L,\s*"([0-9a-f]{64})"')
if ($dictionaries.Count -eq 0 -or $dictionaries.Count -ne [regex]::Matches($misaki, '"[^"\r\n]+" to Dictionary\(').Count) {
    throw 'Dictionary manifest syntax changed; inspect the restore parser before continuing.'
}
foreach ($dictionary in $dictionaries) {
    Add-Artifact $dictionary.Groups[1].Value ([long]$dictionary.Groups[2].Value.Replace('_','')) $dictionary.Groups[3].Value $downloads
}

function Test-LocalArtifact($Artifact) {
    if (-not (Test-Path -LiteralPath $Artifact.LocalPath -PathType Leaf)) {
        throw "Missing local resource; no download attempted: $($Artifact.LocalPath)"
    }
    $file = Get-Item -LiteralPath $Artifact.LocalPath
    if ($file.Length -ne $Artifact.Bytes -or
        (Get-FileHash -LiteralPath $Artifact.LocalPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Artifact.Sha256) {
        throw "Local resource failed size/SHA256 verification: $($Artifact.LocalPath)"
    }
}

# Verify ALL local resources before any Android command or write.
foreach ($artifact in $artifacts.Values) { Test-LocalArtifact $artifact }
$totalBytes = ($artifacts.Values | Measure-Object Bytes -Sum).Sum
Write-Host "Verified $($artifacts.Count) local Kokoro resources ($totalBytes bytes), revision $revision."
if ($VerifyLocalOnly) { return }

$adbPath = (Resolve-Path -LiteralPath $Adb).Path
function Invoke-Adb([string[]]$Arguments) {
    $result = & $adbPath -s $Serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ADB failed ($LASTEXITCODE): $($Arguments -join ' '): $($result -join ' ')" }
    return $result
}
function Read-RemoteArtifact([string]$Path) {
    # Paths contain only fixed package/revision and validated manifest filenames.
    $command = "if [ -L '$Path' ]; then echo INVALID; elif [ -e '$Path' ]; then if [ -f '$Path' ]; then stat -c %s '$Path' && sha256sum '$Path'; else echo INVALID; fi; else echo MISSING; fi"
    $lines = @((Invoke-Adb @('shell', $command)) | ForEach-Object { "$($_)".Trim() })
    if ($lines.Count -eq 1 -and $lines[0] -eq 'MISSING') { return $null }
    $digestMatch = if ($lines.Count -eq 2) { [regex]::Match($lines[1], '^([0-9a-f]{64})\s') } else { $null }
    if ($lines.Count -ne 2 -or $lines[0] -notmatch '^\d+$' -or $null -eq $digestMatch -or -not $digestMatch.Success) {
        throw "Cannot verify existing remote path; preserved without modification: $Path ($($lines -join ' '))"
    }
    return [pscustomobject]@{ Bytes=[long]$lines[0]; Sha256=$digestMatch.Groups[1].Value }
}
function Assert-RemoteArtifact([string]$Path, $Artifact) {
    $remote = Read-RemoteArtifact $Path
    if ($null -eq $remote -or $remote.Bytes -ne $Artifact.Bytes -or $remote.Sha256 -ne $Artifact.Sha256) {
        throw "Remote resource failed size/SHA256 verification; retained for inspection: $Path"
    }
}

Invoke-Adb @('get-state') | Out-Null
Invoke-Adb @('shell', 'pm', 'path', $package) | Out-Null
$identityBefore = (Invoke-Adb @('shell', 'run-as', $package, 'id') -join ' ').Trim()
$destination = "/sdcard/Android/data/$package/files/kokoro/$revision"
$missing = @()
$skipped = 0
# Inspect ALL existing destinations before creating a staging directory or copying.
foreach ($artifact in $artifacts.Values) {
    $path = "$destination/$($artifact.Name)"
    $remote = Read-RemoteArtifact $path
    if ($null -eq $remote) { $missing += $artifact }
    elseif ($remote.Bytes -eq $artifact.Bytes -and $remote.Sha256 -eq $artifact.Sha256) { $skipped++ }
    else { throw "Existing resource differs from pinned metadata; preserved without overwriting: $path" }
}
if ($missing.Count -gt 0) {
    $staging = "/sdcard/Android/data/$package/files/kokoro/.restore-$([guid]::NewGuid().ToString('N'))"
    Invoke-Adb @('shell', "mkdir -p '$destination' && mkdir '$staging'") | Out-Null
    Write-Host "Staging directory: $staging (retained, including partial transfers, for inspection)."
    foreach ($artifact in $missing) {
        # Catch a local change since the initial inventory before copying it.
        Test-LocalArtifact $artifact
        $stagedPath = "$staging/$($artifact.Name)"
        Invoke-Adb @('push', $artifact.LocalPath, $stagedPath) | Out-Null
        Assert-RemoteArtifact $stagedPath $artifact
    }
    foreach ($artifact in $missing) {
        $path = "$destination/$($artifact.Name)"
        $remote = Read-RemoteArtifact $path
        if ($null -ne $remote) {
            Assert-RemoteArtifact $path $artifact
            continue
        }
        $stagedPath = "$staging/$($artifact.Name)"
        # Shell noclobber opens the destination exclusively: an existing file,
        # including one appearing concurrently, is never truncated or replaced.
        # Interrupted/new partial files stay available for diagnosis; reruns refuse
        # to overwrite them. Do not remove them automatically to retry a restore.
        Invoke-Adb @('shell', "(set -C; cat '$stagedPath' > '$path')") | Out-Null
        Assert-RemoteArtifact $path $artifact
        Write-Host "Restored and verified: $($artifact.Name)"
    }
}
foreach ($artifact in $artifacts.Values) { Assert-RemoteArtifact "$destination/$($artifact.Name)" $artifact }
$identityAfter = (Invoke-Adb @('shell', 'run-as', $package, 'id') -join ' ').Trim()
if ($identityAfter -ne $identityBefore) { throw 'Package identity changed during restore; inspect the device before continuing.' }
Write-Host "Kokoro restore verified: $($artifacts.Count) resources, $skipped already present. Preferences and recordings were not changed."
