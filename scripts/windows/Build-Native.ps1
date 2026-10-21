[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug',
    [ValidateSet('Work', 'Codex')]
    [string] $Profile,
    [switch] $SkipTests,
    [switch] $Fresh,
    [switch] $CleanFirst
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

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere is required.' }
$vsRoot = & $vswhere -latest -version '[18.0,19.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Visual Studio 2026 with C++ x64 tools was not found.' }
$vsRoot = "$vsRoot".Trim()
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
foreach ($requiredFile in @($vcvars, $cmake, $ctest,
        (Join-Path $vsRoot 'VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe'))) {
    if (-not (Test-Path -LiteralPath $requiredFile)) { throw "Required tool is missing: $requiredFile" }
}

# cmd receives only quoted, checked installation/source paths and validated parameters.
# vcvarsall changes this child process, never the PC's persistent environment.
foreach ($commandPath in @($sourceRoot, $vcvars, $cmake, $ctest)) {
    if ($commandPath -match '["%!?&|<>^\r\n]') { throw "Unsupported command metacharacter in path: $commandPath" }
}
$freshArgument = if ($Fresh) { '--fresh ' } else { '' }
$cleanArgument = if ($CleanFirst) { ' --clean-first' } else { '' }
$commands = @(
    'chcp 65001 >nul',
    "call `"$vcvars`" x64 10.0.26100.0 -vcvars_ver=14.51.36231",
    "`"$cmake`" ${freshArgument}--preset $Preset -DLGAED_DEFAULT_PROFILE=$Profile",
    "`"$cmake`" --build --preset $Preset$cleanArgument"
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
        -WorkingDirectory $sourceRoot -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput $standardOutputFile -RedirectStandardError $standardErrorFile
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
