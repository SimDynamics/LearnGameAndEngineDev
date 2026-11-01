[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug',
    [ValidateSet('Work', 'Codex')]
    [string] $Profile,
    [switch] $SkipTests,
    [switch] $Fresh,
    [switch] $CleanFirst,
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$')]
    [string] $Target
)

$ErrorActionPreference = 'Stop'
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Profile) {
    if ((Split-Path -Leaf $sourceRoot) -cmatch '^LearnGameAndEngineDev-(Work|Codex)$') {
        $Profile = $Matches[1]
    } else {
        throw 'Select -Profile Work or -Profile Codex for this checkout.'
    }
}

$originalToolEnvironment = @{}
foreach ($name in @('JAVA_HOME','LGAED_JAVA_HOME','LGAED_PYTHON_EXECUTABLE','LGAED_FFMPEG_EXECUTABLE','VULKAN_SDK','PATH')) {
    $originalToolEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
$tools = & (Join-Path $PSScriptRoot 'Resolve-NativeTools.ps1')
$vcvars = $tools.VcVars
$cmake = $tools.CMake
$ctest = $tools.CTest

# cmd receives only quoted, checked installation/source paths and validated parameters.
# vcvarsall changes this child process, never the PC's persistent environment.
foreach ($commandPath in @($sourceRoot, $vcvars, $cmake, $ctest)) {
    if ($commandPath -match '["%!?&|<>^\r\n]') { throw "Unsupported command metacharacter in path: $commandPath" }
}
$freshArgument = if ($Fresh) { '--fresh ' } else { '' }
$cleanArgument = if ($CleanFirst) { ' --clean-first' } else { '' }
$targetArgument = if ($Target) { " --target $Target" } else { '' }
$commands = @(
    'chcp 65001 >nul',
    "call `"$vcvars`" x64 $($tools.WindowsSdk) -vcvars_ver=$($tools.MSVCToolset)",
    "`"$cmake`" ${freshArgument}--preset $Preset -DLGAED_DEFAULT_PROFILE=$Profile",
    "`"$cmake`" --build --preset $Preset$cleanArgument$targetArgument"
)
if (-not $SkipTests) { $commands += "`"$ctest`" --preset $Preset" }
$commandLine = $commands -join ' && '
$logDirectory = Join-Path $sourceRoot "out\build\$Preset\logs"
[System.IO.Directory]::CreateDirectory($logDirectory) | Out-Null
$logFile = Join-Path $logDirectory 'build-native.log'

$standardOutputFile = Join-Path $logDirectory 'build-native.stdout.log'
$standardErrorFile = Join-Path $logDirectory 'build-native.stderr.log'
$buildProcess = $null
try {
    Write-Host "Native build: $Preset; development profile: $Profile"
    # Start-Process creates a NEW console when -NoNewWindow is absent. Keep it
    # hidden: chcp changes only that console, never the caller's or PC settings.
    # A real console also makes MSVC /showIncludes and CMake agree on UTF-8;
    # CREATE_NO_WINDOW can leave their encodings mismatched and lose dependencies.
    $buildProcess = Start-Process -FilePath $env:ComSpec `
        -ArgumentList ('/d /v:off /s /c "' + $commandLine + '"') `
        -WorkingDirectory $sourceRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $standardOutputFile -RedirectStandardError $standardErrorFile
    # Cache the native process handle before waiting; Windows PowerShell can
    # otherwise lose ExitCode when Start-Process is used without -Wait.
    $nativeProcessHandle = $buildProcess.Handle
    $buildProcess.WaitForExit()
    $buildExitCode = $buildProcess.ExitCode
    # File redirection preserves both streams without PowerShell reclassifying
    # native stderr as a terminating error before its exit code is available.
    $standardOutput = [System.IO.File]::ReadAllText($standardOutputFile, [System.Text.Encoding]::UTF8)
    $standardError = [System.IO.File]::ReadAllText($standardErrorFile, [System.Text.Encoding]::UTF8)
    [System.Console]::Write($standardOutput)
    [System.Console]::Error.Write($standardError)
    $logText = $standardOutput
    if ($standardError) { $logText += "`r`n--- stderr ---`r`n" + $standardError }
    [System.IO.File]::WriteAllText($logFile, $logText, (New-Object System.Text.UTF8Encoding($false)))
    if ($buildExitCode -ne 0) { throw "Native configure/build/test failed ($buildExitCode). See $logFile" }
} finally {
    if ($null -ne $buildProcess) { $buildProcess.Dispose() }
}
} finally {
    foreach ($name in $originalToolEnvironment.Keys) {
        $originalValue = $originalToolEnvironment[$name]
        if ($null -eq $originalValue) { $originalValue = [System.Management.Automation.Language.NullString]::Value }
        [Environment]::SetEnvironmentVariable($name, $originalValue, 'Process')
    }
}
