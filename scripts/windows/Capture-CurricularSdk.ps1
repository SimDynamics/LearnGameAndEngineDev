[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This SDK installation exercise requires a disposable GitHub-hosted Windows runner.'
}
$qaRepository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaOutput = Join-Path $qaRepository 'out\qa\curricular-sdk'
$qaWorking = 'C:\LGAED-CurricularQA\sdk-proof'
New-Item -ItemType Directory -Force -Path $qaOutput, $qaWorking | Out-Null
$qaSdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$qaSdkVersion = '10.0.28000.0'
$qaSdkServicing = [version]'10.0.28000.2957'
$qaSdkInclude = Join-Path $qaSdkRoot "Include\$qaSdkVersion"
$qaSdkBin = Join-Path $qaSdkRoot "bin\$qaSdkVersion"
$qaInitialVersions = @(Get-ChildItem -LiteralPath (Join-Path $qaSdkRoot 'Include') -Directory | ForEach-Object Name)
$qaEvidence = [ordered]@{
    commit = $env:GITHUB_SHA
    startedAtUtc = [DateTime]::UtcNow.ToString('o')
    scope = 'Install an absent SDK version on a disposable Windows Server runner with Visual Studio and an earlier SDK already present. Not a clean Windows 11 installation.'
    operatingSystem = (Get-CimInstance Win32_OperatingSystem).Caption
    initialSdkVersions = $qaInitialVersions
    requestedSdk = $qaSdkVersion
    requestedServicing = $qaSdkServicing.ToString()
    installationRoot = $qaSdkRoot
    screenshots = @()
}
function Save-Evidence {
    $qaEvidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $qaOutput 'provenance.json') -Encoding utf8
}
Save-Evidence
if ((Test-Path -LiteralPath $qaSdkInclude) -or (Test-Path -LiteralPath $qaSdkBin)) {
    throw 'The requested SDK already has files on this image; no absent-SDK installation is claimed.'
}
if ((Get-PSDrive -Name C).Free -lt 8GB) { throw 'Insufficient free space for the SDK installation exercise.' }
$qaVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$qaVsRoot = & $qaVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $qaVsRoot) { throw 'The SDK proof requires an existing MSVC installation.' }
$qaVcVars = Join-Path $qaVsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
$qaEnvironmentSetup = 'call "' + $qaVcVars + '" x64'

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public static class CurricularSdkCapture {
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
    [CurricularSdkCapture]::ShowWindow($qaWindow, 9) | Out-Null
    $qaScreen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if ($qaTargetWidth -gt 0 -and $qaTargetHeight -gt 0) {
        [CurricularSdkCapture]::MoveWindow($qaWindow, $qaScreen.Left, $qaScreen.Top,
            [Math]::Min($qaTargetWidth, $qaScreen.Width), [Math]::Min($qaTargetHeight, $qaScreen.Height), $true) | Out-Null
    } else {
        $qaOriginalRect = New-Object CurricularSdkCapture+Rect
        [CurricularSdkCapture]::GetWindowRect($qaWindow, [ref] $qaOriginalRect) | Out-Null
        [CurricularSdkCapture]::MoveWindow($qaWindow, $qaScreen.Left, $qaScreen.Top,
            $qaOriginalRect.Right - $qaOriginalRect.Left, $qaOriginalRect.Bottom - $qaOriginalRect.Top, $true) | Out-Null
    }
    [CurricularSdkCapture]::SetForegroundWindow($qaWindow) | Out-Null
    Start-Sleep -Milliseconds 700
    $qaRect = New-Object CurricularSdkCapture+Rect
    if (-not [CurricularSdkCapture]::GetWindowRect($qaWindow, [ref] $qaRect)) { throw 'Cannot locate the real console window.' }
    $qaWidth = $qaRect.Right - $qaRect.Left
    $qaHeight = $qaRect.Bottom - $qaRect.Top
    if ($qaWidth -lt 300 -or $qaHeight -lt 200) { throw 'The real console window has invalid capture dimensions.' }
    $qaBitmap = [System.Drawing.Bitmap]::new($qaWidth, $qaHeight)
    $qaGraphics = [System.Drawing.Graphics]::FromImage($qaBitmap)
    try {
        if ([CurricularSdkCapture]::GetForegroundWindow() -ne $qaWindow) {
            throw 'The SDK or command window is not foreground; refusing an unrelated desktop capture.'
        }
        if ($qaRect.Right -gt $qaScreen.Right -or $qaRect.Bottom -gt $qaScreen.Bottom) {
            throw 'The real SDK window does not fit the display; no clipped screenshot is claimed.'
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
$qaEnvironmentSetup
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
    $qaProcess = [CurricularSdkCapture]::StartConsole($env:ComSpec, $qaBatch, $qaWorking, $qaTitle)
    try {
        $qaLimit = [DateTime]::UtcNow.AddSeconds(30)
        while (-not (Test-Path -LiteralPath $qaReady)) {
            if ($qaProcess.HasExited) { throw "The real command window failed with exit $($qaProcess.ExitCode)." }
            if ([DateTime]::UtcNow -gt $qaLimit) { throw 'The command window did not reach its capture point.' }
            Start-Sleep -Milliseconds 100
        }
        $qaWindow = [CurricularSdkCapture]::FindTitledWindow($qaTitle)
        if ($qaWindow -eq [IntPtr]::Zero) { throw 'The native command window is unavailable; no screenshot has been fabricated.' }
        Save-ActualWindow $qaWindow (Join-Path $qaOutput ($qaId + '.png'))
    }
    finally { if (-not $qaProcess.HasExited) { Stop-Process -Id $qaProcess.Id -Force } }
}



function Find-SdkWindow {
    foreach ($qaCaption in @('Windows Software Development Kit', 'Windows SDK')) {
        $qaHandle = [CurricularSdkCapture]::FindTitledWindow($qaCaption)
        if ($qaHandle -ne [IntPtr]::Zero) { return $qaHandle }
    }
    return [IntPtr]::Zero
}
function Read-SdkWindow([IntPtr] $qaWindow, [string] $qaName) {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $qaRoot = [System.Windows.Automation.AutomationElement]::FromHandle($qaWindow)
    $qaNodes = $qaRoot.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                              [System.Windows.Automation.Condition]::TrueCondition)
    $qaText = [System.Collections.Generic.List[string]]::new()
    $qaControls = foreach ($qaNode in $qaNodes) {
        $qaText.Add($qaNode.Current.Name)
        $qaTextPattern = $null
        if ($qaNode.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref] $qaTextPattern)) {
            $qaText.Add($qaTextPattern.DocumentRange.GetText(-1))
        }
        [ordered]@{name = $qaNode.Current.Name; id = $qaNode.Current.AutomationId;
            type = $qaNode.Current.ControlType.ProgrammaticName; offscreen = $qaNode.Current.IsOffscreen}
    }
    $qaControls | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $qaOutput ($qaName + '-controls.json')) -Encoding utf8
    return $qaText -join "`n"
}

function Read-SdkFeatureChoices([System.Windows.Automation.AutomationElement] $qaRoot) {
    $qaTree = $qaRoot.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty,
            'UserOptionSelectionPageTreeView'))
    if ($null -eq $qaTree) { throw 'The observed SDK feature tree is unavailable.' }
    $qaItems = $qaTree.FindAll([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::TreeItem))
    foreach ($qaItem in $qaItems) {
        $qaId = $qaItem.Current.AutomationId
        if ($qaId -notlike 'OptionId.*') { continue }
        $qaCheck = $qaItem.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.PropertyCondition]::new(
                [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
                [System.Windows.Automation.ControlType]::CheckBox))
        $qaToggle = $null
        if ($null -eq $qaCheck -or -not $qaCheck.TryGetCurrentPattern(
            [System.Windows.Automation.TogglePattern]::Pattern, [ref] $qaToggle)) {
            throw "The observed SDK feature $qaId has no readable native checkbox."
        }
        [pscustomobject]@{
            id = $qaId; name = $qaCheck.Current.Name
            enabled = $qaItem.Current.IsEnabled -and $qaCheck.Current.IsEnabled
            state = $qaToggle.Current.ToggleState.ToString(); toggle = $qaToggle
        }
    }
}

function Select-SdkDesktopFeatures([System.Windows.Automation.AutomationElement] $qaRoot) {
    # These IDs were read from this signed installer's actual feature tree.
    # Only enabled optional checkboxes in that observed list may be cleared.
    $qaKnownIds = @(
        'OptionId.WindowsPerformanceToolkit', 'OptionId.WindowsDesktopDebuggers',
        'OptionId.AvrfExternal', 'OptionId.NetFxSoftwareDevelopmentKit',
        'OptionId.WindowsSoftwareLogoToolkit', 'OptionId.IpOverUsb',
        'OptionId.MSIInstallTools', 'OptionId.SigningTools', 'OptionId.UWPManaged',
        'OptionId.UWPCPP', 'OptionId.UWPLocalized', 'OptionId.DesktopCPPx86',
        'OptionId.DesktopCPPx64', 'OptionId.DesktopCPParm64'
    )
    $qaTargetId = 'OptionId.DesktopCPPx64'
    $qaBefore = @(Read-SdkFeatureChoices $qaRoot)
    $qaTarget = @($qaBefore | Where-Object { $_.id -eq $qaTargetId })
    if ($qaTarget.Count -ne 1 -or $qaTarget[0].name -notmatch '(?i)Desktop C\+\+.*(x64|amd64)') {
        throw 'The SDK feature tree did not identify exactly one Desktop C++ x64/amd64 checkbox.'
    }
    $qaEvidence.featureActions = @()
    foreach ($qaId in $qaKnownIds) {
        if ($qaId -eq $qaTargetId) { continue }
        $qaChoice = @(Read-SdkFeatureChoices $qaRoot | Where-Object { $_.id -eq $qaId })
        if ($qaChoice.Count -gt 1) { throw "The SDK feature $qaId is ambiguous." }
        if ($qaChoice.Count -eq 1 -and $qaChoice[0].enabled -and $qaChoice[0].state -eq 'On') {
            $qaChoice[0].toggle.Toggle()
            Start-Sleep -Milliseconds 150
            $qaObserved = @(Read-SdkFeatureChoices $qaRoot)
            $qaEvidence.featureActions += [ordered]@{
                id = $qaId; requested = 'Off'
                observed = ($qaObserved | Where-Object { $_.id -eq $qaId } | Select-Object -First 1).state
                targetState = ($qaObserved | Where-Object { $_.id -eq $qaTargetId } | Select-Object -First 1).state
            }
            Save-Evidence
        }
    }
    # This installer can clear amd64 when another selected checkbox is cleared.
    # Select the requested target last, then record any other boxes the installer
    # activates. Their internal dependency relationship is not inferred here.
    $qaBeforeTarget = @(Read-SdkFeatureChoices $qaRoot)
    foreach ($qaChoice in $qaBeforeTarget) {
        if ($qaChoice.id -ne $qaTargetId -and $qaChoice.enabled -and $qaChoice.state -ne 'Off') {
            throw "The SDK retains an unexpected optional selection before choosing amd64: $($qaChoice.id)."
        }
    }
    $qaTarget = @($qaBeforeTarget | Where-Object { $_.id -eq $qaTargetId })
    if ($qaTarget.Count -ne 1) { throw 'The SDK target checkbox disappeared during selection.' }
    if ($qaTarget[0].state -eq 'Off' -and $qaTarget[0].enabled) {
        $qaTarget[0].toggle.Toggle()
        Start-Sleep -Milliseconds 300
        $qaEvidence.featureActions += [ordered]@{id = $qaTargetId; requested = 'On'}
    }
    $qaAfter = @(Read-SdkFeatureChoices $qaRoot)
    $qaActivatedWithTarget = @($qaAfter | Where-Object {
        $qaRow = $_
        $qaPrior = $qaBeforeTarget | Where-Object { $_.id -eq $qaRow.id } | Select-Object -First 1
        $qaRow.id -ne $qaTargetId -and $qaRow.state -eq 'On' -and $qaPrior.state -eq 'Off'
    })
    $qaEvidence.installerActivatedWithTarget = @($qaActivatedWithTarget | ForEach-Object {
        [ordered]@{id = $_.id; name = $_.name; enabled = $_.enabled; state = $_.state}
    })
    $qaEvidence.featureSelection = @($qaAfter | ForEach-Object {
        $qaRow = $_
        $qaPrevious = $qaBefore | Where-Object { $_.id -eq $qaRow.id } | Select-Object -First 1
        $qaPriorTarget = $qaBeforeTarget | Where-Object { $_.id -eq $qaRow.id } | Select-Object -First 1
        [ordered]@{id = $qaRow.id; name = $qaRow.name; enabled = $qaRow.enabled
            before = $qaPrevious.state; beforeTargetSelection = $qaPriorTarget.state; after = $qaRow.state}
    })
    $qaEvidence.featureSelectionAtUtc = [DateTime]::UtcNow.ToString('o')
    Save-Evidence
    $qaSelectedTarget = @($qaAfter | Where-Object { $_.id -eq $qaTargetId -and $_.state -eq 'On' })
    if ($qaSelectedTarget.Count -ne 1) { throw 'The Desktop C++ amd64 feature is not selected after applying the choices.' }
    foreach ($qaChoice in $qaAfter) {
        if ($qaChoice.id -ne $qaTargetId -and $qaChoice.enabled -and $qaChoice.state -ne 'Off' -and
            $qaChoice.id -notin @($qaActivatedWithTarget | ForEach-Object { $_.id })) {
            throw "The SDK retains an optional selection not activated with the target: $($qaChoice.id)."
        }
    }
}

Invoke-Capture 'p1_windows_sdk_antes' @"
dir /b "$qaSdkRoot\Include"
"@
$qaInstaller = Join-Path $qaWorking 'winsdksetup.exe'
# Linked by Microsoft's SDK downloads page, release 10.0.28000.2957.
$qaUrl = 'https://download.microsoft.com/download/e6cfffdc-0d3e-4c6c-96d7-d491da459a07/KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/winsdksetup.exe'
Invoke-WebRequest -Uri $qaUrl -OutFile $qaInstaller
$qaSignature = Get-AuthenticodeSignature -LiteralPath $qaInstaller
if ($qaSignature.Status -ne 'Valid' -or $qaSignature.SignerCertificate.Subject -notmatch '(^|,\s*)(CN|O)=Microsoft Corporation(,|$)') {
    throw 'The SDK installer lacks a valid Microsoft signature.'
}
$qaEvidence.installer = [ordered]@{
    url = $qaUrl
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaInstaller).Hash
    signer = $qaSignature.SignerCertificate.Subject
    productVersion = (Get-Item -LiteralPath $qaInstaller).VersionInfo.ProductVersion
}
Save-Evidence

# Read the signed installer's own help. This SDK supports a visible wizard and
# /quiet, but its observed help does not document /passive.
$qaHelp = Start-Process -FilePath $qaInstaller -ArgumentList '/?' -PassThru
try {
    $qaLimit = [DateTime]::UtcNow.AddSeconds(45)
    $qaWindow = [IntPtr]::Zero
    while ($qaWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $qaLimit) {
        Start-Sleep -Milliseconds 200
        $qaWindow = Find-SdkWindow
    }
    if ($qaWindow -eq [IntPtr]::Zero) { throw 'The SDK installer help window was not observed.' }
    Start-Sleep -Milliseconds 700
    Save-ActualWindow $qaWindow (Join-Path $qaOutput 'p1_windows_sdk_ayuda.png') 0 0
    $qaHelpText = Read-SdkWindow $qaWindow 'p1_windows_sdk_ayuda'
    $qaHelpText | Set-Content -LiteralPath (Join-Path $qaOutput 'sdk-help.txt') -Encoding utf8
    [CurricularSdkCapture]::PostMessage($qaWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    if ($qaHelpText -notmatch '/features' -or $qaHelpText -notmatch '/norestart' -or $qaHelpText -notmatch '/ceip') {
        throw 'The observed SDK help did not confirm the requested documented options.'
    }
    if (-not $qaHelp.WaitForExit(10000)) { throw 'The SDK help process did not close.' }
} finally { $qaHelp.Dispose() }

$qaArguments = @('/norestart', '/ceip', 'off', '/features', 'OptionId.DesktopCPPx64')
$qaEvidence.arguments = $qaArguments
$qaEvidence.captureMethod = 'CopyFromScreen of the foreground installer or command window'
$qaEvidence.uiActions = @()
$qaEvidence.installStartedAtUtc = [DateTime]::UtcNow.ToString('o')
Save-Evidence
$qaInstall = Start-Process -FilePath $qaInstaller -WorkingDirectory $qaWorking -ArgumentList $qaArguments -PassThru
try {
    $qaLimit = [DateTime]::UtcNow.AddMinutes(14)
    $qaNextCapture = [DateTime]::UtcNow.AddSeconds(2)
    $qaCaptureCount = 0
    $qaActionCount = 0
    $qaLastActionSignature = ''
    $qaLastRecognized = [DateTime]::UtcNow
    while (-not $qaInstall.HasExited) {
        if ([DateTime]::UtcNow -gt $qaLimit) { throw 'The SDK installation exceeded its bounded test duration.' }
        $qaWindow = Find-SdkWindow
        if ($qaWindow -ne [IntPtr]::Zero) {
            $qaRoot = [System.Windows.Automation.AutomationElement]::FromHandle($qaWindow)
            $qaNodes = $qaRoot.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                                      [System.Windows.Automation.Condition]::TrueCondition)
            $qaVisible = @($qaNodes | Where-Object { -not $_.Current.IsOffscreen })
            $qaNames = @($qaVisible | ForEach-Object { $_.Current.Name })
            $qaVisibleText = $qaNames -join "`n"
            $qaButtons = @($qaVisible | Where-Object {
                $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and $_.Current.IsEnabled
            })
            $qaButtonOptions = foreach ($qaCandidate in $qaButtons) {
                $qaLabels = @($qaCandidate.Current.Name.Replace('_', '').Trim())
                # The observed privacy-page button is named "Next Page" in
                # UI Automation while its actual child caption reads "Next".
                # Read that caption from the same native button instead of
                # assuming all pages use identical accessibility names.
                $qaLabelNodes = $qaCandidate.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                    [System.Windows.Automation.PropertyCondition]::new(
                        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
                        [System.Windows.Automation.ControlType]::Text))
                foreach ($qaLabelNode in $qaLabelNodes) {
                    $qaLabels += $qaLabelNode.Current.Name.Replace('_', '').Trim()
                }
                [pscustomobject]@{element = $qaCandidate; labels = $qaLabels}
            }
            $qaButton = $null
            foreach ($qaWanted in @('Accept', 'Next', 'Next >', 'Install', 'Finish', 'Close')) {
                $qaOption = $qaButtonOptions | Where-Object { $qaWanted -in $_.labels } | Select-Object -First 1
                if ($null -ne $qaOption) {
                    $qaButton = $qaOption.element
                    $qaButtonName = $qaWanted
                    break
                }
            }
            $qaProgress = @($qaVisible | Where-Object {
                $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ProgressBar
            }).Count -ne 0
            if ($qaProgress) { $qaLastRecognized = [DateTime]::UtcNow }
            if ($null -ne $qaButton) {
                # The observed terminal page says "Welcome". Its title and
                # Close control identify the final page, not installation success.
                # The process exit code and every file/build gate remain required.
                $qaTerminalTitle = @($qaVisible | Where-Object {
                    $_.Current.AutomationId -eq 'FinishedPageTitle' -and
                    $_.Current.Name -like 'Welcome to the Windows Software Development Kit*'
                }).Count -eq 1
                $qaTerminalControl = $qaTerminalTitle -and $qaButton.Current.AutomationId -eq 'FinishedPageClose'
                if ($qaButtonName -in @('Close', 'Finish') -and -not $qaTerminalControl -and
                    $qaVisibleText -notmatch '(?i)complete|successfully installed|installation was successful') {
                    $qaButton = $null
                }
            }
            if ($null -ne $qaButton) {
                $qaActionSignature = $qaButtonName + "`n" + $qaVisibleText
                if ($qaActionSignature -ne $qaLastActionSignature) {
                    if ($qaActionCount -ge 8) { throw 'The SDK wizard exceeded its bounded known sequence.' }
                    $qaName = 'p1_windows_sdk_paso_{0:d2}' -f ($qaActionCount + 1)
                    if ($qaButtonName -eq 'Install') {
                        Select-SdkDesktopFeatures $qaRoot
                    }
                    # Capture the verified choices, after changing the real controls.
                    Save-ActualWindow $qaWindow (Join-Path $qaOutput ($qaName + '.png')) 0 0
                    Read-SdkWindow $qaWindow $qaName | Out-Null
                    $qaInvoke = $null
                    if (-not $qaButton.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref] $qaInvoke)) {
                        throw 'The observed SDK button does not expose its native invocation action.'
                    }
                    $qaEvidence.uiActions += [ordered]@{button = $qaButtonName; screenshot = $qaName + '.png'; atUtc = [DateTime]::UtcNow.ToString('o')}
                    Save-Evidence
                    Write-Host "SDK wizard action: $qaButtonName ($($qaButton.Current.AutomationId))"
                    $qaInvoke.Invoke()
                    $qaLastActionSignature = $qaActionSignature
                    $qaActionCount++
                    $qaLastRecognized = [DateTime]::UtcNow
                    Start-Sleep -Milliseconds 500
                }
            }
            if ($qaCaptureCount -lt 12 -and [DateTime]::UtcNow -gt $qaNextCapture) {
                $qaName = 'p1_windows_sdk_instalacion_{0:d2}' -f ($qaCaptureCount + 1)
                try {
                    Save-ActualWindow $qaWindow (Join-Path $qaOutput ($qaName + '.png')) 0 0
                    $qaEvidence.screenshots += ($qaName + '.png')
                    $qaCaptureCount++
                    Save-Evidence
                } catch {
                    # A window may disappear as an installation phase finishes.
                    # No placeholder image is produced for that transition.
                    $qaEvidence.lastCaptureDiagnostic = $_.Exception.Message
                    Save-Evidence
                }
                $qaNextCapture = [DateTime]::UtcNow.AddSeconds(15)
            }
            if ([DateTime]::UtcNow -gt $qaLastRecognized.AddSeconds(45)) {
                Read-SdkWindow $qaWindow 'sdk-unrecognized-state' | Out-Null
                throw 'The SDK wizard has no recognized continuation or progress; inspect its actual controls.'
            }
        }
        Start-Sleep -Milliseconds 250
        $qaInstall.Refresh()
    }
    $qaEvidence.installerExitCode = $qaInstall.ExitCode
    Save-Evidence
    if ($qaInstall.ExitCode -ne 0) { throw "SDK installer returned $($qaInstall.ExitCode); a restart or failure is not counted as a ready environment." }
    if ($qaCaptureCount -eq 0) { throw 'No real SDK installation window was captured.' }
} finally { $qaInstall.Dispose(); Save-Evidence }

$qaRequiredFiles = @(
    (Join-Path $qaSdkInclude 'um\Windows.h'),
    (Join-Path $qaSdkInclude 'ucrt\stdio.h'),
    (Join-Path $qaSdkRoot "Lib\$qaSdkVersion\um\x64\kernel32.lib"),
    (Join-Path $qaSdkRoot "Lib\$qaSdkVersion\ucrt\x64\ucrt.lib"),
    (Join-Path $qaSdkBin 'x64\rc.exe'),
    (Join-Path $qaSdkBin 'x64\mt.exe')
)
foreach ($qaFile in $qaRequiredFiles) {
    if (-not (Test-Path -LiteralPath $qaFile -PathType Leaf)) { throw "The SDK lacks a required file: $qaFile" }
}
$qaEvidence.filesVerified = $qaRequiredFiles
$qaToolVersions = foreach ($qaTool in @('rc.exe', 'mt.exe')) {
    $qaToolPath = Join-Path $qaSdkBin ('x64\' + $qaTool)
    $qaToolVersion = (Get-Item -LiteralPath $qaToolPath).VersionInfo.FileVersionRaw
    if ($qaToolVersion -lt $qaSdkServicing) { throw 'The SDK servicing level is older than the selected release.' }
    [ordered]@{path = $qaToolPath; version = $qaToolVersion.ToString()}
}
$qaEvidence.toolVersions = $qaToolVersions
$qaFinalVersions = @(Get-ChildItem -LiteralPath (Join-Path $qaSdkRoot 'Include') -Directory | ForEach-Object Name)
foreach ($qaInitial in $qaInitialVersions) {
    if ($qaInitial -notin $qaFinalVersions) { throw 'A previously installed SDK directory is no longer present.' }
}
$qaEvidence.finalSdkVersions = $qaFinalVersions
$qaEnvironmentSetup = 'call "' + $qaVcVars + '" x64 ' + $qaSdkVersion
@'
#include <iostream>
int main() { std::cout << "Entorno preparado\n"; return 0; }
'@ | Set-Content -LiteralPath (Join-Path $qaWorking 'comprobar_entorno.cpp') -Encoding ascii
Invoke-Capture 'p1_windows_sdk_comprobado' @'
echo %WindowsSDKVersion%
where rc
cl /W4 /EHsc comprobar_entorno.cpp
@if errorlevel 1 exit /b 14
.\comprobar_entorno.exe
@if errorlevel 1 exit /b 15
echo %ERRORLEVEL%
'@
$qaEnvironmentFile = Join-Path $qaOutput 'selected-sdk.txt'
& $env:ComSpec /d /c "call `"$qaVcVars`" x64 $qaSdkVersion >nul && set WindowsSDKVersion >`"$qaEnvironmentFile`""
$qaSelected = (Get-Content -LiteralPath $qaEnvironmentFile -Raw).Trim()
if ($qaSelected -ne ('WindowsSDKVersion=' + $qaSdkVersion + '\')) { throw 'The compiler environment did not select the newly installed SDK.' }
$qaActual = & (Join-Path $qaWorking 'comprobar_entorno.exe')
if ($LASTEXITCODE -ne 0 -or $qaActual -cne 'Entorno preparado') { throw 'The program did not execute correctly with the newly selected SDK.' }
$qaEvidence.selectedSdk = $qaSelected
$qaEvidence.standardOutput = $qaActual
$qaEvidence.exitCode = 0
$qaEvidence.completedAtUtc = [DateTime]::UtcNow.ToString('o')
Save-Evidence
# Raw installer logs can dump environment variables; only structured evidence,
# the program source and real screenshots are copied to the artifact.
Copy-Item -LiteralPath (Join-Path $qaWorking 'comprobar_entorno.cpp') -Destination $qaOutput
