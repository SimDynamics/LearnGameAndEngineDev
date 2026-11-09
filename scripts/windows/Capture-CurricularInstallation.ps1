[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This installation exercise is restricted to a disposable GitHub-hosted Windows runner.'
}
$qaRepository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaOutput = Join-Path $qaRepository 'out\qa\curricular-installation'
$qaWorking = 'C:\LGAED-CurricularQA\installation-proof'
$qaInstallRoot = 'C:\LGAED-CurricularQA\VSCommunity'
New-Item -ItemType Directory -Force -Path $qaOutput, $qaWorking | Out-Null
$qaVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$qaInstaller = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
$qaProduct = 'Microsoft.VisualStudio.Product.Community'
$qaCompilerComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
$qaCoreWorkload = 'Microsoft.VisualStudio.Workload.CoreEditor'
$qaCppWorkload = 'Microsoft.VisualStudio.Workload.NativeDesktop'
$qaEvidence = [ordered]@{
    commit = $env:GITHUB_SHA
    startedAtUtc = [DateTime]::UtcNow.ToString('o')
    scope = 'New Community instance on a disposable runner with Enterprise and shared Windows SDK components already installed. Not an empty machine or Windows 11.'
    operatingSystem = (Get-CimInstance Win32_OperatingSystem).Caption
    initialInstances = @(& $qaVswhere -all -products '*' -format json -utf8 | ConvertFrom-Json)
    installationRoot = $qaInstallRoot
    coreWorkload = $qaCoreWorkload
    compilerComponent = $qaCompilerComponent
    cppWorkload = $qaCppWorkload
    operations = @()
}
function Save-Evidence {
    $qaEvidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $qaOutput 'provenance.json') -Encoding utf8
}
Save-Evidence
if (Test-Path -LiteralPath $qaInstallRoot) { throw 'The isolated installation destination already exists; it is preserved.' }
if (@($qaEvidence.initialInstances | Where-Object productId -eq $qaProduct).Count -ne 0) {
    throw 'Community already exists on this image; the missing-instance exercise does not apply.'
}
if ((Get-PSDrive -Name C).Free -lt 12GB) { throw 'Insufficient free space for an isolated Community installation.' }

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public static class CurricularWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
    delegate bool WindowVisitor(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowVisitor visitor, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder title, int length);
    public static IntPtr FindTitledWindow(string expectedTitle) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            var title = new StringBuilder(1024);
            GetWindowText(window, title, title.Capacity);
            // Elevated consoles prepend a localized Administrator label.
            if (title.ToString().IndexOf(expectedTitle, StringComparison.Ordinal) < 0) return true;
            found = window;
            return false;
        }, IntPtr.Zero);
        return found;
    }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
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

function Save-ActualWindow([IntPtr] $qaWindow, [string] $qaPath,
                           [int] $qaTargetWidth = 1000, [int] $qaTargetHeight = 640) {
    [CurricularWindowCapture]::ShowWindow($qaWindow, 9) | Out-Null
    $qaScreen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    [CurricularWindowCapture]::MoveWindow($qaWindow, $qaScreen.Left, $qaScreen.Top,
        [Math]::Min($qaTargetWidth, $qaScreen.Width), [Math]::Min($qaTargetHeight, $qaScreen.Height), $true) | Out-Null
    [CurricularWindowCapture]::SetForegroundWindow($qaWindow) | Out-Null
    Start-Sleep -Milliseconds 700
    $qaRect = New-Object CurricularWindowCapture+Rect
    if (-not [CurricularWindowCapture]::GetWindowRect($qaWindow, [ref] $qaRect)) { throw 'Cannot locate the real console window.' }
    $qaWidth = $qaRect.Right - $qaRect.Left
    $qaHeight = $qaRect.Bottom - $qaRect.Top
    if ($qaWidth -lt 300 -or $qaHeight -lt 200) { throw 'The real console window has invalid capture dimensions.' }
    $qaBitmap = [System.Drawing.Bitmap]::new($qaWidth, $qaHeight)
    $qaGraphics = [System.Drawing.Graphics]::FromImage($qaBitmap)
    try {
        if ([CurricularWindowCapture]::GetForegroundWindow() -ne $qaWindow) {
            throw 'The actual installation or command window is not foreground.'
        }
        if ($qaRect.Left -lt $qaScreen.Left -or $qaRect.Top -lt $qaScreen.Top -or
            $qaRect.Right -gt $qaScreen.Right -or $qaRect.Bottom -gt $qaScreen.Bottom) {
            throw 'The actual window does not fit the display; refusing a clipped screenshot.'
        }
        $qaGraphics.CopyFromScreen($qaRect.Left, $qaRect.Top, 0, 0, $qaBitmap.Size)
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
        $qaWindow = [CurricularWindowCapture]::FindTitledWindow($qaTitle)
        if ($qaWindow -eq [IntPtr]::Zero) { throw 'The native command window is unavailable; no screenshot has been fabricated.' }
        Save-ActualWindow $qaWindow (Join-Path $qaOutput ($qaId + '.png'))
    }
    finally { if (-not $qaProcess.HasExited) { Stop-Process -Id $qaProcess.Id -Force } }
}


function Find-InstallerWindow {
    foreach ($qaCaption in @('Visual Studio Installer', 'Instalador de Visual Studio')) {
        $qaHandle = [CurricularWindowCapture]::FindTitledWindow($qaCaption)
        if ($qaHandle -ne [IntPtr]::Zero) { return $qaHandle }
    }
    return [IntPtr]::Zero
}
function Save-InstallerSnapshot([string] $qaName) {
    $qaHandle = Find-InstallerWindow
    if ($qaHandle -eq [IntPtr]::Zero) { return $false }
    try {
        Save-ActualWindow $qaHandle (Join-Path $qaOutput ($qaName + '.png'))
        return $true
    } catch {
        # A bootstrapper progress window may disappear or be replaced while
        # its native handle is being captured. Keep the diagnostic and retry
        # later. The required final selection captures still fail normally.
        $qaEvidence.lastProgressCaptureDiagnostic = $_.Exception.Message
        Save-Evidence
        return $false
    }
}
function Invoke-Installer([string] $qaPhase, [string[]] $qaArguments) {
    $qaRecord = [ordered]@{
        phase = $qaPhase
        executable = $qaBootstrap
        arguments = $qaArguments
        startedAtUtc = [DateTime]::UtcNow.ToString('o')
        screenshots = @()
    }
    $qaEvidence.operations += $qaRecord
    Save-Evidence
    $qaProcess = Start-Process -FilePath $qaBootstrap -ArgumentList $qaArguments -PassThru
    try {
        $qaDeadline = [DateTime]::UtcNow.AddMinutes(18)
        $qaNextCapture = [DateTime]::UtcNow.AddSeconds(8)
        $qaCaptureCount = 0
        while (-not $qaProcess.HasExited) {
            if ([DateTime]::UtcNow -gt $qaDeadline) { throw 'The installation exceeded its bounded test duration.' }
            if ($qaCaptureCount -lt 8 -and [DateTime]::UtcNow -gt $qaNextCapture) {
                $qaCaptureName = '{0}_{1:d2}' -f $qaPhase, ($qaCaptureCount + 1)
                if (Save-InstallerSnapshot $qaCaptureName) {
                    $qaRecord.screenshots += ($qaCaptureName + '.png')
                    $qaCaptureCount++
                }
                $qaNextCapture = [DateTime]::UtcNow.AddSeconds(35)
                Save-Evidence
            }
            Start-Sleep -Milliseconds 250
            $qaProcess.Refresh()
        }
        $qaRecord.exitCode = $qaProcess.ExitCode
        $qaRecord.completedAtUtc = [DateTime]::UtcNow.ToString('o')
        Save-Evidence
        if ($qaProcess.ExitCode -ne 0) { throw "The installer returned $($qaProcess.ExitCode); no successful continuation is claimed." }
    }
    finally { $qaProcess.Dispose() }
}
function Inspect-InstalledSelection([string] $qaName, [string] $qaChannel) {
    $qaUiProcess = Start-Process -FilePath $qaInstaller -WorkingDirectory $qaWorking -ArgumentList @(
        'modify', '--installPath', ('"' + $qaInstallRoot + '"'), '--channelId', $qaChannel
    ) -PassThru
    try {
        $qaLimit = [DateTime]::UtcNow.AddSeconds(45)
        $qaWindow = [IntPtr]::Zero
        while ($qaWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $qaLimit) {
            Start-Sleep -Milliseconds 250
            $qaWindow = Find-InstallerWindow
        }
        if ($qaWindow -eq [IntPtr]::Zero) { throw 'The real installation-selection window is unavailable.' }
        Add-Type -AssemblyName UIAutomationClient
        Add-Type -AssemblyName UIAutomationTypes
        $qaScreen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
        [CurricularWindowCapture]::MoveWindow($qaWindow, $qaScreen.Left, $qaScreen.Top,
            [Math]::Min(1200, $qaScreen.Width), [Math]::Min(900, $qaScreen.Height), $true) | Out-Null
        $qaElement = [System.Windows.Automation.AutomationElement]::FromHandle($qaWindow)
        $qaReadyLimit = [DateTime]::UtcNow.AddMinutes(2)
        $qaLoaded = $false
        do {
            $qaNodes = $qaElement.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                                         [System.Windows.Automation.Condition]::TrueCondition)
            # The first run captured the observed LoadingLiveTextBlock. Wait for
            # the actual workload controls instead of assuming a fixed delay.
            $qaLoading = @($qaNodes | Where-Object {
                -not $_.Current.IsOffscreen -and
                ($_.Current.AutomationId -eq 'LoadingLiveTextBlock' -or
                 $_.Current.Name -match 'Getting things ready|Preparando todo')
            }).Count -ne 0
            $qaWorkloadLabels = @($qaNodes | Where-Object {
                -not $_.Current.IsOffscreen -and
                $_.Current.Name -match '^Workloads$|^Cargas de trabajo$|Desktop development with C\+\+|Desarrollo para el escritorio con C\+\+'
            })
            $qaLoaded = -not $qaLoading -and $qaWorkloadLabels.Count -gt 0 -and $qaNodes.Count -gt 25
            if (-not $qaLoaded) { Start-Sleep -Milliseconds 500 }
        } while (-not $qaLoaded -and [DateTime]::UtcNow -lt $qaReadyLimit)

        # Read and scroll the observed C++ card; never toggle a component here.
        $qaCppCards = @($qaNodes | Where-Object {
            $_.Current.Name -match '^Desktop development with C\+\+|^Desarrollo para el escritorio con C\+\+'
        })
        if ($qaLoaded -and $qaCppCards.Count -gt 0) {
            $qaCard = $qaCppCards[0]
            if ($qaCard.Current.IsOffscreen) {
                $qaScrollItem = $null
                if ($qaCard.TryGetCurrentPattern([System.Windows.Automation.ScrollItemPattern]::Pattern, [ref] $qaScrollItem)) {
                    $qaScrollItem.ScrollIntoView()
                } else {
                    $qaAncestor = [System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($qaCard)
                    $qaScroller = $null
                    while ($null -ne $qaAncestor) {
                        if ($qaAncestor.TryGetCurrentPattern([System.Windows.Automation.ScrollPattern]::Pattern, [ref] $qaScroller) -and
                            $qaScroller.Current.VerticallyScrollable) { break }
                        $qaScroller = $null
                        $qaAncestor = [System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($qaAncestor)
                    }
                    for ($qaScrollCount = 0; $null -ne $qaScroller -and $qaCard.Current.IsOffscreen -and $qaScrollCount -lt 12; $qaScrollCount++) {
                        $qaScroller.Scroll([System.Windows.Automation.ScrollAmount]::NoAmount,
                                           [System.Windows.Automation.ScrollAmount]::LargeIncrement)
                        Start-Sleep -Milliseconds 200
                    }
                }
            }
        }
        Start-Sleep -Milliseconds 400
        Save-ActualWindow $qaWindow (Join-Path $qaOutput ($qaName + '.png')) 1200 900
        # Preserve the actual installer subtree, including a useful diagnostic if
        # it never loaded. No browser, account or unrelated window is inspected.
        $qaNodes = $qaElement.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                                     [System.Windows.Automation.Condition]::TrueCondition)
        $qaControls = foreach ($qaNode in $qaNodes) {
            $qaToggle = $null
            if ($qaNode.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref] $qaToggle)) {
                $qaToggleState = $qaToggle.Current.ToggleState.ToString()
            } else { $qaToggleState = $null }
            [ordered]@{name = $qaNode.Current.Name; id = $qaNode.Current.AutomationId;
                type = $qaNode.Current.ControlType.ProgrammaticName; offscreen = $qaNode.Current.IsOffscreen; toggle = $qaToggleState}
        }
        $qaControls | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $qaOutput ($qaName + '-controls.json')) -Encoding utf8
        if (-not $qaLoaded) { throw 'The installer did not finish loading its workload selection.' }
        if ($qaCppCards.Count -eq 0 -or $qaCppCards[0].Current.IsOffscreen) {
            throw 'The C++ workload card is not visible; no selection screenshot is claimed.'
        }
        [CurricularWindowCapture]::PostMessage($qaWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Seconds 2
    }
    finally { $qaUiProcess.Dispose() }
}

$qaBootstrap = Join-Path $qaWorking 'vs_community.exe'
$qaBootstrapUrl = 'https://aka.ms/vs/stable/vs_community.exe'
Invoke-WebRequest -Uri $qaBootstrapUrl -OutFile $qaBootstrap
$qaSignature = Get-AuthenticodeSignature -LiteralPath $qaBootstrap
if ($qaSignature.Status -ne 'Valid' -or $qaSignature.SignerCertificate.Subject -notmatch '(^|,\s*)(CN|O)=Microsoft Corporation(,|$)') {
    throw 'The downloaded bootstrapper does not have a valid Microsoft signature.'
}
$qaEvidence.bootstrapper = [ordered]@{
    url = $qaBootstrapUrl
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaBootstrap).Hash
    signer = $qaSignature.SignerCertificate.Subject
    productVersion = (Get-Item -LiteralPath $qaBootstrap).VersionInfo.ProductVersion
}
Save-Evidence
try {
    Invoke-Installer 'p1_windows_instalar_editor' @(
        '--installPath', ('"' + $qaInstallRoot + '"'), '--add', $qaCoreWorkload,
        '--addProductLang', 'es-ES', '--passive', '--norestart', '--wait'
    )
    $qaCommunity = @(& $qaVswhere -all -products $qaProduct -format json -utf8 | ConvertFrom-Json |
        Where-Object installationPath -eq $qaInstallRoot)
    if ($qaCommunity.Count -ne 1 -or -not $qaCommunity[0].isComplete) { throw 'The new Community instance was not completed.' }
    $qaCompilerBefore = @(& $qaVswhere -all -products $qaProduct -requires $qaCompilerComponent -format json -utf8 | ConvertFrom-Json |
        Where-Object installationPath -eq $qaInstallRoot)
    $qaCompilerFilesBefore = @(Get-ChildItem -LiteralPath (Join-Path $qaInstallRoot 'VC\Tools\MSVC') -Filter cl.exe -File -Recurse -ErrorAction SilentlyContinue)
    $qaEvidence.coreInstance = $qaCommunity[0]
    $qaEvidence.compilerComponentPresentBefore = $qaCompilerBefore.Count -ne 0
    $qaEvidence.compilerFilesBefore = @($qaCompilerFilesBefore | ForEach-Object FullName)
    Save-Evidence
    if ($qaCompilerBefore.Count -ne 0 -or $qaCompilerFilesBefore.Count -ne 0) {
        throw 'C++ tools are already present in the new instance; no missing-component claim can be made.'
    }
    Inspect-InstalledSelection 'p1_windows_cpp_ausente' $qaCommunity[0].channelId
    Invoke-Installer 'p1_windows_agregar_cpp' @(
        'modify', '--installPath', ('"' + $qaInstallRoot + '"'), '--channelId', $qaCommunity[0].channelId,
        '--add', $qaCppWorkload, '--add', $qaCompilerComponent, '--passive', '--norestart', '--wait'
    )
    $qaCppInstance = @(& $qaVswhere -all -products $qaProduct -requires $qaCompilerComponent -format json -utf8 | ConvertFrom-Json |
        Where-Object installationPath -eq $qaInstallRoot)
    if ($qaCppInstance.Count -ne 1 -or -not $qaCppInstance[0].isComplete) { throw 'The C++ component was not added to the new instance.' }
    $qaEvidence.cppInstance = $qaCppInstance[0]
    $qaEvidence.compilerComponentPresentAfter = $true
    Save-Evidence
    Inspect-InstalledSelection 'p1_windows_cpp_incorporado' $qaCppInstance[0].channelId
    $qaDeveloper = Join-Path $qaInstallRoot 'Common7\Tools\VsDevCmd.bat'
    @'
#include <iostream>
int main() { std::cout << "Entorno preparado\n"; return 0; }
'@ | Set-Content -LiteralPath (Join-Path $qaWorking 'comprobar_entorno.cpp') -Encoding ascii
    Invoke-Capture 'p1_windows_cpp_instalado_comprobado' @'
where cl
cl /W4 /EHsc comprobar_entorno.cpp
@if errorlevel 1 exit /b 14
.\comprobar_entorno.exe
@if errorlevel 1 exit /b 15
echo %ERRORLEVEL%
'@
    $qaActual = & (Join-Path $qaWorking 'comprobar_entorno.exe')
    if ($LASTEXITCODE -ne 0 -or $qaActual -cne 'Entorno preparado') { throw 'The installed toolchain did not build the expected program.' }
    $qaCompilerPathFile = Join-Path $qaWorking 'compiler-path.txt'
    & $env:ComSpec /d /c "call `"$qaDeveloper`" -no_logo -arch=x64 -host_arch=x64 >nul && where cl >`"$qaCompilerPathFile`""
    $qaCompilerPaths = @(Get-Content -LiteralPath $qaCompilerPathFile)
    if (-not $qaCompilerPaths[0].StartsWith($qaInstallRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'The proof used a compiler outside the newly installed instance.'
    }
    $qaEvidence.compilerUsed = $qaCompilerPaths[0]
    $qaEvidence.compilerVersion = (Get-Item -LiteralPath $qaCompilerPaths[0]).VersionInfo.FileVersion
    $qaEvidence.standardOutput = $qaActual
    $qaEvidence.exitCode = 0
    $qaEvidence.completedAtUtc = [DateTime]::UtcNow.ToString('o')
    Save-Evidence
} finally {
    # VS installer diagnostics can dump the process environment, including
    # unrelated runner service passwords. Preserve useful diagnostics without
    # copying any environment values into a downloadable artifact.
    foreach ($qaPattern in @('dd_bootstrapper*.log', 'dd_installer*.log')) {
        foreach ($qaLog in (Get-ChildItem -LiteralPath $env:TEMP -Filter $qaPattern -File -ErrorAction SilentlyContinue)) {
            Get-Content -LiteralPath $qaLog.FullName -ErrorAction SilentlyContinue |
                Where-Object { $_ -notmatch 'Environment variable:' } |
                Set-Content -LiteralPath (Join-Path $qaOutput $qaLog.Name) -Encoding utf8
        }
    }
    Save-Evidence
}
