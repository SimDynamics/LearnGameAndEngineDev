[CmdletBinding()]
param(
    [ValidateSet('Probe', 'Curriculum')]
    [string] $Mode = 'Probe'
)

$ErrorActionPreference = 'Stop'
# Real, isolated Windows/Visual Studio execution. The probe checks the debugger
# infrastructure only; it is not an InspectionLog reference or a lesson image.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Run only on a disposable GitHub-hosted Windows runner.'
}
if ([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA') {
    throw 'Launch this script with Windows PowerShell -STA for Visual Studio automation.'
}
$qaRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaOutput = Join-Path $qaRepository 'out\qa\inspection-log'
$qaWorking = Join-Path 'C:\LGAED-CurricularQA' ('inspection-' + $Mode.ToLowerInvariant())
New-Item -ItemType Directory -Force -Path $qaOutput, $qaWorking | Out-Null
Start-Transcript -LiteralPath (Join-Path $qaOutput 'capture-transcript.txt') -Force | Out-Null
$qaStarted = [DateTime]::UtcNow
$qaDeadline = $qaStarted.AddMinutes(9)
$qaEvents = [Collections.Generic.List[object]]::new()
$qaCaptures = [Collections.Generic.List[object]]::new()
$qaDte = $null
$qaVsProcess = $null
$qaState = [ordered]@{
    status = 'running'; mode = $Mode; commit = $env:GITHUB_SHA
    startedAtUtc = $qaStarted.ToString('o')
    environment = 'Disposable GitHub-hosted Windows runner; Visual Studio and MSVC already installed.'
    firstTimeInstallationTested = $false
    curricularReference = ($Mode -eq 'Curriculum')
    events = $qaEvents; captures = $qaCaptures
}

function Write-Provenance {
    $qaState | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $qaOutput 'provenance.json') -Encoding UTF8
}
function Write-Stage([string] $Name, [object] $Detail) {
    $qaEvents.Add([ordered]@{ atUtc = [DateTime]::UtcNow.ToString('o'); stage = $Name; detail = $Detail })
    Write-Host $Name
    Write-Provenance
}
function Assert-Time {
    if ([DateTime]::UtcNow -gt $qaDeadline) { throw 'The bounded debugger capture exceeded nine minutes.' }
}
function Wait-Until([string] $Description, [scriptblock] $Condition, [int] $Seconds = 60) {
    $qaLimit = [DateTime]::UtcNow.AddSeconds($Seconds)
    $qaLastError = ''
    while ([DateTime]::UtcNow -lt $qaLimit) {
        Assert-Time
        try { if (& $Condition) { return } } catch { $qaLastError = $_.Exception.Message }
        Start-Sleep -Milliseconds 250
    }
    throw ("Timed out: " + $Description + '. ' + $qaLastError)
}
function Invoke-RecordedProcess([string] $Executable, [string] $Arguments, [string] $Name, [int] $Seconds = 90) {
    Assert-Time
    Write-Stage $Name ([ordered]@{ executable = $Executable; arguments = $Arguments; workingDirectory = $qaWorking })
    $qaProcessOptions = @{
        FilePath = $Executable; WorkingDirectory = $qaWorking; PassThru = $true
        RedirectStandardOutput = Join-Path $qaOutput ($Name + '.stdout.txt')
        RedirectStandardError = Join-Path $qaOutput ($Name + '.stderr.txt')
    }
    if ($Arguments) { $qaProcessOptions.ArgumentList = $Arguments }
    $qaProcess = Start-Process @qaProcessOptions
    try {
        $null = $qaProcess.Handle
        if (-not $qaProcess.WaitForExit($Seconds * 1000)) { throw ($Name + ' did not finish within its time limit.') }
        $qaProcess.Refresh()
        $qaExit = $qaProcess.ExitCode
        Write-Stage ($Name + '-exit') $qaExit
        if ($qaExit -ne 0) { throw ($Name + ' failed with exit code ' + $qaExit + '. See the preserved stdout/stderr.') }
    }
    finally {
        if (-not $qaProcess.HasExited) {
            & (Join-Path $env:WINDIR 'System32\taskkill.exe') /PID $qaProcess.Id /T /F | Out-Null
        }
        $qaProcess.Dispose()
    }
}

try {
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

public static class InspectionNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hwnd, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    delegate bool WindowVisitor(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowVisitor visitor, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder title, int length);
    [DllImport("ole32.dll")] static extern int CreateBindCtx(uint reserved, out IBindCtx context);

    public static string[] WindowsForProcess(int processId) {
        var names = new List<string>();
        EnumWindows((hwnd, unused) => {
            uint owner; GetWindowThreadProcessId(hwnd, out owner);
            if (owner == processId && IsWindowVisible(hwnd)) {
                var title = new StringBuilder(2048);
                GetWindowText(hwnd, title, title.Capacity);
                names.Add(hwnd.ToInt64().ToString() + ": " + title);
            }
            return true;
        }, IntPtr.Zero);
        return names.ToArray();
    }
    public static IntPtr FindWindowForProcess(int processId) {
        IntPtr result = IntPtr.Zero;
        long largest = 0;
        EnumWindows((hwnd, unused) => {
            uint owner; GetWindowThreadProcessId(hwnd, out owner);
            Rect rect;
            if (owner == processId && IsWindowVisible(hwnd) && GetWindowRect(hwnd, out rect)) {
                long area = (long)(rect.Right - rect.Left) * (rect.Bottom - rect.Top);
                if (area > largest) { largest = area; result = hwnd; }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static object DteForProcess(int processId) {
        IBindCtx context = null;
        IRunningObjectTable table = null;
        IEnumMoniker enumerator = null;
        try {
            Marshal.ThrowExceptionForHR(CreateBindCtx(0, out context));
            context.GetRunningObjectTable(out table);
            table.EnumRunning(out enumerator);
            var monikers = new IMoniker[1];
            while (enumerator.Next(1, monikers, IntPtr.Zero) == 0) {
                try {
                    string name;
                    monikers[0].GetDisplayName(context, null, out name);
                    if (name.Contains("VisualStudio.DTE.") && name.EndsWith(":" + processId, StringComparison.Ordinal)) {
                        object dte; table.GetObject(monikers[0], out dte);
                        return dte;
                    }
                } catch (COMException) { }
                finally { if (monikers[0] != null) Marshal.ReleaseComObject(monikers[0]); }
            }
            return null;
        } finally {
            if (enumerator != null) Marshal.ReleaseComObject(enumerator);
            if (table != null) Marshal.ReleaseComObject(table);
            if (context != null) Marshal.ReleaseComObject(context);
        }
    }
}
[ComImport, Guid("00000016-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IInspectionMessageFilter {
    [PreserveSig] int HandleInComingCall(int type, IntPtr task, int tick, IntPtr info);
    [PreserveSig] int RetryRejectedCall(IntPtr task, int tick, int reject);
    [PreserveSig] int MessagePending(IntPtr task, int tick, int pending);
}
public sealed class InspectionMessageFilter : IInspectionMessageFilter {
    [DllImport("ole32.dll")] static extern int CoRegisterMessageFilter(IInspectionMessageFilter filter, out IInspectionMessageFilter previous);
    static IInspectionMessageFilter previous;
    static InspectionMessageFilter current;
    public static void Register() {
        current = new InspectionMessageFilter();
        Marshal.ThrowExceptionForHR(CoRegisterMessageFilter(current, out previous));
    }
    public static void Revoke() {
        IInspectionMessageFilter ignored;
        CoRegisterMessageFilter(previous, out ignored);
        current = null; previous = null;
    }
    int IInspectionMessageFilter.HandleInComingCall(int type, IntPtr task, int tick, IntPtr info) { return 0; }
    int IInspectionMessageFilter.RetryRejectedCall(IntPtr task, int tick, int reject) {
        return reject == 2 && tick < 10000 ? 100 : -1;
    }
    int IInspectionMessageFilter.MessagePending(IntPtr task, int tick, int pending) { return 2; }
}
'@
    [InspectionMessageFilter]::Register()

    function Save-ActualVsWindow([string] $Name) {
        $qaWindow = [InspectionNative]::FindWindowForProcess($qaVsProcess.Id)
        if ($qaWindow -eq [IntPtr]::Zero) { throw 'The launched Visual Studio has no visible native window.' }
        $qaScreen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
        [InspectionNative]::ShowWindow($qaWindow, 9) | Out-Null
        [InspectionNative]::MoveWindow($qaWindow, $qaScreen.Left, $qaScreen.Top,
            [Math]::Min(1440, $qaScreen.Width), [Math]::Min(1000, $qaScreen.Height), $true) | Out-Null
        [InspectionNative]::SetForegroundWindow($qaWindow) | Out-Null
        Start-Sleep -Milliseconds 700
        $qaRect = New-Object InspectionNative+Rect
        if (-not [InspectionNative]::GetWindowRect($qaWindow, [ref] $qaRect)) { throw 'Cannot read the Visual Studio window bounds.' }
        $qaWidth = $qaRect.Right - $qaRect.Left
        $qaHeight = $qaRect.Bottom - $qaRect.Top
        if ($qaWidth -lt 700 -or $qaHeight -lt 500) { throw 'The Visual Studio window is too small for a legible capture.' }
        $qaPath = Join-Path $qaOutput ($Name + '.png')
        $qaBitmap = [Drawing.Bitmap]::new($qaWidth, $qaHeight)
        $qaGraphics = [Drawing.Graphics]::FromImage($qaBitmap)
        try {
            # Real displayed pixels, only from this newly launched IDE. The
            # disposable runner contains no user desktop or personal documents.
            if ([InspectionNative]::GetForegroundWindow() -ne $qaWindow) {
                throw 'Visual Studio is not foreground; refusing an unrelated desktop capture.'
            }
            $qaGraphics.CopyFromScreen($qaRect.Left, $qaRect.Top, 0, 0, $qaBitmap.Size)
            $qaBitmap.Save($qaPath, [Drawing.Imaging.ImageFormat]::Png)
        } finally { $qaGraphics.Dispose(); $qaBitmap.Dispose() }
        $qaCaptures.Add([ordered]@{
            file = $Name + '.png'; method = 'CopyFromScreen of foreground Visual Studio window'
            width = $qaWidth; height = $qaHeight
            processId = $qaVsProcess.Id; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaPath).Hash
        })
        Write-Provenance
    }
    function Read-DebugExpression([string] $Expression, [string] $Expected) {
        $qaValue = $qaDte.Debugger.GetExpression($Expression, $false, 5000)
        $qaResult = [ordered]@{
            expression = $Expression; valid = [bool] $qaValue.IsValidValue
            type = [string] $qaValue.Type; value = [string] $qaValue.Value; expected = $Expected
        }
        Write-Stage 'debug-expression' $qaResult
        if (-not $qaResult.valid -or $qaResult.value -cne $Expected) {
            throw ('Unexpected real debugger value for ' + $Expression + ': ' + $qaResult.value)
        }
        return $qaResult
    }
    function Complete-OptionalFirstLaunch {
        # The first captured runner showed this optional account screen before
        # DTE was registered. Invoke only the named, visible offline controls;
        # never enter credentials or infer a successful debugger connection.
        $qaWindow = [InspectionNative]::FindWindowForProcess($qaVsProcess.Id)
        if ($qaWindow -eq [IntPtr]::Zero) { return }
        $qaRoot = [Windows.Automation.AutomationElement]::FromHandle($qaWindow)
        foreach ($qaAction in @(
            @{ name = 'Skip and add accounts later.'; image = 'visual_studio_optional_account' },
            @{ name = 'Start Visual Studio'; image = 'visual_studio_first_settings' }
        )) {
            $qaCondition = [Windows.Automation.PropertyCondition]::new(
                [Windows.Automation.AutomationElement]::NameProperty, $qaAction.name)
            $qaControl = $qaRoot.FindFirst([Windows.Automation.TreeScope]::Descendants, $qaCondition)
            if (-not $qaControl -or -not $qaControl.Current.IsEnabled -or $qaControl.Current.IsOffscreen) { continue }
            $qaPattern = $null
            if (-not $qaControl.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref] $qaPattern)) {
                throw ('The visible first-use control has no Invoke pattern: ' + $qaAction.name)
            }
            Save-ActualVsWindow $qaAction.image
            Write-Stage 'first-use-control' ([ordered]@{
                name = $qaControl.Current.Name
                automationId = $qaControl.Current.AutomationId
                action = 'Invoke visible optional account/setup control; no account used.'
            })
            ([Windows.Automation.InvokePattern] $qaPattern).Invoke()
            return
        }
    }

    Write-Stage 'environment' ([ordered]@{
        os = (Get-CimInstance Win32_OperatingSystem).Caption
        powershell = $PSVersionTable.PSVersion.ToString()
        sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
        userInteractive = [Environment]::UserInteractive
    })
    $qaVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $qaVswhere)) { throw 'vswhere.exe is unavailable.' }
    $qaInstances = & $qaVswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -version '[18.0,19.0)' -format json -utf8 | ConvertFrom-Json
    $qaInstance = @($qaInstances | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.installationPath 'Common7\IDE\devenv.exe')
    }) | Select-Object -First 1
    if (-not $qaInstance) { throw 'Visual Studio 2026 IDE with C++ tools is unavailable; Build Tools alone cannot provide the editor/debugger capture.' }
    $qaDeveloper = Join-Path $qaInstance.installationPath 'Common7\Tools\VsDevCmd.bat'
    $qaDevenv = Join-Path $qaInstance.installationPath 'Common7\IDE\devenv.exe'
    $qaState['visualStudio'] = [ordered]@{
        version = $qaInstance.installationVersion; product = $qaInstance.productId
        displayName = $qaInstance.displayName; executable = $qaDevenv
    }
    Write-Stage 'visual-studio-located' $qaState.visualStudio

    if ($Mode -eq 'Curriculum') {
        throw 'Curricular source mapping is not yet available in this recipe. Probe output must not be used as an InspectionLog lesson.'
    }
    $qaSource = @'
#include <iostream>

int main()
{
    const int aprobadas = 3;
    const int rechazadas = 2;
    int total = aprobadas;
    total += rechazadas;
    std::cout << "Total: " << total << '\n';
    return total == 5 ? 0 : 1;
}
'@
    $qaSourceName = 'debugger_probe.cpp'
    $qaSourcePath = Join-Path $qaWorking $qaSourceName
    [IO.File]::WriteAllText($qaSourcePath, $qaSource + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $qaSourcePath -Destination $qaOutput
    $qaSourceLines = [IO.File]::ReadAllLines($qaSourcePath)
    $qaBreakpointLine = [Array]::IndexOf($qaSourceLines, '    total += rechazadas;') + 1
    if ($qaBreakpointLine -lt 1) { throw 'The actual source contains no unique breakpoint location.' }
    $qaExecutable = Join-Path $qaWorking 'debugger_probe.exe'
    $qaBuild = Join-Path $qaWorking 'compile.cmd'
    @"
@echo off
call "$qaDeveloper" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 21
cd /d "$qaWorking"
cl /W4 /EHsc /Zi /Od /Fe:debugger_probe.exe debugger_probe.cpp
exit /b %ERRORLEVEL%
"@ | Set-Content -LiteralPath $qaBuild -Encoding ASCII
    Copy-Item -LiteralPath $qaBuild -Destination $qaOutput
    Invoke-RecordedProcess $env:ComSpec ('/d /c "' + $qaBuild + '"') 'compile'
    if (-not (Test-Path -LiteralPath $qaExecutable)) { throw 'Compilation produced no executable.' }
    Invoke-RecordedProcess $qaExecutable '' 'normal-execution' 15
    $qaNormalOutput = [IO.File]::ReadAllText((Join-Path $qaOutput 'normal-execution.stdout.txt')).TrimEnd()
    if ($qaNormalOutput -cne 'Total: 5') { throw 'The real probe executable produced an unexpected result.' }
    $qaState['source'] = [ordered]@{
        path = $qaSourceName; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaSourcePath).Hash
        breakpointLine = $qaBreakpointLine
        executableSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaExecutable).Hash
    }

    $qaActivity = Join-Path $qaOutput 'visual-studio-activity.xml'
    $qaVsArgs = '/NoSplash /Log "' + $qaActivity + '" /DebugExe "' + $qaExecutable + '"'
    Write-Stage 'launch-debugexe' ([ordered]@{ executable = $qaDevenv; arguments = $qaVsArgs })
    $qaVsProcess = Start-Process -FilePath $qaDevenv -ArgumentList $qaVsArgs -WorkingDirectory $qaWorking -PassThru
    Wait-Until 'DTE registration for the launched Visual Studio process' {
        if ($qaVsProcess.HasExited) { throw 'The launched Visual Studio process exited.' }
        $script:qaDte = [InspectionNative]::DteForProcess($qaVsProcess.Id)
        if (-not $qaDte) { Complete-OptionalFirstLaunch }
        return $null -ne $qaDte
    } 180
    $qaDte.SuppressUI = $true
    $qaDte.MainWindow.Visible = $true
    Write-Stage 'dte-connected' ([ordered]@{ version = [string] $qaDte.Version; processId = $qaVsProcess.Id })
    Wait-Until 'Visual Studio design mode' { [int] $qaDte.Debugger.CurrentMode -eq 1 } 60
    $qaDocumentWindow = $qaDte.ItemOperations.OpenFile($qaSourcePath, '{00000000-0000-0000-0000-000000000000}')
    $qaDocumentWindow.Activate()
    $qaDte.ActiveDocument.Selection.GotoLine($qaBreakpointLine, $true)
    # Explicit unconditional file breakpoint, with no hit count.
    $qaBreakpoints = $qaDte.Debugger.Breakpoints.Add('', $qaSourcePath, $qaBreakpointLine, 1, '', 1, 'C++', '', 1, '', 0, 1)
    if ($qaBreakpoints.Count -lt 1) { throw 'Visual Studio created no breakpoint.' }
    Write-Stage 'breakpoint-created' ([ordered]@{ file = $qaSourcePath; line = $qaBreakpointLine; count = $qaBreakpoints.Count })
    Save-ActualVsWindow 'probe_breakpoint_configured'

    $qaDte.Debugger.Go($false)
    Wait-Until 'actual native breakpoint' { [int] $qaDte.Debugger.CurrentMode -eq 2 } 90
    Write-Stage 'breakpoint-reached' ([ordered]@{ function = [string] $qaDte.Debugger.CurrentStackFrame.FunctionName })
    $qaBefore = Read-DebugExpression 'total' '3'
    Read-DebugExpression 'aprobadas' '3' | Out-Null
    Read-DebugExpression 'rechazadas' '2' | Out-Null
    $qaDte.ExecuteCommand('Debug.Locals', '')
    Start-Sleep -Milliseconds 400
    Save-ActualVsWindow 'probe_breakpoint_locals'
    $qaDte.Debugger.StepOver($false)
    Start-Sleep -Milliseconds 250
    Wait-Until 'native Step Over with total equal to five' {
        if ([int] $qaDte.Debugger.CurrentMode -ne 2) { return $false }
        $qaExpression = $qaDte.Debugger.GetExpression('total', $false, 1000)
        return $qaExpression.IsValidValue -and [string] $qaExpression.Value -ceq '5'
    } 30
    $qaAfter = Read-DebugExpression 'total' '5'
    Save-ActualVsWindow 'probe_step_over_locals'
    $qaDte.Debugger.Go($false)
    Wait-Until 'debugged process termination' { [int] $qaDte.Debugger.CurrentMode -eq 1 } 45
    $qaState['observed'] = [ordered]@{ beforeStep = $qaBefore; afterStep = $qaAfter; normalOutput = $qaNormalOutput }
    $qaState.status = 'passed'
    Write-Stage 'completed' 'Actual MSVC execution, native breakpoint, expression inspection and Step Over completed.'
}
catch {
    $qaState.status = 'failed'
    $qaState['error'] = $_.Exception.ToString()
    $qaState['scriptStack'] = $_.ScriptStackTrace
    if ($qaVsProcess -and -not $qaVsProcess.HasExited -and ('InspectionNative' -as [type])) {
        $qaState['visibleWindows'] = [InspectionNative]::WindowsForProcess($qaVsProcess.Id)
        try { Save-ActualVsWindow 'failure_visual_studio' }
        catch { $qaState['failureCaptureError'] = $_.Exception.Message }
    }
    Write-Provenance
    throw
}
finally {
    if ($qaDte) {
        try { if ([int] $qaDte.Debugger.CurrentMode -ne 1) { $qaDte.Debugger.Stop($false) } } catch {}
        try { $qaDte.Solution.Close($false); $qaDte.Quit() } catch {}
    }
    if ($qaVsProcess -and -not $qaVsProcess.HasExited) {
        & (Join-Path $env:WINDIR 'System32\taskkill.exe') /PID $qaVsProcess.Id /T /F | Out-Null
    }
    if ('InspectionMessageFilter' -as [type]) { [InspectionMessageFilter]::Revoke() }
    $qaState['finishedAtUtc'] = [DateTime]::UtcNow.ToString('o')
    Write-Provenance
    Stop-Transcript | Out-Null
}
