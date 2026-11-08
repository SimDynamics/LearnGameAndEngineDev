[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Prepare narration CI tools only on a disposable GitHub-hosted Windows runner.'
}

$qaSource = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaVersions = Get-Content -LiteralPath (Join-Path $qaSource 'toolchains\windows.versions.json') -Raw |
    ConvertFrom-Json
if ($qaVersions.java.major -ne 27 -or $qaVersions.java.minVersion -ne '27.0.1.0.3' -or
    $qaVersions.ffmpeg.version -ne '9.0.2') {
    throw 'Reverify the official narration tool archives when the tools manifest changes.'
}

# Sources: Oracle's official oracle-actions/setup-java release table and the
# Gyan Windows build linked by ffmpeg.org. Resolve-NativeTools.ps1 separately
# checks the actual extracted executables, JDK release, JNI headers and paths.
$qaJavaUrl = 'https://download.java.net/java/GA/jdk27.0.1.0.3/b14170c9b2de463d9b89442c6db3e384/1/GPL/openjdk-27.0.1.0.3_windows-x64_bin.zip'
$qaFFmpegUrl = 'https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.2-essentials_build.zip'
$qaFFmpegHash = '60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba'
$qaToolsRoot = Join-Path $env:RUNNER_TEMP "lgaed-narration-tools-$env:GITHUB_RUN_ID-$env:GITHUB_RUN_ATTEMPT"
if (Test-Path -LiteralPath $qaToolsRoot) {
    throw "The narration tool destination must be new: $qaToolsRoot"
}
New-Item -ItemType Directory -Path $qaToolsRoot | Out-Null

function Receive-VerifiedArchive([string] $Url, [string] $Hash, [string] $Name) {
    if ($Hash -notmatch '^[a-fA-F0-9]{64}$') { throw "Invalid official SHA-256 for $Name." }
    $archive = Join-Path $qaToolsRoot "$Name.zip"
    Invoke-WebRequest -Uri $Url -OutFile $archive
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actual -ne $Hash) { throw "The $Name archive does not match its official SHA-256." }
    Write-Host "Verified archive: $Name SHA256=$actual source=$Url"
    $destination = Join-Path $qaToolsRoot $Name
    Expand-Archive -LiteralPath $archive -DestinationPath $destination
    return $destination
}

# The checksum is read from the immutable OpenJDK release's own HTTPS sidecar;
# no unverified archive, mutable 'latest' URL or alternate mirror is accepted.
$qaJavaHashFile = Join-Path $qaToolsRoot 'openjdk.zip.sha256'
Invoke-WebRequest -Uri "$qaJavaUrl.sha256" -OutFile $qaJavaHashFile
$qaJavaHash = [System.IO.File]::ReadAllText($qaJavaHashFile).Trim()
$qaJavaExtract = Receive-VerifiedArchive $qaJavaUrl $qaJavaHash 'openjdk'
$qaFFmpegExtract = Receive-VerifiedArchive $qaFFmpegUrl $qaFFmpegHash 'ffmpeg'
$qaJdks = @(Get-ChildItem -LiteralPath $qaJavaExtract -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'bin\java.exe') -PathType Leaf
})
$qaFFmpegRoots = @(Get-ChildItem -LiteralPath $qaFFmpegExtract -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'bin\ffmpeg.exe') -PathType Leaf
})
if ($qaJdks.Count -ne 1 -or $qaFFmpegRoots.Count -ne 1) {
    throw 'The verified narration archives do not contain exactly one expected tool installation.'
}

[PSCustomObject]@{
    JavaHome = $qaJdks[0].FullName
    FFmpeg = Join-Path $qaFFmpegRoots[0].FullName 'bin\ffmpeg.exe'
    JavaArchiveSHA256 = $qaJavaHash.ToLowerInvariant()
    FFmpegArchiveSHA256 = $qaFFmpegHash
}
