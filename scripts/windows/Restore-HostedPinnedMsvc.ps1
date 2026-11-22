# Restore only the explicitly pinned MSVC component on an ephemeral GitHub runner.
# Never modify the user's PC and never substitute a different compiler version.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows' -or
    $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Pinned MSVC restoration is restricted to ephemeral GitHub-hosted Windows runners.'
}

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$manifest = Get-Content -LiteralPath (Join-Path $root 'toolchains\windows.versions.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$toolset = [string] $manifest.visualStudio.msvcToolset
$requiredVersion = [string] $manifest.visualStudio.compilerVersion
# VS2026 14.51 is the only component reviewed for this temporary image.
# A future manifest change must not guess another component ID.
if (-not $toolset.StartsWith('14.51.', [StringComparison]::Ordinal)) {
    throw "No approved hosted component for toolset $toolset."
}
$component = 'Microsoft.VisualStudio.Component.VC.14.51.x86.x64'
$installerDirectory = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer'
$vswhere = Join-Path $installerDirectory 'vswhere.exe'
$installer = Join-Path $installerDirectory 'setup.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf) -or
    -not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw 'Official Visual Studio locator or installer is not available on the hosted image.'
}
$instances = @(& $vswhere -products '*' -version $manifest.visualStudio.versionRange -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1 -or [string]::IsNullOrWhiteSpace($instances[0])) {
    throw 'Expected one hosted Visual Studio 2026 instance with C++ tools.'
}
$vsRoot = [string] $instances[0]
$compiler = Join-Path $vsRoot ("VC\Tools\MSVC\" + $toolset + "\bin\Hostx64\x64\cl.exe")

function Test-PinnedCompiler {
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { return $false }
    $info = (Get-Item -LiteralPath $compiler).VersionInfo
    $actual = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart
    Write-Host "MSVC restoration: found cl.exe version $actual; required $requiredVersion"
    return $actual -eq $requiredVersion
}
if (Test-PinnedCompiler) {
    Write-Host 'MSVC restoration: already pinned; no installer invoked.'
    return
}
Write-Host "MSVC restoration: requested $toolset is absent on the hosted image."
Write-Host "MSVC restoration: adding ONLY $component to $vsRoot."
# setup.exe supports modify, --installPath, --add, --quiet and --norestart.
# It does not accept --wait; Start-Process supplies that synchronization.
$arguments = 'modify --installPath "' + $vsRoot + '" --add ' + $component + ' --quiet --norestart'
$process = Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru -NoNewWindow
if ($process.ExitCode -ne 0) {
    throw "Visual Studio returned $($process.ExitCode); pinned Windows tests remain blocked."
}
if (-not (Test-PinnedCompiler)) {
    throw "Installer did not provide exact $toolset / cl.exe $requiredVersion; no fallback is allowed."
}
Write-Host "MSVC restoration: exact $toolset with cl.exe $requiredVersion verified on the ephemeral runner."
