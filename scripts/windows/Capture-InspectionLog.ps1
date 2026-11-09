[CmdletBinding()]
param(
    [ValidateSet('Probe', 'Curriculum')]
    [string] $Mode = 'Curriculum'
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
function Invoke-RecordedProcess(
    [string] $Executable, [string] $Arguments, [string] $Name, [int] $Seconds = 90,
    [int] $ExpectedExit = 0, [string] $StandardInput = ''
) {
    Assert-Time
    Write-Stage $Name ([ordered]@{ executable = $Executable; arguments = $Arguments; workingDirectory = $qaWorking })
    $qaProcessOptions = @{
        FilePath = $Executable; WorkingDirectory = $qaWorking; PassThru = $true
        RedirectStandardOutput = Join-Path $qaOutput ($Name + '.stdout.txt')
        RedirectStandardError = Join-Path $qaOutput ($Name + '.stderr.txt')
    }
    if ($Arguments) { $qaProcessOptions.ArgumentList = $Arguments }
    if ($StandardInput) { $qaProcessOptions.RedirectStandardInput = $StandardInput }
    $qaProcess = Start-Process @qaProcessOptions
    try {
        $null = $qaProcess.Handle
        if (-not $qaProcess.WaitForExit($Seconds * 1000)) { throw ($Name + ' did not finish within its time limit.') }
        $qaProcess.Refresh()
        $qaExit = $qaProcess.ExitCode
        Write-Stage ($Name + '-exit') $qaExit
        if ($qaExit -ne $ExpectedExit) {
            throw ($Name + ' returned ' + $qaExit + '; expected ' + $ExpectedExit + '. See the preserved stdout/stderr.')
        }
    }
    finally {
        if (-not $qaProcess.HasExited) {
            & (Join-Path $env:WINDIR 'System32\taskkill.exe') /PID $qaProcess.Id /T /F | Out-Null
        }
        $qaProcess.Dispose()
    }
}

function Get-CurricularBlock([string] $Lesson, [string] $Owner, [string] $Field) {
    $qaRelative = 'app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/' + $Lesson + '.json'
    $qaLessonPath = Join-Path $qaRepository $qaRelative
    $qaLesson = [IO.File]::ReadAllText($qaLessonPath) | ConvertFrom-Json
    $qaMatches = @()
    for ($qaPage = 0; $qaPage -lt $qaLesson.stateLayout.current.Count; ++$qaPage) {
        $qaOwners = $qaLesson.stateLayout.current[$qaPage].blocks
        for ($qaBlock = 0; $qaBlock -lt $qaOwners.Count; ++$qaBlock) {
            if ($qaOwners[$qaBlock] -ceq $Owner) {
                $qaValue = $qaLesson.pages[$qaPage].blocks[$qaBlock].$Field
                if ($qaValue -isnot [string] -or [string]::IsNullOrWhiteSpace($qaValue)) {
                    throw ('Missing curricular field: ' + $Lesson + '/' + $Owner + '/' + $Field)
                }
                $qaMatches += [ordered]@{
                    lesson = $qaRelative; lessonSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaLessonPath).Hash
                    owner = $Owner; field = $Field; page = $qaPage; block = $qaBlock; value = $qaValue
                }
            }
        }
    }
    if ($qaMatches.Count -ne 1) { throw ('Expected one stable source owner: ' + $Lesson + '/' + $Owner) }
    return $qaMatches[0]
}
function Export-CurricularSource([string] $Lesson, [string] $Owner, [string] $Field, [string] $Destination) {
    $qaBlockSource = Get-CurricularBlock $Lesson $Owner $Field
    $qaDestination = Join-Path $qaWorking $Destination
    $qaPreserved = Join-Path (Join-Path $qaOutput 'sources') $Destination
    New-Item -ItemType Directory -Force -Path (Split-Path $qaDestination), (Split-Path $qaPreserved) | Out-Null
    # Preserve the decoded authored field byte for byte as UTF-8, including
    # whether it has a final newline. No generated solution is substituted.
    [IO.File]::WriteAllText($qaDestination, $qaBlockSource.value, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $qaDestination -Destination $qaPreserved
    $qaBlockSource.Remove('value')
    $qaBlockSource['file'] = $Destination
    $qaBlockSource['sha256'] = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaDestination).Hash
    $qaSources.Add($qaBlockSource)
}
function Build-InspectionTarget([string] $Name, [string[]] $Sources, [string] $Executable, [switch] $DebugSymbols) {
    $qaOptions = '/W4 /EHsc'
    if ($DebugSymbols) { $qaOptions += ' /Zi /Od' }
    $qaCommand = 'cl ' + $qaOptions + ' ' + ($Sources -join ' ') + ' /Fe:' + $Executable
    $qaBuild = Join-Path $qaWorking ($Name + '.cmd')
    @"
@echo off
call "$qaDeveloper" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 21
cd /d "$qaWorking"
$qaCommand
exit /b %ERRORLEVEL%
"@ | Set-Content -LiteralPath $qaBuild -Encoding ASCII
    Copy-Item -LiteralPath $qaBuild -Destination $qaOutput
    Invoke-RecordedProcess $env:ComSpec ('/d /c "' + $qaBuild + '"') $Name
    $qaBuiltPath = Join-Path $qaWorking $Executable
    if (-not (Test-Path -LiteralPath $qaBuiltPath)) { throw ('No executable was produced: ' + $Executable) }
    Write-Stage ($Name + '-binary') ([ordered]@{
        file = $Executable; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaBuiltPath).Hash
        sourceFiles = $Sources; command = $qaCommand
    })
}
function Read-RecordedOutput([string] $Name) {
    return [IO.File]::ReadAllText((Join-Path $qaOutput ($Name + '.stdout.txt'))).Replace(([string][char]13), '').TrimEnd([char]10)
}
function Assert-RecordedOutput([string] $Name, [string] $Expected) {
    $qaActual = Read-RecordedOutput $Name
    if ($qaActual -cne $Expected.Replace(([string][char]13), '').TrimEnd([char]10)) {
        throw ('Unexpected actual output: ' + $Name)
    }
    Write-Stage ($Name + '-output-verified') $qaActual
}
function Assert-CheckOutput([string] $Name, [int] $Count, [string] $Summary, [string[]] $FailedCases = @()) {
    $qaLines = @((Read-RecordedOutput $Name) -split ([string][char]10))
    if ($qaLines.Count -ne $Count + 1 -or $qaLines[-1] -cne $Summary) {
        throw ('Unexpected test count or summary: ' + $Name)
    }
    $qaFailures = @()
    foreach ($qaLine in $qaLines[0..($qaLines.Count - 2)]) {
        if ($qaLine.EndsWith(': FALLO', [StringComparison]::Ordinal)) { $qaFailures += $qaLine }
        elseif (-not $qaLine.EndsWith(': OK', [StringComparison]::Ordinal)) {
            throw ('Unexpected test line in ' + $Name + ': ' + $qaLine)
        }
    }
    if (($qaFailures -join [char]10) -cne ($FailedCases -join [char]10)) {
        throw ('Unexpected failed cases: ' + $Name)
    }
    Write-Stage ($Name + '-checks-verified') ([ordered]@{ count = $Count; failed = $qaFailures; summary = $Summary })
}

try {
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

public static class InspectionNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
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
        // CREATE_NEW_CONSOLE keeps the real console separate from runner logs.
        if (!CreateProcess(executable, new StringBuilder("\"" + executable + "\" /d /c \"" + batch + "\""),
            IntPtr.Zero, IntPtr.Zero, false, 0x10, IntPtr.Zero, directory, ref startup, out info))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try { return Process.GetProcessById(info.processId); }
        finally { CloseHandle(info.thread); CloseHandle(info.process); }
    }
    public static IntPtr FindTitledWindow(string expectedTitle) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((hwnd, unused) => {
            if (!IsWindowVisible(hwnd)) return true;
            var title = new StringBuilder(2048);
            GetWindowText(hwnd, title, title.Capacity);
            if (title.ToString().IndexOf(expectedTitle, StringComparison.Ordinal) < 0) return true;
            result = hwnd;
            return false;
        }, IntPtr.Zero);
        return result;
    }

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
    function Invoke-ActualConsole([string] $Name, [string] $Body, [string[]] $InputLines = @()) {
        Assert-Time
        $qaReady = Join-Path $qaWorking ($Name + '.ready')
        $qaInputReady = Join-Path $qaWorking ($Name + '.input-ready')
        $qaBatch = Join-Path $qaWorking ($Name + '.cmd')
        $qaTitle = 'LGAED InspectionLog ' + $Name + ' ' + [Guid]::NewGuid().ToString('N')
        $qaConsoleBody = $Body.Replace('__INPUT_READY__', $qaInputReady)
        @"
@echo off
mode con cols=110 lines=32
call "$qaDeveloper" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 31
title $qaTitle
cd /d "$qaWorking"
cls
@echo on
$qaConsoleBody
@echo completed>"$qaReady"
@echo off
pause >nul
"@ | Set-Content -LiteralPath $qaBatch -Encoding ASCII
        Copy-Item -LiteralPath $qaBatch -Destination $qaOutput
        Write-Stage ($Name + '-console-start') ([ordered]@{ batch = $qaBatch; inputLines = $InputLines })
        $qaConsoleProcess = [InspectionNative]::StartConsole($env:ComSpec, $qaBatch, $qaWorking, $qaTitle)
        try {
            if ($InputLines.Count -gt 0) {
                Wait-Until ($Name + ': console ready for its real standard input') {
                    if ($qaConsoleProcess.HasExited) { throw 'The command window exited before reading input.' }
                    return Test-Path -LiteralPath $qaInputReady
                } 60
                $qaConsoleWindow = [InspectionNative]::FindTitledWindow($qaTitle)
                if ($qaConsoleWindow -eq [IntPtr]::Zero) { throw 'No visible console is available for the input.' }
                [InspectionNative]::SetForegroundWindow($qaConsoleWindow) | Out-Null
                Start-Sleep -Milliseconds 250
                if ([InspectionNative]::GetForegroundWindow() -ne $qaConsoleWindow) {
                    throw 'The isolated command window is not foreground; refusing to send keys elsewhere.'
                }
                $qaConsoleKeys = New-Object -ComObject WScript.Shell
                foreach ($qaInputLine in $InputLines) {
                    if ($qaInputLine -match '[+^%~(){}]') { throw 'This capture accepts only the fixed simple curricular paths.' }
                    $qaConsoleKeys.SendKeys($qaInputLine + '{ENTER}')
                    Start-Sleep -Milliseconds 200
                }
            }
            Wait-Until ($Name + ': console reached the successful capture point') {
                if ($qaConsoleProcess.HasExited) { throw ('The command window exited early: ' + $qaConsoleProcess.ExitCode) }
                return Test-Path -LiteralPath $qaReady
            } 60
            $qaConsoleWindow = [InspectionNative]::FindTitledWindow($qaTitle)
            if ($qaConsoleWindow -eq [IntPtr]::Zero) { throw 'The real command window is unavailable.' }
            [InspectionNative]::ShowWindow($qaConsoleWindow, 9) | Out-Null
            [InspectionNative]::MoveWindow($qaConsoleWindow, 0, 0, 1000, 680, $true) | Out-Null
            [InspectionNative]::SetForegroundWindow($qaConsoleWindow) | Out-Null
            Start-Sleep -Milliseconds 400
            $qaConsoleRect = New-Object InspectionNative+Rect
            if (-not [InspectionNative]::GetWindowRect($qaConsoleWindow, [ref] $qaConsoleRect)) {
                throw 'Cannot read the actual console dimensions.'
            }
            $qaWidth = $qaConsoleRect.Right - $qaConsoleRect.Left
            $qaHeight = $qaConsoleRect.Bottom - $qaConsoleRect.Top
            if ($qaWidth -lt 700 -or $qaHeight -lt 500) { throw 'The actual console is too small for a legible capture.' }
            $qaBitmap = [Drawing.Bitmap]::new($qaWidth, $qaHeight)
            $qaGraphics = [Drawing.Graphics]::FromImage($qaBitmap)
            $qaPath = Join-Path $qaOutput ($Name + '.png')
            try {
                $qaDc = $qaGraphics.GetHdc()
                try { $qaPrinted = [InspectionNative]::PrintWindow($qaConsoleWindow, $qaDc, 2) }
                finally { $qaGraphics.ReleaseHdc($qaDc) }
                if (-not $qaPrinted) { throw 'Windows did not provide the real command-window pixels.' }
                $qaBitmap.Save($qaPath, [Drawing.Imaging.ImageFormat]::Png)
            } finally { $qaGraphics.Dispose(); $qaBitmap.Dispose() }
            $qaCaptures.Add([ordered]@{
                file = $Name + '.png'; method = 'PrintWindow of the newly created real command window'
                width = $qaWidth; height = $qaHeight; commandProcessId = $qaConsoleProcess.Id
                batch = $Name + '.cmd'; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaPath).Hash
            })
            Write-Stage ($Name + '-console-captured') 'Commands completed in the real console; native window pixels preserved.'
        }
        finally {
            if (-not $qaConsoleProcess.HasExited) {
                & (Join-Path $env:WINDIR 'System32\taskkill.exe') /PID $qaConsoleProcess.Id /T /F | Out-Null
            }
            $qaConsoleProcess.Dispose()
        }
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
    function Show-ReportLocals([string] $CaptureName, [string] $Expected) {
        $qaDte.ExecuteCommand('Debug.Locals', '')
        $qaVsHandle = [InspectionNative]::FindWindowForProcess($qaVsProcess.Id)
        $qaNativeRoot = [Windows.Automation.AutomationElement]::FromHandle($qaVsHandle)
        # These names and control types were observed in the real debugger UI.
        $qaTreeCondition = [Windows.Automation.AndCondition]::new(
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, 'Locals'),
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Tree))
        $qaRowCondition = [Windows.Automation.AndCondition]::new(
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, 'aceptados'),
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::TreeItem))
        Wait-Until 'the observed Locals tree in the actual debugger' {
            $script:qaLocatedLocalsTree = $qaNativeRoot.FindFirst([Windows.Automation.TreeScope]::Descendants, $qaTreeCondition)
            return $qaLocatedLocalsTree -and -not $qaLocatedLocalsTree.Current.IsOffscreen
        } 20
        $qaScrollItemTried = $false
        $qaSelectionTried = $false
        $qaScrollStarted = $false
        $qaKeyboardStarted = $false
        $qaActions = [Collections.Generic.List[string]]::new()
        try {
            for ($qaAttempt = 0; $qaAttempt -le 16; ++$qaAttempt) {
                Assert-Time
                if ([int] $qaDte.Debugger.CurrentMode -ne 2) { throw 'The debugger left break mode while exposing Locals.' }
                $qaTree = $qaNativeRoot.FindFirst([Windows.Automation.TreeScope]::Descendants, $qaTreeCondition)
                if (-not $qaTree) { throw 'The observed Locals tree disappeared.' }
                $qaRow = $qaTree.FindFirst([Windows.Automation.TreeScope]::Children, $qaRowCondition)
                if ($qaRow) {
                    $qaRowBounds = $qaRow.Current.BoundingRectangle
                    $qaTreeBounds = $qaTree.Current.BoundingRectangle
                    # The observed complete rows are 19 pixels tall. A partially
                    # clipped bottom row can report IsOffscreen=false and 16px.
                    if (-not $qaRow.Current.IsOffscreen -and $qaRowBounds.Height -ge 18 -and
                        $qaTreeBounds.Contains($qaRowBounds)) {
                        $qaUiValue = $qaRow.GetCurrentPropertyValue([Windows.Automation.ValuePattern]::ValueProperty, $true)
                        if ($qaUiValue -eq [Windows.Automation.AutomationElement]::NotSupported) { $qaUiValue = $null }
                        Write-Stage 'locals-accumulator-visible' ([ordered]@{
                            capture = $CaptureName; expectedDebuggerValue = $Expected
                            name = $qaRow.Current.Name; bounds = [string] $qaRowBounds
                            uiValue = $qaUiValue; treeBounds = [string] $qaTreeBounds
                            rowPatterns = @($qaRow.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName })
                            actions = @($qaActions)
                        })
                        Save-ActualVsWindow $CaptureName
                        return
                    }
                    $qaItemPattern = $null
                    if (-not $qaScrollItemTried -and $qaRow.TryGetCurrentPattern([Windows.Automation.ScrollItemPattern]::Pattern, [ref] $qaItemPattern)) {
                        $qaScrollItemTried = $true
                        try { $qaItemPattern.ScrollIntoView(); $qaActions.Add('ScrollItemPattern.ScrollIntoView: aceptados') }
                        catch { $qaActions.Add('ScrollItemPattern: ' + $_.Exception.Message) }
                        Start-Sleep -Milliseconds 200
                        continue
                    }
                    $qaSelectionPattern = $null
                    if (-not $qaSelectionTried -and $qaRow.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern, [ref] $qaSelectionPattern)) {
                        $qaSelectionTried = $true
                        try { $qaSelectionPattern.Select(); $qaActions.Add('SelectionItemPattern.Select: aceptados') }
                        catch { $qaActions.Add('SelectionItemPattern: ' + $_.Exception.Message) }
                        Start-Sleep -Milliseconds 200
                        continue
                    }
                }
                $qaScrollPattern = $null
                if ($qaTree.TryGetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern, [ref] $qaScrollPattern) -and
                    $qaScrollPattern.Current.VerticallyScrollable) {
                    if (-not $qaScrollStarted) {
                        $qaScrollPattern.SetScrollPercent([Windows.Automation.ScrollPattern]::NoScroll, 0)
                        $qaActions.Add('ScrollPattern: start of Locals')
                        $qaScrollStarted = $true
                    }
                    else {
                        $qaScrollPattern.ScrollVertical([Windows.Automation.ScrollAmount]::SmallIncrement)
                        $qaActions.Add('ScrollPattern: one small vertical increment')
                    }
                }
                else {
                    # Some native VS trees expose selection but no scroll
                    # provider. Navigate only this observed, focused tree;
                    # Home/Down select rows and cannot edit a variable value.
                    [InspectionNative]::SetForegroundWindow($qaVsHandle) | Out-Null
                    $qaTree.SetFocus()
                    $qaFocusCondition = [Windows.Automation.PropertyCondition]::new(
                        [Windows.Automation.AutomationElement]::HasKeyboardFocusProperty, $true)
                    $qaFocused = $qaTree.FindFirst([Windows.Automation.TreeScope]::Subtree, $qaFocusCondition)
                    if (-not $qaFocused -or [InspectionNative]::GetForegroundWindow() -ne $qaVsHandle) {
                        throw 'Keyboard focus is not inside the observed Locals tree.'
                    }
                    if (-not $qaKeyboardStarted) {
                        [Windows.Forms.SendKeys]::SendWait('{HOME}')
                        $qaActions.Add('Home in the focused Locals tree')
                        $qaKeyboardStarted = $true
                    }
                    else {
                        [Windows.Forms.SendKeys]::SendWait('{DOWN}')
                        $qaActions.Add('Down in the focused Locals tree')
                    }
                }
                Start-Sleep -Milliseconds 200
            }
            throw 'The actual aceptados row could not be made fully visible in Locals.'
        }
        catch {
            $qaCaptureFailure = $_
            try {
                $qaInventory = @()
                foreach ($qaControl in $qaNativeRoot.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)) {
                    $qaInventory += [ordered]@{
                        name = $qaControl.Current.Name; id = $qaControl.Current.AutomationId
                        type = $qaControl.Current.ControlType.ProgrammaticName
                        offscreen = $qaControl.Current.IsOffscreen; bounds = [string] $qaControl.Current.BoundingRectangle
                        patterns = @($qaControl.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName })
                    }
                }
                $qaInventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $qaOutput ($CaptureName + '-locals-ui.json')) -Encoding UTF8
                Write-Stage 'locals-scroll-diagnostic' ([ordered]@{ capture = $CaptureName; actions = @($qaActions) })
            }
            catch { Write-Stage 'locals-ui-diagnostic-error' $_.Exception.Message }
            throw $qaCaptureFailure
        }
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

    if ($Mode -eq 'Probe') {
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
    }
    else {
        $qaSources = [Collections.Generic.List[object]]::new()
        $qaState['sources'] = $qaSources
        $qaModelLesson = 'lesson_construir_el_modelo_de_inspectionlog'
        $qaApplicationLesson = 'lesson_integrar_archivos_y_consola_en_inspectionlog'
        $qaTestsLesson = 'lesson_probar_conservacion_y_errores_en_inspectionlog'
        $qaVariantLesson = 'lesson_crear_una_variante_y_documentar_inspectionlog'
        $qaDebugLesson = 'lesson_depurar_un_fallo_del_informe_en_windows'
        Export-CurricularSource $qaModelLesson 'block_0003_0002' 'code' 'inspection_log.h'
        Export-CurricularSource $qaModelLesson 'block_0005_0002' 'code' 'modelo.cpp'
        Export-CurricularSource $qaModelLesson 'block_0006_0002' 'code' 'prueba_modelo.cpp'
        Export-CurricularSource $qaApplicationLesson 'block_0004_0002' 'code' 'aplicacion.h'
        Export-CurricularSource $qaApplicationLesson 'block_0004_0004' 'code' 'informe.cpp'
        Export-CurricularSource $qaApplicationLesson 'block_0005_0002' 'code' 'archivos.cpp'
        Export-CurricularSource $qaApplicationLesson 'block_0006_0002' 'code' 'main.cpp'
        Export-CurricularSource $qaApplicationLesson 'block_0007_0006' 'text' 'ejemplos/base.txt'
        Export-CurricularSource $qaTestsLesson 'block_0004_0002' 'code' 'pruebas.cpp'
        Export-CurricularSource $qaVariantLesson 'block_0003_0002' 'code' 'informe_variante.cpp'
        Export-CurricularSource $qaVariantLesson 'block_0004_0002' 'code' 'pruebas_variante.cpp'
        Export-CurricularSource $qaDebugLesson 'block_0001_0003' 'code' 'depurar_informe.cpp'
        Export-CurricularSource $qaDebugLesson 'block_0007_0004' 'code' 'informe_error.cpp'
        Write-Stage 'curricular-sources-extracted' $qaSources.Count
        New-Item -ItemType Directory -Force -Path (Join-Path $qaWorking 'resultados'), (Join-Path $qaWorking 'pruebas_tmp') | Out-Null

        Build-InspectionTarget 'compile-model' @('modelo.cpp', 'prueba_modelo.cpp') 'prueba_modelo.exe'
        Invoke-RecordedProcess (Join-Path $qaWorking 'prueba_modelo.exe') '' 'model-checks' 20
        Assert-RecordedOutput 'model-checks' (Get-CurricularBlock $qaModelLesson 'block_0006_0004' 'text').value

        Build-InspectionTarget 'compile-application' @('modelo.cpp', 'informe.cpp', 'archivos.cpp', 'main.cpp') 'inspectionlog.exe'
        $qaInput = Join-Path $qaOutput 'application.stdin.txt'
        [IO.File]::WriteAllLines($qaInput, @('ejemplos/base.txt', 'resultados/base_guardada.txt'), [Text.UTF8Encoding]::new($false))
        Invoke-RecordedProcess (Join-Path $qaWorking 'inspectionlog.exe') '' 'application' 20 -StandardInput $qaInput
        $qaExpectedApplication = (@('InspectionLog', 'Ruta de entrada:', 'Ruta de salida:') -join [char]10) + [char]10
        $qaExpectedApplication += (Get-CurricularBlock $qaApplicationLesson 'block_0007_0010' 'text').value
        Assert-RecordedOutput 'application' $qaExpectedApplication
        $qaSaved = Join-Path $qaWorking 'resultados/base_guardada.txt'
        if ([IO.File]::ReadAllText($qaSaved) -cne [IO.File]::ReadAllText((Join-Path $qaWorking 'ejemplos/base.txt'))) {
            throw 'The application did not preserve the complete canonical fixture.'
        }
        Copy-Item -LiteralPath $qaSaved -Destination (Join-Path $qaOutput 'base_guardada.txt')
        Write-Stage 'application-saved-file-verified' (Get-FileHash -Algorithm SHA256 -LiteralPath $qaSaved).Hash
        Invoke-ActualConsole 'inspection_application_console' @'
cl /W4 /EHsc modelo.cpp informe.cpp archivos.cpp main.cpp /Fe:inspectionlog.exe
@if errorlevel 1 exit /b 32
@echo ready>"__INPUT_READY__"
.\inspectionlog.exe
@if errorlevel 1 exit /b 33
echo %ERRORLEVEL%
'@ @('ejemplos/base.txt', 'resultados/base_guardada.txt')
        if ([IO.File]::ReadAllText($qaSaved) -cne [IO.File]::ReadAllText((Join-Path $qaWorking 'ejemplos/base.txt'))) {
            throw 'The real interactive console run changed the saved fixture.'
        }

        Build-InspectionTarget 'compile-variant' @('modelo.cpp', 'informe_variante.cpp', 'archivos.cpp', 'pruebas_variante.cpp') 'pruebas_variante.exe'
        Invoke-RecordedProcess (Join-Path $qaWorking 'pruebas_variante.exe') '' 'variant-checks' 20
        Assert-CheckOutput 'variant-checks' 9 'Variante: OK'
        Build-InspectionTarget 'compile-variant-application' @('modelo.cpp', 'informe_variante.cpp', 'archivos.cpp', 'main.cpp') 'inspectionlog_variante.exe'
        Invoke-RecordedProcess (Join-Path $qaWorking 'inspectionlog_variante.exe') '' 'variant-application' 20 -StandardInput $qaInput
        $qaExpectedVariant = $qaExpectedApplication.Replace(
            ('Mayor: rojo 3' + [char]10),
            ((@('Mayor: rojo 3', 'Registros rojo: 2', 'Registros azul: 0') -join [char]10) + [char]10))
        Assert-RecordedOutput 'variant-application' $qaExpectedVariant
        if ([IO.File]::ReadAllText($qaSaved) -cne [IO.File]::ReadAllText((Join-Path $qaWorking 'ejemplos/base.txt'))) {
            throw 'The variant client did not preserve the complete canonical fixture.'
        }
        Write-Stage 'variant-application-saved-file-verified' (Get-FileHash -Algorithm SHA256 -LiteralPath $qaSaved).Hash

        $qaSourceName = 'informe_error.cpp'
        $qaSourcePath = Join-Path $qaWorking $qaSourceName
        $qaBugSource = [IO.File]::ReadAllText($qaSourcePath)
        $qaCorrectSource = [IO.File]::ReadAllText((Join-Path $qaWorking 'informe.cpp'))
        $qaBugStatement = 'aceptados = registro.recuento.aceptados;'
        $qaCorrectStatement = 'aceptados += registro.recuento.aceptados;'
        if ($qaBugSource.Replace($qaBugStatement, $qaCorrectStatement) -cne $qaCorrectSource) {
            throw 'The authored error copy must differ from the report only by the demonstrated assignment.'
        }
        $qaSourceLines = [IO.File]::ReadAllLines($qaSourcePath)
        $qaBreakpointCandidates = @(for ($qaLine = 0; $qaLine -lt $qaSourceLines.Count; ++$qaLine) {
            if ($qaSourceLines[$qaLine].Trim() -ceq $qaBugStatement) { $qaLine + 1 }
        })
        if ($qaBreakpointCandidates.Count -ne 1) { throw 'The report must contain one unique breakpoint location.' }
        $qaBreakpointLine = $qaBreakpointCandidates[0]
        $qaExecutable = Join-Path $qaWorking 'depurar_informe.exe'
        Build-InspectionTarget 'compile-debug-error' @('modelo.cpp', 'informe_error.cpp', 'depurar_informe.cpp') 'depurar_informe.exe' -DebugSymbols
        Invoke-RecordedProcess $qaExecutable '' 'debug-error-output' 20
        Assert-RecordedOutput 'debug-error-output' (@('Grupos: 1', 'Aceptados: 3', 'Rechazados: 1', 'Mayor: verde 5') -join [char]10)
        # Build the counterexample while the exact authored error source is
        # still on disk. Its executable is run after the corrected verification.
        Build-InspectionTarget 'compile-tests-error' @('modelo.cpp', 'informe_error.cpp', 'archivos.cpp', 'pruebas.cpp') 'pruebas_error.exe'
        $qaState['source'] = [ordered]@{
            path = $qaSourceName; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaSourcePath).Hash
            breakpointLine = $qaBreakpointLine
            executableSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaExecutable).Hash
        }
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
    Save-ActualVsWindow ($Mode.ToLowerInvariant() + '_breakpoint_configured')

    if ($Mode -eq 'Probe') {
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
    }
    else {
        function Wait-DebugValue([string] $Expression, [string] $Expected, [string] $Description) {
            Wait-Until $Description {
                if ([int] $qaDte.Debugger.CurrentMode -ne 2) { return $false }
                $qaExpression = $qaDte.Debugger.GetExpression($Expression, $false, 1000)
                return $qaExpression.IsValidValue -and [string] $qaExpression.Value -ceq $Expected
            } 60
        }
        function Trace-Report([string] $Name, [string] $SecondTotal) {
            Write-Stage ($Name + '-start') 'Run the real executable and observe the two source breakpoint visits.'
            $qaDte.Debugger.Go($false)
            Wait-DebugValue 'registro.id' '6' ($Name + ': first record at the native breakpoint')
            Write-Stage ($Name + '-first-frame') ([string] $qaDte.Debugger.CurrentStackFrame.FunctionName)
            $qaFirstBefore = Read-DebugExpression 'aceptados' '0'
            Read-DebugExpression 'registro.recuento.aceptados' '2' | Out-Null
            Show-ReportLocals ($Name + '_first_before') '0'
            $qaDte.Debugger.StepOver($false)
            Wait-DebugValue 'aceptados' '2' ($Name + ': first Step Over')
            $qaFirstAfter = Read-DebugExpression 'aceptados' '2'
            Show-ReportLocals ($Name + '_first_after') '2'
            $qaDte.Debugger.Go($false)
            # The record identity distinguishes the second stop from the old
            # asynchronous break mode with the same accumulator value.
            Wait-DebugValue 'registro.id' '0' ($Name + ': second record at the native breakpoint')
            $qaSecondBefore = Read-DebugExpression 'aceptados' '2'
            Read-DebugExpression 'registro.recuento.aceptados' '3' | Out-Null
            Show-ReportLocals ($Name + '_second_before') '2'
            $qaDte.Debugger.StepOver($false)
            Wait-DebugValue 'aceptados' $SecondTotal ($Name + ': second Step Over')
            $qaSecondAfter = Read-DebugExpression 'aceptados' $SecondTotal
            Show-ReportLocals ($Name + '_second_after') $SecondTotal
            $qaDte.Debugger.Go($false)
            Wait-Until ($Name + ': debugged process termination') { [int] $qaDte.Debugger.CurrentMode -eq 1 } 45
            return [ordered]@{
                firstBefore = $qaFirstBefore; firstAfter = $qaFirstAfter
                secondBefore = $qaSecondBefore; secondAfter = $qaSecondAfter
            }
        }
        $qaState['observed'] = [ordered]@{}
        $qaState.observed['error'] = Trace-Report 'inspection_error' '3'

        # Apply the student's one-character correction in the actual IDE
        # editor, save it, verify the saved file and then rebuild the binary.
        $qaDocumentWindow.Activate()
        $qaColumn = $qaSourceLines[$qaBreakpointLine - 1].IndexOf('=') + 1
        $qaDte.ActiveDocument.Selection.MoveToLineAndOffset($qaBreakpointLine, $qaColumn, $false)
        $qaDte.ActiveDocument.Selection.Insert('+', 1)
        $qaDte.ActiveDocument.Save()
        $qaSavedCorrection = [IO.File]::ReadAllText($qaSourcePath)
        if ($qaSavedCorrection -cne $qaCorrectSource) {
            throw 'The actual editor correction differs from the authored correct report.'
        }
        Copy-Item -LiteralPath $qaSourcePath -Destination (Join-Path $qaOutput 'informe_error_corrected.cpp')
        Write-Stage 'editor-correction-saved' ([ordered]@{
            method = 'TextSelection.MoveToLineAndOffset/Insert and Document.Save in the running IDE'
            file = $qaSourceName; line = $qaBreakpointLine; column = $qaColumn
            beforeSha256 = $qaState.source.sha256
            afterSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaSourcePath).Hash
        })
        Save-ActualVsWindow 'inspection_correction_saved'
        Build-InspectionTarget 'compile-debug-corrected' @('modelo.cpp', 'informe_error.cpp', 'depurar_informe.cpp') 'depurar_informe.exe' -DebugSymbols
        Invoke-RecordedProcess $qaExecutable '' 'debug-corrected-output' 20
        Assert-RecordedOutput 'debug-corrected-output' (@('Grupos: 1', 'Aceptados: 5', 'Rechazados: 1', 'Mayor: verde 5') -join [char]10)
        $qaState.observed['corrected'] = Trace-Report 'inspection_corrected' '5'

        Build-InspectionTarget 'compile-tests-corrected' @('modelo.cpp', 'informe_error.cpp', 'archivos.cpp', 'pruebas.cpp') 'pruebas_corregidas.exe'
        Invoke-RecordedProcess (Join-Path $qaWorking 'pruebas_corregidas.exe') '' 'corrected-checks' 25
        Assert-CheckOutput 'corrected-checks' 45 'Bateria completa: OK'
        Invoke-ActualConsole 'inspection_tests_console' @'
.\pruebas_corregidas.exe
@if errorlevel 1 exit /b 34
echo %ERRORLEVEL%
'@
        Invoke-RecordedProcess (Join-Path $qaWorking 'pruebas_error.exe') '' 'error-counterexample' 25 -ExpectedExit 1
        Assert-CheckOutput 'error-counterexample' 45 'Bateria completa: FALLO' @('informe_limite: FALLO')
        $qaState.observed['counterexample'] = 'The original authored assignment error fails informe_limite and returns exit 1.'
    }
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
