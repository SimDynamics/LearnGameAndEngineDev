[CmdletBinding()]
param([string] $Manifest = '')

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Manifest)) { $Manifest = Join-Path $PSScriptRoot 'InspectionLog.publication.json' }
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Run only on a disposable GitHub-hosted Windows runner.'
}
if ([Threading.Thread]::CurrentThread.ApartmentState -ne 'STA') { throw 'Windows PowerShell -STA is required.' }
$qaRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaOutput = Join-Path $qaRepository 'out\qa\inspection-publication'
$qaBase = 'C:\LGAED-CurricularQA\published'
$qaDownloads = Join-Path $qaBase 'zip'
$qaExtracted = Join-Path $qaBase 'extracted'
if (Test-Path -LiteralPath $qaBase) { throw 'The publication workspace must be new for this reproduction.' }
New-Item -ItemType Directory -Force -Path $qaOutput, $qaDownloads | Out-Null
Start-Transcript -LiteralPath (Join-Path $qaOutput 'capture-transcript.txt') -Force | Out-Null
$qaStarted = [DateTime]::UtcNow
$qaDeadline = $qaStarted.AddMinutes(8)
$qaWorking = $qaBase
$qaEvents = [Collections.Generic.List[object]]::new()
$qaCaptures = [Collections.Generic.List[object]]::new()
$qaVerbProcess = $null
$qaState = [ordered]@{
    status = 'running'; commit = $env:GITHUB_SHA; startedAtUtc = $qaStarted.ToString('o')
    scope = 'Published InspectionLog ZIP reproduced on disposable Windows Server with preinstalled Visual Studio Enterprise/MSVC/SDK.'
    installationRepeated = $false; debuggerRepeated = $false; manualExtractionVerified = $false
    events = $qaEvents; captures = $qaCaptures
}

# Parse the existing, verified recipe and import only these definitions. Its
# top-level body, source export and debugger session are never executed here.
$qaRecipe = Join-Path $PSScriptRoot 'Capture-InspectionLog.ps1'
$qaTokens = $null; $qaParseErrors = $null
$qaAst = [Management.Automation.Language.Parser]::ParseFile($qaRecipe, [ref] $qaTokens, [ref] $qaParseErrors)
if ($qaParseErrors.Count) { throw 'The existing capture helpers contain a parse error.' }
$qaDefinitions = @($qaAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $true))
foreach ($qaHelperName in @('Write-Provenance', 'Write-Stage', 'Wait-Until', 'Invoke-RecordedProcess', 'Build-InspectionTarget', 'Invoke-ActualConsole')) {
    $qaDefinition = @($qaDefinitions | Where-Object { $_.Name -ceq $qaHelperName })
    if ($qaDefinition.Count -ne 1) { throw ('Expected one existing helper: ' + $qaHelperName) }
    . ([ScriptBlock]::Create($qaDefinition[0].Extent.Text))
}
function Assert-Time {
    if ([DateTime]::UtcNow -gt $qaDeadline) { throw 'The bounded publication reproduction exceeded eight minutes.' }
}

function Save-PublicationWindow([IntPtr] $Handle, [string] $Name, [int] $Width = 0, [int] $Height = 0) {
    $qaInitial = New-Object InspectionNative+Rect
    if (-not [InspectionNative]::GetWindowRect($Handle, [ref] $qaInitial)) { throw 'The observed window disappeared.' }
    $qaScreen = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if (-not $Width) { $Width = $qaInitial.Right - $qaInitial.Left }
    if (-not $Height) { $Height = $qaInitial.Bottom - $qaInitial.Top }
    [InspectionNative]::ShowWindow($Handle, 9) | Out-Null
    [InspectionNative]::MoveWindow($Handle, $qaScreen.Left, $qaScreen.Top,
        [Math]::Min($Width, $qaScreen.Width), [Math]::Min($Height, $qaScreen.Height), $true) | Out-Null
    [InspectionNative]::SetForegroundWindow($Handle) | Out-Null
    Start-Sleep -Milliseconds 500
    $qaRect = New-Object InspectionNative+Rect
    if (-not [InspectionNative]::GetWindowRect($Handle, [ref] $qaRect)) { throw 'Cannot read actual capture bounds.' }
    $qaBounds = [Drawing.Rectangle]::new($qaRect.Left, $qaRect.Top, $qaRect.Right - $qaRect.Left, $qaRect.Bottom - $qaRect.Top)
    if (-not $qaScreen.Contains($qaBounds) -or [InspectionNative]::GetForegroundWindow() -ne $Handle) {
        throw 'The observed window is clipped or not foreground; refusing that screenshot.'
    }
    $qaImagePath = Join-Path $qaOutput ($Name + '.png')
    $qaBitmap = [Drawing.Bitmap]::new($qaBounds.Width, $qaBounds.Height)
    $qaGraphics = [Drawing.Graphics]::FromImage($qaBitmap)
    try {
        $qaGraphics.CopyFromScreen($qaBounds.Left, $qaBounds.Top, 0, 0, $qaBitmap.Size)
        $qaBitmap.Save($qaImagePath, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $qaGraphics.Dispose(); $qaBitmap.Dispose() }
    $qaCaptures.Add([ordered]@{
        file = $Name + '.png'; method = 'CopyFromScreen of the observed foreground window'
        width = $qaBounds.Width; height = $qaBounds.Height
        sha256 = (Get-FileHash -LiteralPath $qaImagePath -Algorithm SHA256).Hash
    })
    Write-Provenance
}
function Save-PublicationControls([Windows.Automation.AutomationElement] $Root, [string] $Name) {
    $qaControls = @()
    foreach ($qaItem in $Root.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)) {
        $qaCurrent = $qaItem.Current
        $qaControls += [ordered]@{
            name = $qaCurrent.Name; id = $qaCurrent.AutomationId; type = $qaCurrent.ControlType.ProgrammaticName
            class = $qaCurrent.ClassName; offscreen = $qaCurrent.IsOffscreen; bounds = [string] $qaCurrent.BoundingRectangle
            patterns = @($qaItem.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName })
        }
    }
    $qaControls | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $qaOutput ($Name + '.json')) -Encoding UTF8
}
function Open-PublicationFolder([string] $Folder) {
    Start-Process -FilePath (Join-Path $env:WINDIR 'explorer.exe') -ArgumentList ('/n,"' + $Folder + '"') | Out-Null
    Wait-Until ('Explorer view for ' + $Folder) {
        foreach ($qaWindow in @($qaShell.Windows())) {
            try {
                if ([string] $qaWindow.Document.Folder.Self.Path -ieq $Folder) {
                    $script:qaExplorerView = $qaWindow
                    return $true
                }
            }
            catch { }
        }
        return $false
    } 30
    return $qaExplorerView
}
function Assert-PublishedFiles([string] $Folder, [switch] $ExactFileCount) {
    if ($ExactFileCount) {
        $qaFiles = @(Get-ChildItem -LiteralPath $Folder -File -Force -Recurse)
        if ($qaFiles.Count -ne 15) { throw ('Expected 15 extracted files; found ' + $qaFiles.Count) }
    }
    $qaChecked = @()
    foreach ($qaFile in $qaPublication.files) {
        $qaPath = Join-Path $Folder $qaFile.path
        if (-not (Test-Path -LiteralPath $qaPath -PathType Leaf)) { throw ('Missing published file: ' + $qaFile.path) }
        $qaHash = (Get-FileHash -LiteralPath $qaPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($qaHash -cne $qaFile.sha256 -or (Get-Item -LiteralPath $qaPath).Length -ne $qaFile.bytes) {
            throw ('Published content mismatch: ' + $qaFile.path)
        }
        $qaChecked += [ordered]@{ path = $qaFile.path; sha256 = $qaHash; bytes = $qaFile.bytes }
    }
    return $qaChecked
}
function Invoke-PublishedConsole([string] $Name, [string] $Executable, [string[]] $InputLines = @()) {
    $qaExitFile = Join-Path $qaOutput ($Name + '.exit.txt')
    $qaBody = ''
    if ($InputLines.Count) { $qaBody += '@echo ready>"__INPUT_READY__"' + [Environment]::NewLine }
    $qaBody += '.\' + $Executable + [Environment]::NewLine
    $qaBody += '@set "qaProgramExit=%ERRORLEVEL%"' + [Environment]::NewLine
    $qaBody += 'echo %qaProgramExit%' + [Environment]::NewLine
    $qaBody += '@>"' + $qaExitFile + '" echo %qaProgramExit%' + [Environment]::NewLine
    $qaBody += '@if not "%qaProgramExit%"=="0" exit /b 34' + [Environment]::NewLine
    Invoke-ActualConsole $Name $qaBody $InputLines
    $qaProgramExit = [IO.File]::ReadAllText($qaExitFile).Trim()
    if ($qaProgramExit -cne '0') { throw ('Published executable failed: ' + $Executable) }
    Write-Stage ($Name + '-verified') ([ordered]@{ executable = $Executable; exitCode = 0; input = $InputLines; workingDirectory = $qaWorking })
}

try {
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $qaNativeLiteral = @($qaAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.StringConstantExpressionAst] -and
        $node.Value.Contains('public static class InspectionNative {')
    }, $true))
    if ($qaNativeLiteral.Count -ne 1) { throw 'Expected the one verified native capture helper.' }
    Add-Type -TypeDefinition $qaNativeLiteral[0].Value
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PublicationPointer {
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    public static void Click(IntPtr expectedWindow, int x, int y) {
        if (GetForegroundWindow() != expectedWindow) throw new InvalidOperationException("The extraction dialog is not foreground.");
        if (!SetCursorPos(x, y)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        if (GetForegroundWindow() != expectedWindow) throw new InvalidOperationException("The extraction dialog lost foreground.");
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }
}
'@
    Write-Stage 'helpers-imported-without-running-debugger' ([ordered]@{
        file = 'Capture-InspectionLog.ps1'; sha256 = (Get-FileHash -LiteralPath $qaRecipe -Algorithm SHA256).Hash
    })
    $qaPublication = [IO.File]::ReadAllText($Manifest) | ConvertFrom-Json
    if ($qaPublication.repository -cne 'SimDynamics/InspectionLog' -or $qaPublication.commit -notmatch '^[0-9a-f]{40}$') {
        throw 'The manifest must pin the published InspectionLog commit.'
    }
    if (@($qaPublication.files).Count -ne 15) { throw 'The publication manifest must contain exactly 15 files.' }
    foreach ($qaFile in $qaPublication.files) {
        if ($qaFile.path -notmatch '^(?:[a-zA-Z0-9_.-]+/)*[a-zA-Z0-9_.-]+$' -or $qaFile.path -match '(^|/)\.\.(/|$)' -or
            $qaFile.sha256 -notmatch '^[0-9a-f]{64}$' -or $qaFile.bytes -lt 1) { throw 'Unsafe or incomplete publication manifest.' }
    }
    if (@($qaPublication.files.path | Sort-Object -Unique).Count -ne 15) { throw 'Duplicate publication paths.' }
    Copy-Item -LiteralPath $Manifest -Destination (Join-Path $qaOutput 'publication-manifest.json')
    $qaArchiveUrl = 'https://github.com/' + $qaPublication.repository + '/archive/' + $qaPublication.commit + '.zip'
    $qaZipPath = Join-Path $qaDownloads 'InspectionLog.zip'
    Invoke-WebRequest -UseBasicParsing -Uri $qaArchiveUrl -OutFile $qaZipPath
    $qaZipHash = (Get-FileHash -LiteralPath $qaZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $qaState['publication'] = [ordered]@{
        repository = $qaPublication.repository; commit = $qaPublication.commit; archiveUrl = $qaArchiveUrl
        archiveSha256 = $qaZipHash; browserArchiveSha256 = $qaPublication.browserArchiveSha256
        browserDownloadEvidence = $qaPublication.browserDownloadEvidence
        archiveHashNote = 'Commit and the 15 file hashes are the gate. Branch ZIP prefixes and regenerated compression may produce different archive hashes.'
    }
    Copy-Item -LiteralPath $qaZipPath -Destination (Join-Path $qaOutput 'InspectionLog-published.zip')
    $qaArchive = [IO.Compression.ZipFile]::OpenRead($qaZipPath)
    try {
        $qaEntries = @($qaArchive.Entries | Where-Object { $_.Name -ne '' })
        if ($qaEntries.Count -ne 15) { throw 'The downloaded ZIP does not contain exactly 15 files.' }
        $qaArchiveRoots = @($qaEntries | ForEach-Object { ($_.FullName -split '/', 2)[0] } | Sort-Object -Unique)
        if ($qaArchiveRoots.Count -ne 1 -or $qaArchiveRoots[0] -notmatch '^[a-zA-Z0-9_.-]+$') { throw 'Unexpected ZIP root.' }
        $qaArchiveRoot = $qaArchiveRoots[0]
        foreach ($qaEntry in $qaEntries) {
            if ($qaEntry.FullName -match '(^/|\\|(^|/)\.\.(/|$)|:)') { throw 'Unsafe ZIP entry.' }
            $qaRelative = ($qaEntry.FullName -split '/', 2)[1]
            $qaExpected = @($qaPublication.files | Where-Object { $_.path -ceq $qaRelative })
            if ($qaExpected.Count -ne 1 -or $qaEntry.Length -ne $qaExpected[0].bytes) { throw ('Unexpected ZIP file: ' + $qaRelative) }
            $qaStream = $qaEntry.Open(); $qaSha = [Security.Cryptography.SHA256]::Create()
            try { $qaHash = ([BitConverter]::ToString($qaSha.ComputeHash($qaStream))).Replace('-', '').ToLowerInvariant() }
            finally { $qaSha.Dispose(); $qaStream.Dispose() }
            if ($qaHash -cne $qaExpected[0].sha256) { throw ('Downloaded ZIP content mismatch: ' + $qaRelative) }
        }
    }
    finally { $qaArchive.Dispose() }
    Write-Stage 'published-zip-verified' $qaState.publication

    $qaShell = New-Object -ComObject Shell.Application
    try {
        $qaExplorer = Open-PublicationFolder $qaDownloads
        $qaZipItem = $qaExplorer.Document.Folder.ParseName('InspectionLog.zip')
        if (-not $qaZipItem) { throw 'Explorer did not expose the downloaded ZIP.' }
        $qaExplorer.Document.SelectItem($qaZipItem, 29)
        $qaExplorerHandle = [IntPtr] ([long] $qaExplorer.HWND)
        Save-PublicationWindow $qaExplorerHandle 'inspection_published_zip_selected' 1000 680
        Save-PublicationControls ([Windows.Automation.AutomationElement]::FromHandle($qaExplorerHandle)) 'zip-explorer-controls'
        $qaVerbs = @($qaZipItem.Verbs())
        $qaVerbNames = @($qaVerbs | ForEach-Object { [string] $_.Name })
        Write-Stage 'observed-zip-verbs' $qaVerbNames
        $qaExtractVerbs = @($qaVerbs | Where-Object { (([string] $_.Name).Replace('&', '').Trim() -replace '(?:\.{3}|\u2026)$', '').Trim() -ieq 'Extract All' })
        if ($qaExtractVerbs.Count -ne 1) { throw 'Explorer exposes no unique Extract All verb for this ZIP.' }
        $qaVerbName = [string] $qaExtractVerbs[0].Name
        # The observed Explorer exposes a real Extract all AppBarButton.
        # Invoke that UI control, without depending on the Shell verb behavior.
        $qaExplorerRoot = [Windows.Automation.AutomationElement]::FromHandle($qaExplorerHandle)
        $qaExtractButtons = @($qaExplorerRoot.FindAll([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button)) |
            Where-Object { -not $_.Current.IsOffscreen -and $_.Current.IsEnabled -and $_.Current.Name -ceq 'Extract all' })
        if ($qaExtractButtons.Count -ne 1) { throw 'Explorer no longer exposes the observed Extract all button.' }
        $qaExtractionComplete = Join-Path $qaOutput 'extraction-ui-complete.marker'
        $qaChildSource = @'
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [Windows.Automation.AutomationElement]::FromHandle([IntPtr]::new(__HANDLE__))
$condition = [Windows.Automation.AndCondition]::new(
    [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, 'Extract all'),
    [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button))
$button = $root.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
if (-not $button -or $button.Current.IsOffscreen -or -not $button.Current.IsEnabled) { throw 'The observed Extract all button is unavailable.' }
$invoke = $null
if (-not $button.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref] $invoke)) { throw 'Extract all has no InvokePattern.' }
Write-Output 'Invoking the observed Explorer Extract all button.'
$invoke.Invoke()
Write-Output 'The Extract all invocation returned.'
$limit = [DateTime]::UtcNow.AddSeconds(100)
while ([DateTime]::UtcNow -lt $limit -and -not (Test-Path -LiteralPath '__COMPLETE__')) { Start-Sleep -Milliseconds 200 }
'@
        $qaChildSource = $qaChildSource.Replace('__HANDLE__', $qaExplorerHandle.ToInt64().ToString()).Replace('__COMPLETE__', $qaExtractionComplete.Replace("'", "''"))
        $qaEncoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($qaChildSource))
        $qaWindowsPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $qaVerbProcess = Start-Process -FilePath $qaWindowsPowerShell -ArgumentList ('-NoLogo -NoProfile -NonInteractive -STA -EncodedCommand ' + $qaEncoded) -PassThru `
            -RedirectStandardOutput (Join-Path $qaOutput 'extraction-control.stdout.txt') -RedirectStandardError (Join-Path $qaOutput 'extraction-control.stderr.txt')
        Wait-Until 'the actual compressed-folder extraction dialog' {
            $qaWindows = [Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children, [Windows.Automation.Condition]::TrueCondition)
            foreach ($qaWindow in $qaWindows) {
                if ($qaWindow.Current.IsOffscreen -or $qaWindow.Current.Name -notmatch '^Extract.*(Compressed|Zipped|Folder)') { continue }
                $script:qaExtractionDialog = $qaWindow
                return $true
            }
            return $false
        } 30
        Save-PublicationControls $qaExtractionDialog 'extraction-dialog-controls'
        # This native wizard exposes its Edit and CCPushButton as raw Panes
        # without Value/Invoke patterns. Use the identifiers actually observed.
        $qaEdits = @($qaExtractionDialog.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition) |
            Where-Object { -not $_.Current.IsOffscreen -and $_.Current.IsEnabled -and
                $_.Current.ControlType -eq [Windows.Automation.ControlType]::Pane -and
                $_.Current.ClassName -ceq 'Edit' -and $_.Current.AutomationId -ceq '1045' })
        if ($qaEdits.Count -ne 1) { throw 'The observed extraction destination field is unavailable.' }
        $qaExtracted = [IO.Path]::GetFullPath([string] $qaEdits[0].Current.Name)
        $qaExpectedDestination = [IO.Path]::GetFullPath((Join-Path $qaDownloads 'InspectionLog'))
        if ($qaExtracted -ine $qaExpectedDestination -or (Test-Path -LiteralPath $qaExtracted)) {
            throw 'The observed default extraction destination is not the new expected QA path.'
        }
        Write-Stage 'observed-extraction-destination' $qaExtracted
        $qaDialogHandle = [IntPtr] $qaExtractionDialog.Current.NativeWindowHandle
        Save-PublicationWindow $qaDialogHandle 'inspection_published_extract_dialog'
        # Read the button bounds after moving/capturing the actual dialog.
        $qaButtons = @($qaExtractionDialog.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition) |
            Where-Object { -not $_.Current.IsOffscreen -and $_.Current.IsEnabled -and
                $_.Current.ControlType -eq [Windows.Automation.ControlType]::Pane -and
                $_.Current.ClassName -ceq 'CCPushButton' -and $_.Current.AutomationId -ceq 'finishbutton' -and $_.Current.Name -ceq 'Extract' })
        if ($qaButtons.Count -ne 1) { throw 'The observed native Extract button is unavailable.' }
        $qaButtonBounds = $qaButtons[0].Current.BoundingRectangle
        $qaDialogBounds = $qaExtractionDialog.Current.BoundingRectangle
        if ($qaButtonBounds.Width -lt 10 -or $qaButtonBounds.Height -lt 10 -or -not $qaDialogBounds.Contains($qaButtonBounds) -or
            [InspectionNative]::GetForegroundWindow() -ne $qaDialogHandle) { throw 'The actual Extract button cannot be safely clicked.' }
        $qaClickX = [int] [Math]::Round($qaButtonBounds.Left + $qaButtonBounds.Width / 2)
        $qaClickY = [int] [Math]::Round($qaButtonBounds.Top + $qaButtonBounds.Height / 2)
        [PublicationPointer]::Click($qaDialogHandle, $qaClickX, $qaClickY)
        Write-Stage 'observed-extract-button-clicked' ([ordered]@{
            name = 'Extract'; id = 'finishbutton'; class = 'CCPushButton'; x = $qaClickX; y = $qaClickY; bounds = [string] $qaButtonBounds
        })
        $qaWorking = Join-Path $qaExtracted $qaArchiveRoot
        Wait-Until 'all 15 files extracted by the actual Explorer dialog' {
            if (-not (Test-Path -LiteralPath $qaWorking)) { return $false }
            $null = Assert-PublishedFiles $qaWorking -ExactFileCount
            return $true
        } 45
        New-Item -ItemType File -Path $qaExtractionComplete -Force | Out-Null
        $qaState.manualExtractionVerified = $true
        $qaState['extraction'] = [ordered]@{ method = 'Observed Explorer Extract all button and actual Extract dialog'; destination = $qaExtracted; observedVerb = $qaVerbName }
    }
    catch {
        # Preserve functional verification even if this runner changes its
        # Explorer UI; that fallback does not claim a manual extraction image.
        Write-Stage 'explorer-extraction-unavailable' $_.Exception.Message
        $qaState.manualExtractionVerified = $false
        try { Save-PublicationControls ([Windows.Automation.AutomationElement]::RootElement) 'extraction-failure-controls' }
        catch { Write-Stage 'extraction-failure-diagnostic-error' $_.Exception.Message }
        if ($qaVerbProcess -and -not $qaVerbProcess.HasExited) { Stop-Process -Id $qaVerbProcess.Id -Force }
        $qaExtracted = Join-Path $qaBase 'command-extracted'
        Expand-Archive -LiteralPath $qaZipPath -DestinationPath $qaExtracted
        $qaWorking = Join-Path $qaExtracted $qaArchiveRoot
        $qaState['extraction'] = [ordered]@{ method = 'Expand-Archive fallback; manual Explorer extraction not verified'; destination = $qaExtracted }
    }
    $qaState['publishedFiles'] = @(Assert-PublishedFiles $qaWorking -ExactFileCount)
    Write-Stage 'extracted-content-verified' $qaState.publishedFiles
    $qaExplorer = Open-PublicationFolder $qaWorking
    Save-PublicationWindow ([IntPtr] ([long] $qaExplorer.HWND)) 'inspection_published_extracted_folder' 1000 680
    Save-PublicationControls ([Windows.Automation.AutomationElement]::FromHandle([IntPtr] ([long] $qaExplorer.HWND))) 'extracted-folder-controls'

    $qaVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $qaInstances = & $qaVswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -version '[18.0,19.0)' -format json -utf8 | ConvertFrom-Json
    $qaInstance = @($qaInstances | Where-Object { Test-Path -LiteralPath (Join-Path $_.installationPath 'Common7\Tools\VsDevCmd.bat') }) | Select-Object -First 1
    if (-not $qaInstance) { throw 'Preinstalled Visual Studio 2026 C++ tools are unavailable.' }
    $qaDeveloper = Join-Path $qaInstance.installationPath 'Common7\Tools\VsDevCmd.bat'
    Write-Stage 'preinstalled-toolchain' ([ordered]@{ path = $qaInstance.installationPath; version = $qaInstance.installationVersion; product = $qaInstance.productId })
    $qaReadme = [IO.File]::ReadAllText((Join-Path $qaWorking 'README.md'))
    $qaFirstBatch = [regex]::Match($qaReadme, '(?s)```bat\r?\n(.*?)```')
    if (-not $qaFirstBatch.Success) { throw 'The published README has no construction block.' }
    $qaPlan = @(
        @{ name = 'application'; sources = @('modelo.cpp', 'informe.cpp', 'archivos.cpp', 'main.cpp'); exe = 'inspectionlog.exe' },
        @{ name = 'model'; sources = @('modelo.cpp', 'prueba_modelo.cpp'); exe = 'prueba_modelo.exe' },
        @{ name = 'tests'; sources = @('modelo.cpp', 'informe.cpp', 'archivos.cpp', 'pruebas.cpp'); exe = 'pruebas.exe' },
        @{ name = 'variant-application'; sources = @('modelo.cpp', 'informe_variante.cpp', 'archivos.cpp', 'main.cpp'); exe = 'inspectionlog_variante.exe' },
        @{ name = 'variant-tests'; sources = @('modelo.cpp', 'informe_variante.cpp', 'archivos.cpp', 'pruebas_variante.cpp'); exe = 'pruebas_variante.exe' }
    )
    $qaExpectedCommands = @('mkdir resultados', 'mkdir pruebas_tmp') + @($qaPlan | ForEach-Object { 'cl /W4 /EHsc ' + ($_.sources -join ' ') + ' /Fe:' + $_.exe })
    if ($qaFirstBatch.Groups[1].Value.Replace([string][char]13, '').Trim() -cne ($qaExpectedCommands -join [char]10)) {
        throw 'The reproduction commands differ from the published README.'
    }
    New-Item -ItemType Directory -Path (Join-Path $qaWorking 'resultados'), (Join-Path $qaWorking 'pruebas_tmp') | Out-Null
    foreach ($qaTarget in $qaPlan) { Build-InspectionTarget ('published-build-' + $qaTarget.name) $qaTarget.sources $qaTarget.exe }
    Invoke-PublishedConsole 'inspection_published_application' 'inspectionlog.exe' @('ejemplos\base.txt', 'resultados\base_guardada.txt')
    Invoke-PublishedConsole 'inspection_published_model' 'prueba_modelo.exe'
    Invoke-PublishedConsole 'inspection_published_tests' 'pruebas.exe'
    Invoke-PublishedConsole 'inspection_published_variant_application' 'inspectionlog_variante.exe' @('ejemplos\base.txt', 'resultados\base_variante.txt')
    Invoke-PublishedConsole 'inspection_published_variant_tests' 'pruebas_variante.exe'
    foreach ($qaSavedName in @('base_guardada.txt', 'base_variante.txt')) {
        $qaSaved = Join-Path (Join-Path $qaWorking 'resultados') $qaSavedName
        if ([IO.File]::ReadAllText($qaSaved) -cne [IO.File]::ReadAllText((Join-Path $qaWorking 'ejemplos\base.txt'))) { throw ('The downloaded program did not preserve its fixture: ' + $qaSavedName) }
        Copy-Item -LiteralPath $qaSaved -Destination (Join-Path $qaOutput $qaSavedName)
    }
    $null = Assert-PublishedFiles $qaWorking
    $qaState.status = 'passed'
    Write-Stage 'published-reproduction-completed' 'Five README builds and five normal executions completed from the hash-verified downloaded source; source files were not edited.'
}
catch {
    $qaState.status = 'failed'; $qaState['error'] = $_.Exception.Message; $qaState['scriptStack'] = $_.ScriptStackTrace
    Write-Provenance
    throw
}
finally {
    if ($qaVerbProcess -and -not $qaVerbProcess.HasExited) { Stop-Process -Id $qaVerbProcess.Id -Force }
    $qaState['finishedAtUtc'] = [DateTime]::UtcNow.ToString('o')
    Write-Provenance
    Stop-Transcript | Out-Null
}
