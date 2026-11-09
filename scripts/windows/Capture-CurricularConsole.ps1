[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

# These are real command windows on a disposable runner. This recipe never
# creates a substitute image from text, nor claims a first-time installation.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Use this capture recipe only on a disposable GitHub-hosted Windows runner.'
}
$qaRepository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaOutput = Join-Path $qaRepository 'out\qa\curricular-console'
$qaWorking = 'C:\LGAED-CurricularQA\comprobar_entorno'
New-Item -ItemType Directory -Force -Path $qaOutput, $qaWorking | Out-Null
$qaVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $qaVswhere)) { throw 'The installed Visual Studio locator is missing.' }
$qaInstances = & $qaVswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -version '[18.0,19.0)' -format json -utf8 | ConvertFrom-Json
if (-not $qaInstances) { throw 'No Visual Studio 2026 installation with C++ tools was found.' }
$qaInstance = @($qaInstances)[0]
$qaDeveloper = Join-Path $qaInstance.installationPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $qaDeveloper)) { throw 'The installation has no developer command file.' }

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public static class CurricularWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
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
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr process, thread; public int processId, threadId; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string application, StringBuilder command, IntPtr processAttributes,
        IntPtr threadAttributes, bool inheritHandles, uint flags, IntPtr environment, string directory,
        ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static Process StartConsole(string executable, string batch, string directory, string title) {
        var startup = new StartupInfo { cb = Marshal.SizeOf(typeof(StartupInfo)), title = title, flags = 1, show = 1 };
        ProcessInfo info;
        // CREATE_NEW_CONSOLE prevents inheriting the runner's redirected/pseudo console.
        if (!CreateProcess(executable, new StringBuilder("\"" + executable + "\" /d /c \"" + batch + "\""),
            IntPtr.Zero, IntPtr.Zero, false, 0x10, IntPtr.Zero, directory, ref startup, out info))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try { return Process.GetProcessById(info.processId); }
        finally { CloseHandle(info.thread); CloseHandle(info.process); }
    }
}
'@

function Save-ActualWindow([IntPtr] $qaWindow, [string] $qaPath) {
    [CurricularWindowCapture]::ShowWindow($qaWindow, 9) | Out-Null
    [CurricularWindowCapture]::MoveWindow($qaWindow, 0, 0, 1000, 640, $true) | Out-Null
    [CurricularWindowCapture]::SetForegroundWindow($qaWindow) | Out-Null
    Start-Sleep -Milliseconds 300
    $qaRect = New-Object CurricularWindowCapture+Rect
    if (-not [CurricularWindowCapture]::GetWindowRect($qaWindow, [ref] $qaRect)) { throw 'Cannot locate the real console window.' }
    $qaWidth = $qaRect.Right - $qaRect.Left
    $qaHeight = $qaRect.Bottom - $qaRect.Top
    if ($qaWidth -lt 300 -or $qaHeight -lt 200) { throw 'The real console window has invalid capture dimensions.' }
    $qaBitmap = [System.Drawing.Bitmap]::new($qaWidth, $qaHeight)
    $qaGraphics = [System.Drawing.Graphics]::FromImage($qaBitmap)
    try {
        $qaDc = $qaGraphics.GetHdc()
        try { $qaPrinted = [CurricularWindowCapture]::PrintWindow($qaWindow, $qaDc, 2) }
        finally { $qaGraphics.ReleaseHdc($qaDc) }
        if (-not $qaPrinted) { throw 'Windows did not provide the real console pixels.' }
        $qaBitmap.Save($qaPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $qaGraphics.Dispose(); $qaBitmap.Dispose() }
}

function Invoke-Capture([string] $qaId, [string] $qaBody) {
    $qaReady = Join-Path $qaWorking ($qaId + '.ready')
    $qaBatch = Join-Path $qaWorking ($qaId + '.cmd')
    $qaTitle = 'LGAED curricular ' + $qaId
    @"
@echo off
mode con cols=110 lines=32
call "$qaDeveloper" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 11
title $qaTitle
cd /d "$qaWorking"
cls
@echo on
$qaBody
@echo completed>"$qaReady"
@echo off
pause >nul
"@ | Set-Content -LiteralPath $qaBatch -Encoding ascii
    Copy-Item -LiteralPath $qaBatch -Destination $qaOutput
    $qaProcess = [CurricularWindowCapture]::StartConsole($env:ComSpec, $qaBatch, $qaWorking, $qaTitle)
    try {
        $qaLimit = [DateTime]::UtcNow.AddSeconds(30)
        while (-not (Test-Path -LiteralPath $qaReady)) {
            if ($qaProcess.HasExited) { throw "The real command window failed with exit $($qaProcess.ExitCode)." }
            if ([DateTime]::UtcNow -gt $qaLimit) { throw 'The command window did not reach its capture point.' }
            Start-Sleep -Milliseconds 100
        }
        $qaWindow = [CurricularWindowCapture]::FindWindow($null, $qaTitle)
        if ($qaWindow -eq [IntPtr]::Zero) { throw 'The native command window is unavailable; no screenshot has been fabricated.' }
        Save-ActualWindow $qaWindow (Join-Path $qaOutput ($qaId + '.png'))
    }
    finally { if (-not $qaProcess.HasExited) { Stop-Process -Id $qaProcess.Id -Force } }
}

@'
#include <iostream>

int main()
{
    std::cout << "Entorno preparado\n";
    return 0;
}
'@ | Set-Content -LiteralPath (Join-Path $qaWorking 'comprobar_entorno.cpp') -Encoding ascii
Copy-Item -LiteralPath (Join-Path $qaWorking 'comprobar_entorno.cpp') -Destination $qaOutput

Invoke-Capture 'p1_windows_consola_compilador' @'
ver
where cl
cl
'@

Invoke-Capture 'p1_windows_consola_construccion' @'
type comprobar_entorno.cpp
cl /nologo /EHsc /W4 /std:c++23preview /Fe:comprobar_entorno.exe comprobar_entorno.cpp
@if errorlevel 1 exit /b 12
.\comprobar_entorno.exe
@if errorlevel 1 exit /b 13
echo %ERRORLEVEL%
'@

$qaActual = & (Join-Path $qaWorking 'comprobar_entorno.exe')
if ($LASTEXITCODE -ne 0 -or $qaActual -cne 'Entorno preparado') { throw 'The constructed program did not produce the required result.' }
$qaOs = Get-CimInstance Win32_OperatingSystem
@{
    commit = $env:GITHUB_SHA
    capturedAtUtc = [DateTime]::UtcNow.ToString('o')
    environment = 'Disposable GitHub-hosted runner; C++ tools already installed; no initial installation tested.'
    operatingSystem = $qaOs.Caption
    operatingSystemVersion = $qaOs.Version
    installationVersion = $qaInstance.installationVersion
    productId = $qaInstance.productId
    displayName = $qaInstance.displayName
    developerCommandFile = $qaDeveloper
    source = 'comprobar_entorno.cpp'
    standardOutput = $qaActual
    exitCode = 0
    captures = @('p1_windows_consola_compilador.png', 'p1_windows_consola_construccion.png')
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $qaOutput 'provenance.json') -Encoding utf8
