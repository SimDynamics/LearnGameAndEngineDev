[CmdletBinding()]
param(
    [ValidateSet('Application', 'NarrationHost')]
    [string] $Scope = 'Application',
    [string] $CMakePath,
    [string] $PythonPath,
    [string] $GitPath,
    [string] $JavaHome,
    [string] $FFmpegPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$manifestPath = Join-Path $sourceRoot 'toolchains\windows.versions.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Falta el manifiesto Windows: $manifestPath. Integra los cambios tecnicos de Codex antes de compilar esta copia."
}
$versions = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($versions.schemaVersion -ne 1) { throw 'Version del manifiesto Windows no compatible.' }

function Expand-ToolPath([string] $Path) {
    $expanded = [Environment]::ExpandEnvironmentVariables($Path)
    if ($expanded -match '%[^%]+%') { throw "No se pudo resolver la ruta de herramienta: $Path" }
    return [IO.Path]::GetFullPath($expanded)
}
function Require-ToolFile([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Falta la herramienta Windows requerida: $Path. Instala la version indicada en toolchains/windows.versions.json."
    }
}
function Read-ToolVersion([string] $Path, [string[]] $Arguments, [string] $Pattern, [string] $Expected) {
    $output = @(& $Path @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0 -or -not (($output -join "`n") -match $Pattern)) {
        throw "La herramienta $Path no corresponde a la version requerida $Expected."
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
Require-ToolFile $vswhere
$instances = @(& $vswhere -latest -version $versions.visualStudio.versionRange -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1 -or $instances[0].isPrerelease) {
    throw 'Se requiere Visual Studio 2026 estable con las herramientas C++ x64; no se selecciona Insiders.'
}
$vsRoot = [IO.Path]::GetFullPath([string]$instances[0].installationPath)
$msvcToolset = [string]$versions.visualStudio.msvcToolset
$windowsSdk = [string]$versions.visualStudio.windowsSdk
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
$cl = Join-Path $vsRoot "VC\Tools\MSVC\$msvcToolset\bin\Hostx64\x64\cl.exe"
$atl = Join-Path $vsRoot "VC\Tools\MSVC\$msvcToolset\atlmfc\include\atlbase.h"
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$sdkRoot = [string](Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction Stop).KitsRoot10
if ([string]::IsNullOrWhiteSpace($sdkRoot)) { throw "Falta Windows SDK $windowsSdk en la instalacion de Windows Kits." }
foreach ($relative in @("bin\$windowsSdk\x64\rc.exe", "bin\$windowsSdk\x64\mt.exe",
        "Include\$windowsSdk\um\Windows.h", "Include\$windowsSdk\ucrt\stdio.h",
        "Lib\$windowsSdk\um\x64\windowsapp.lib", "Lib\$windowsSdk\ucrt\x64\ucrt.lib")) {
    Require-ToolFile (Join-Path $sdkRoot $relative)
}
$minimumSdkServicing = [version]$versions.visualStudio.windowsSdkServicing
foreach ($sdkTool in @('rc.exe','mt.exe')) {
    $sdkFile = Join-Path $sdkRoot "bin\$windowsSdk\x64\$sdkTool"
    $fileVersion = (Get-Item -LiteralPath $sdkFile).VersionInfo.FileVersion
    if ($fileVersion -notmatch '^(\d+\.\d+\.\d+\.\d+)' -or
        [version]$Matches[1] -lt $minimumSdkServicing) {
        throw "Actualiza el Windows SDK a ${minimumSdkServicing}: $sdkTool tiene version '$fileVersion'. El nombre de carpeta $windowsSdk no demuestra el mantenimiento instalado."
    }
}
$cmake = Expand-ToolPath $(if ($CMakePath) { $CMakePath } else { $versions.cmake.path })
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$cmakeGui = Join-Path (Split-Path -Parent $cmake) 'cmake-gui.exe'
$python = Expand-ToolPath $(if ($PythonPath) { $PythonPath } else { $versions.python.path })
$git = Expand-ToolPath $(if ($GitPath) { $GitPath } else { $versions.git.path })
$ffmpeg = Expand-ToolPath $(if ($FFmpegPath) { $FFmpegPath } else { $versions.ffmpeg.path })
$vulkan = ''
$physicalTools = @($vswhere, $vcvars, $cl, $ninja, $cmake, $ctest, $cmakeGui, $python, $git, $ffmpeg,
    (Join-Path $sdkRoot "bin\$windowsSdk\x64\rc.exe"), (Join-Path $sdkRoot "bin\$windowsSdk\x64\mt.exe"))
if ($Scope -eq 'Application') {
    $vulkan = Expand-ToolPath $versions.vulkan.path
    $glslc = Join-Path $vulkan 'Bin\glslc.exe'
    $vulkanHeader = Join-Path $vulkan 'Include\vulkan\vulkan_core.h'
    $physicalTools += @($glslc, $vulkanHeader)
}
foreach ($file in $physicalTools) { Require-ToolFile $file }
& $python -I -S -B (Join-Path $PSScriptRoot 'verify_physical_tools.py') @physicalTools
if ($LASTEXITCODE -ne 0) { throw 'Las herramientas Windows deben estar en instalaciones fisicas compartidas, fuera de los datos privados del agente.' }
if (-not (Test-Path -LiteralPath $atl -PathType Leaf)) {
    throw 'La narracion Windows requiere C++ ATL para el toolset elegido. Instala Microsoft.VisualStudio.Component.VC.ATL en Visual Studio Installer.'
}
if ((Get-Item -LiteralPath $cl).VersionInfo.FileVersion -ne $versions.visualStudio.compilerVersion) {
    throw "El compilador MSVC no corresponde a $($versions.visualStudio.compilerVersion); revisa la instalacion estable y el manifiesto."
}
Read-ToolVersion $cmake @('--version') ('(?m)^cmake version ' + [regex]::Escape($versions.cmake.version) + '\r?$') $versions.cmake.version
Read-ToolVersion $ctest @('--version') ('(?m)^ctest version ' + [regex]::Escape($versions.cmake.version) + '\r?$') $versions.cmake.version
Read-ToolVersion $ninja @('--version') ('^' + [regex]::Escape($versions.visualStudio.ninjaVersion) + '$') $versions.visualStudio.ninjaVersion
Read-ToolVersion $python @('--version') ('^Python ' + [regex]::Escape($versions.python.version) + '$') $versions.python.version
Read-ToolVersion $git @('--version') ('^git version ' + [regex]::Escape($versions.git.version) + '$') $versions.git.version
Read-ToolVersion $ffmpeg @('-version') ('(?m)^ffmpeg version ' + [regex]::Escape($versions.ffmpeg.version) + '(?:-|\s)') $versions.ffmpeg.version
if ($Scope -eq 'Application') {
    $vulkanRevision = ([version]$versions.vulkan.version).Build
    if ((Get-Content -LiteralPath $vulkanHeader -Raw) -notmatch ('(?m)^#define VK_HEADER_VERSION ' + $vulkanRevision + '\r?$')) {
        throw "Las cabeceras Vulkan no corresponden al SDK $($versions.vulkan.version)."
    }
}

if ($JavaHome) {
    $jdkPattern = Expand-ToolPath $JavaHome
    if (-not (Test-Path -LiteralPath $jdkPattern -PathType Container)) {
        throw "Falta el directorio JDK solicitado: $jdkPattern"
    }
    $jdkDirectories = @(Get-Item -LiteralPath $jdkPattern)
} else {
    $jdkPattern = [Environment]::ExpandEnvironmentVariables([string]$versions.java.rootPattern)
    $jdkParent = Expand-ToolPath (Split-Path -Parent $jdkPattern)
    $jdkFilter = Split-Path -Leaf $jdkPattern
    $jdkDirectories = @(Get-ChildItem -LiteralPath $jdkParent -Filter $jdkFilter -Directory -ErrorAction SilentlyContinue)
}
function Compare-VersionParts([long[]] $Left, [long[]] $Right) {
    for ($index = 0; $index -lt [Math]::Max($Left.Count, $Right.Count); ++$index) {
        $leftPart = if ($index -lt $Left.Count) { $Left[$index] } else { 0 }
        $rightPart = if ($index -lt $Right.Count) { $Right[$index] } else { 0 }
        if ($leftPart -lt $rightPart) { return -1 }
        if ($leftPart -gt $rightPart) { return 1 }
    }
    return 0
}
$minimumJava = @(([string]$versions.java.minVersion -split '\.') | ForEach-Object { [long]$_ })
$jdkCandidates = @(foreach ($directory in $jdkDirectories) {
    $root = $directory.FullName
    if (@('release','bin\java.exe','bin\javac.exe','bin\jar.exe','include\jni.h','include\win32\jni_md.h','bin\server\jvm.dll') |
            Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_) -PathType Leaf) }) { continue }
    $release = Get-Content -LiteralPath (Join-Path $root 'release') -Raw
    if ($release -notmatch '(?m)^OS_ARCH="x86_64"\r?$') { continue }
    $runtime = [regex]::Match($release, '(?m)^JAVA_RUNTIME_VERSION="([^"]+)"\r?$')
    if (-not $runtime.Success) { $runtime = [regex]::Match($release, '(?m)^JAVA_VERSION="([^"]+)"\r?$') }
    $runtimeText = $runtime.Groups[1].Value
    if ($runtimeText -match '(?i)(?:^|[-+.])(?:ea|beta|rc)\d*(?:[-+.]|$)') { continue }
    $number = [regex]::Match($runtimeText, '^(\d+(?:\.\d+)*)(?:\+(\d+)(?:-[A-Za-z0-9.-]+)?)?(?:-LTS)?$')
    if (-not $number.Success) { continue } # Never select early-access builds.
    $parts = @($number.Groups[1].Value -split '\.' | ForEach-Object { [long]$_ })
    if ($parts[0] -ne $versions.java.major -or (Compare-VersionParts $parts $minimumJava) -lt 0) { continue }
    $buildNumber = if ($number.Groups[2].Success) { [long]$number.Groups[2].Value } else { 0 }
    [pscustomobject]@{FullName=$root; Parts=$parts; BuildNumber=$buildNumber}
})
if (-not $jdkCandidates.Count) { throw "Falta un JDK OpenJDK $($versions.java.minVersion) o posterior, estable, x64 y completo en $jdkPattern; no se usa el JBR de Android Studio ni otro JDK." }
$partCount = ($jdkCandidates | ForEach-Object { $_.Parts.Count } | Measure-Object -Maximum).Maximum
$jdks = @($jdkCandidates | Sort-Object @{Expression={
    $parts = $_.Parts
    $sortParts = for ($index = 0; $index -lt $partCount; ++$index) {
        if ($index -lt $parts.Count) { '{0:D20}' -f $parts[$index] } else { '{0:D20}' -f 0 }
    }
    $sortParts -join '.'
};Descending=$true}, @{Expression='BuildNumber';Descending=$true}, FullName)
$javaHome = $jdks[0].FullName
& $python -I -S -B (Join-Path $PSScriptRoot 'verify_physical_tools.py') `
    (Join-Path $javaHome 'bin\java.exe') (Join-Path $javaHome 'include\jni.h') (Join-Path $javaHome 'bin\server\jvm.dll')
if ($LASTEXITCODE -ne 0) { throw 'El JDK completo debe estar fuera de los datos privados del agente.' }
Read-ToolVersion (Join-Path $javaHome 'bin\java.exe') @('--version') ('(?m)^openjdk ' + $versions.java.major + '(?:\.[0-9]+)*(?:\s|\+)') "OpenJDK $($versions.java.major) estable"
Read-ToolVersion (Join-Path $javaHome 'bin\javac.exe') @('--version') ('^javac ' + $versions.java.major + '(?:\.[0-9]+)*$') "JDK $($versions.java.major)"
if ((Get-Content -LiteralPath (Join-Path $javaHome 'release') -Raw) -notmatch '(?m)^OS_ARCH="x86_64"\r?$') {
    throw "El JDK requerido debe ser x64: $javaHome"
}

# Only this process and its children use these versions. Existing user/machine
# variables and all previously installed tools remain untouched.
$env:LGAED_JAVA_HOME = $javaHome
$env:JAVA_HOME = $javaHome
$env:LGAED_PYTHON_EXECUTABLE = $python
$env:LGAED_FFMPEG_EXECUTABLE = $ffmpeg
if ($Scope -eq 'Application') { $env:VULKAN_SDK = $vulkan }
$toolDirectories = @((Split-Path -Parent $cmake), (Split-Path -Parent $python),
    (Join-Path (Split-Path -Parent $python) 'Scripts'), (Split-Path -Parent $ffmpeg))
if ($Scope -eq 'Application') { $toolDirectories += (Join-Path $vulkan 'Bin') }
$toolDirectories += @((Join-Path $javaHome 'bin'), (Split-Path -Parent $git))
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$pathEntries = foreach ($entry in @($toolDirectories) + @($env:PATH -split ';')) {
    if ($entry -and $seen.Add($entry.TrimEnd('\','/'))) { $entry }
}
$env:PATH = $pathEntries -join ';'
[pscustomobject]@{
    Scope=$Scope
    VsRoot=$vsRoot; CMake=$cmake; CTest=$ctest; VcVars=$vcvars; MSVCToolset=$msvcToolset; WindowsSdk=$windowsSdk
    WindowsSdkRoot=$sdkRoot; Ninja=$ninja; Python=$python; Git=$git; FFmpeg=$ffmpeg; VulkanSDK=$vulkan; JavaHome=$javaHome; Manifest=$manifestPath
}
