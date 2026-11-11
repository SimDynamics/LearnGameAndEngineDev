[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

# Real CMD windows on a disposable GitHub-hosted Windows runner.
# The script does not inspect the student's E: drive, mimic screenshots,
# install Visual Studio, or claim a manual student reconstruction.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Only GitHub-hosted disposable Windows runners may capture U4L3.'
}
if (-not $env:RUNNER_TEMP -or -not $env:GITHUB_SHA) {
    throw 'The hosted-runner provenance is incomplete.'
}

$qaRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaOutput = Join-Path $qaRoot 'out\qa\assetcheck-u4l3-console'
$qaWorking = Join-Path $env:RUNNER_TEMP 'assetcheck-u4l3-real-console'
if (Test-Path -LiteralPath $qaWorking) {
    throw "The isolated working directory already exists: $qaWorking"
}
New-Item -ItemType Directory -Path $qaWorking -Force | Out-Null
New-Item -ItemType Directory -Path $qaOutput -Force | Out-Null

$pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
if (-not $pf86) { throw 'ProgramFiles(x86) is not available on this Windows runner.' }
$qaVswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $qaVswhere)) {
    throw 'The real Visual Studio installer locator was not found.'
}
$qaInstances = @(& $qaVswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8 | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0 -or $qaInstances.Count -ne 1) {
    throw 'Expected one installed VS 2026 instance with C++ x64.'
}
$qaInstance = $qaInstances[0]
$qaDeveloper = Join-Path $qaInstance.installationPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $qaDeveloper)) {
    throw 'The installed Visual Studio developer console is unavailable.'
}

$qaLesson = Join-Path $qaRoot 'app\src\main\assets\content\es\programming\programming_practice_tools\proyecto_assetcheck\lesson_preparar_y_comprobar_cmake_en_windows.json'
$qaJson = Get-Content -LiteralPath $qaLesson -Encoding utf8 -Raw | ConvertFrom-Json -Depth 64
$qaBlocks = @($qaJson.pages | ForEach-Object { $_.blocks } | Where-Object { $_.type -eq 'code' })
function Get-UniqueLessonCode([string] $language, [string] $token) {
    $found = @($qaBlocks | Where-Object {
        $_.language -eq $language -and $_.code.Contains($token)
    })
    if ($found.Count -ne 1) {
        throw "U4L3 no longer has exactly one $language example containing $token"
    }
    return [string] $found[0].code
}
$qaCmake = Get-UniqueLessonCode 'cmake' 'project(PreparacionAssetCheck LANGUAGES CXX)'
$qaOriginal = Get-UniqueLessonCode 'cpp' 'std::puts("AssetCheck: entorno listo")'
$qaUpdated = Get-UniqueLessonCode 'cpp' 'std::puts("AssetCheck: entorno actualizado")'
if ($qaCmake -notmatch 'add_executable\(preparar_entorno main\.cpp\)' -or $qaOriginal -ceq $qaUpdated) {
    throw 'The U4L3 exercise contract has changed; no screenshot should be taken.'
}
$qaUtf8 = [System.Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $qaWorking 'CMakeLists.txt'), $qaCmake, $qaUtf8)
[IO.File]::WriteAllText((Join-Path $qaWorking 'main.cpp'), $qaOriginal, $qaUtf8)

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public static class AssetCheckU4L3NativeWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    delegate bool WindowVisitor(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowVisitor callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr window, StringBuilder title, int length);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr window, int x, int y, int width, int height, bool repaint);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct StartupInfo {
        public int cb; public string reserved, desktop, title;
        public int x, y, width, height, charsX, charsY, fill, flags;
        public short show, reservedCount; public IntPtr reservedBytes, input, output, error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo {
        public IntPtr process, thread; public int processId, threadId;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string application, StringBuilder command,
        IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles,
        uint flags, IntPtr environment, string directory,
        ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    public static IntPtr FindTitledWindow(string title) {
        IntPtr match = IntPtr.Zero;
        EnumWindows((window, ignored) => {
            var actual = new StringBuilder(512);
            GetWindowText(window, actual, actual.Capacity);
            if (actual.ToString().IndexOf(title, StringComparison.Ordinal) < 0)
                return true;
            match = window;
            return false;
        }, IntPtr.Zero);
        return match;
    }

    public static Process StartConsole(string command, string batch, string directory, string title) {
        var startup = new StartupInfo {
            cb = Marshal.SizeOf(typeof(StartupInfo)), title = title,
            flags = 1, show = 1
        };
        ProcessInfo info;
        if (!CreateProcess(command,
            new StringBuilder("\"" + command + "\" /d /c \"" + batch + "\""),
            IntPtr.Zero, IntPtr.Zero, false, 0x10, IntPtr.Zero,
            directory, ref startup, out info))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try { return Process.GetProcessById(info.processId); }
        finally { CloseHandle(info.thread); CloseHandle(info.process); }
    }
}
'@

function Save-NativeConsole([IntPtr] $window, [string] $name) {
    [AssetCheckU4L3NativeWindow]::ShowWindow($window, 9) | Out-Null
    [AssetCheckU4L3NativeWindow]::MoveWindow($window, 0, 0, 1100, 760, $true) | Out-Null
    [AssetCheckU4L3NativeWindow]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Milliseconds 350
    $bounds = New-Object AssetCheckU4L3NativeWindow+Rect
    if (-not [AssetCheckU4L3NativeWindow]::GetWindowRect($window, [ref] $bounds)) {
        throw 'Could not retrieve the genuine CMD window rectangle.'
    }
    $width = $bounds.Right - $bounds.Left
    $height = $bounds.Bottom - $bounds.Top
    if ($width -lt 320 -or $height -lt 200) {
        throw 'The native command window is too small to capture.'
    }
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $png = Join-Path $qaOutput ($name + '.png')
    try {
        $dc = $graphics.GetHdc()
        try { $printed = [AssetCheckU4L3NativeWindow]::PrintWindow($window, $dc, 2) }
        finally { $graphics.ReleaseHdc($dc) }
        if (-not $printed) { throw 'Windows did not capture pixels from the real CMD window.' }
        $bitmap.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $graphics.Dispose(); $bitmap.Dispose() }
    if ((Get-Item -LiteralPath $png).Length -lt 3000) {
        throw 'The native screenshot is suspiciously small; it is not reliable evidence.'
    }
    Write-Host "Captured real CMD window: $name.png"
}

function Invoke-NativeCapture([string] $name, [string] $body) {
    $ready = Join-Path $qaWorking ($name + '.ready')
    $batch = Join-Path $qaWorking ($name + '.cmd')
    $title = "AssetCheck U4L3 CI $name"
    $commands = @"
@echo off
mode con cols=120 lines=41
call "$qaDeveloper" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 11
title $title
cd /d "$qaWorking"
if errorlevel 1 exit /b 12
cls
@echo on
$body
@echo off
echo completed>"$ready"
pause >nul
"@
    [IO.File]::WriteAllText($batch, $commands, [System.Text.Encoding]::ASCII)
    Copy-Item -LiteralPath $batch -Destination $qaOutput
    $process = [AssetCheckU4L3NativeWindow]::StartConsole($env:ComSpec, $batch, $qaWorking, $title)
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(130)
        while (-not (Test-Path -LiteralPath $ready)) {
            if ($process.HasExited) {
                throw "The native command process exited before $name could be captured: $($process.ExitCode)."
            }
            if ([DateTime]::UtcNow -gt $deadline) {
                throw "The native command process exceeded its $name capture deadline."
            }
            Start-Sleep -Milliseconds 150
        }
        $window = [AssetCheckU4L3NativeWindow]::FindTitledWindow($title)
        if ($window -eq [IntPtr]::Zero) {
            throw "The real CMD window for $name was not found; no screenshot was fabricated."
        }
        Save-NativeConsole $window $name
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}

function Confirm-Result([string] $stage, [string] $expected) {
    $binary = Join-Path $qaWorking 'build\preparar_entorno.exe'
    if (-not (Test-Path -LiteralPath $binary)) {
        throw "$stage has no real compiled executable."
    }
    $actual = & $binary
    if ($LASTEXITCODE -ne 0 -or $actual -cne $expected) {
        throw "$stage produced '$actual' (exit $LASTEXITCODE), not '$expected'."
    }
    Write-Host "PASS $stage: $actual (exit=0)"
}

Invoke-NativeCapture 'u4l3_configurar' @'
where cl
where cmake
cmake --version
where ninja
ninja --version
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
@if errorlevel 1 exit /b 20
echo CodigoConfiguracion=%ERRORLEVEL%
'@
if (-not (Test-Path -LiteralPath (Join-Path $qaWorking 'build\CMakeCache.txt')) -or
    -not (Test-Path -LiteralPath (Join-Path $qaWorking 'build\build.ninja'))) {
    throw 'Configuration was not followed by actual Ninja/CMake cache generation.'
}

Invoke-NativeCapture 'u4l3_construir' @'
type main.cpp
cmake --build build --target preparar_entorno
@if errorlevel 1 exit /b 21
build\preparar_entorno.exe
@if errorlevel 1 exit /b 22
echo Codigo=%ERRORLEVEL%
'@
Confirm-Result 'initial' 'AssetCheck: entorno listo'

[IO.File]::WriteAllText((Join-Path $qaWorking 'main.cpp'), $qaUpdated, $qaUtf8)
Confirm-Result 'stale-before-rebuild' 'AssetCheck: entorno listo'

Invoke-NativeCapture 'u4l3_reconstruir' @'
type main.cpp
build\preparar_entorno.exe
@if errorlevel 1 exit /b 23
cmake --build build --target preparar_entorno
@if errorlevel 1 exit /b 24
build\preparar_entorno.exe
@if errorlevel 1 exit /b 25
echo CodigoDespues=%ERRORLEVEL%
'@
Confirm-Result 'updated-after-rebuild' 'AssetCheck: entorno actualizado'

$qaImages = @('u4l3_configurar.png', 'u4l3_construir.png', 'u4l3_reconstruir.png')
foreach ($name in $qaImages) {
    if (-not (Test-Path -LiteralPath (Join-Path $qaOutput $name))) {
        throw "Missing actual native capture: $name"
    }
}
$qaOS = Get-CimInstance Win32_OperatingSystem
$qaEvidence = [ordered]@{
    origin = 'Real visible native CMD windows captured with Win32 PrintWindow on a disposable GitHub-hosted Windows runner'
    claimLimit = 'This is not the student PC, not a first-time VS installation, not the standalone AssetCheck project, and not a manual student capture.'
    githubCommit = $env:GITHUB_SHA
    recordedAtUtc = [DateTime]::UtcNow.ToString('o')
    operatingSystem = $qaOS.Caption
    operatingSystemVersion = $qaOS.Version
    visualStudioProduct = $qaInstance.productId
    visualStudioVersion = $qaInstance.installationVersion
    developerConsole = $qaDeveloper
    cmakeVersion = (& cmake --version | Select-Object -First 1)
    ninjaVersion = (& ninja --version)
    lesson = 'U4L3 preparar y comprobar CMake en Windows'
    source = 'lesson_preparar_y_comprobar_cmake_en_windows.json'
    cmakeTarget = 'preparar_entorno'
    generator = 'Ninja'
    configuration = 'Debug'
    initialOutput = 'AssetCheck: entorno listo'
    staleBinaryOutput = 'AssetCheck: entorno listo'
    rebuiltOutput = 'AssetCheck: entorno actualizado'
    initialExitCode = 0
    staleExitCode = 0
    rebuiltExitCode = 0
    captures = $qaImages
    installationTested = $false
    studentComputerTested = $false
    standaloneAssetCheckTested = $false
} 
$qaEvidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $qaOutput 'provenance.json') -Encoding utf8
Write-Host 'PASS: three real U4L3 Windows CMD screenshots, source rebuild and provenance preserved.'
