[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Source-only MSVC evidence on the authorized native Windows Codex checkout.
# The student's files and profile, Work checkout, and application are untouched.
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ((Split-Path -Leaf $root) -cne 'LearnGameAndEngineDev-Codex') {
    throw "Use the authorized LearnGameAndEngineDev-Codex checkout: $root"
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Local variable dependency QA requires native Windows.'
}
if ($env:GITHUB_ACTIONS -eq 'true') {
    throw 'Use the existing hosted workflow in CI; this entry point is for the local Windows checkout.'
}

$environmentNames = @(
    'JAVA_HOME', 'LGAED_JAVA_HOME', 'LGAED_PYTHON_EXECUTABLE',
    'LGAED_FFMPEG_EXECUTABLE', 'VULKAN_SDK', 'PATH'
)
$originalEnvironment = @{}
foreach ($name in $environmentNames) {
    $originalEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Quote-CmdArg([string] $value) {
    # cmd.exe is used ONLY with resolved physical paths and literal flags.
    # Never pass script content or a student's source through a shell string.
    if ([string]::IsNullOrWhiteSpace($value) -or
            $value -match '["%!?&|<>^\r\n]') {
        throw "Unsupported command argument: $value"
    }
    return '"' + $value + '"'
}

try {
    $tools = & (Join-Path $PSScriptRoot 'Resolve-NativeTools.ps1')
    $git = [string] $tools.Git
    $python = [string] $tools.Python
    $vcvars = [string] $tools.VcVars
    $toolset = [string] $tools.MSVCToolset
    $sdk = [string] $tools.WindowsSdk
    $cl = Join-Path ([string] $tools.VsRoot) "VC\Tools\MSVC\$toolset\bin\Hostx64\x64\cl.exe"
    if ($toolset -notmatch '^[0-9.]+$' -or $sdk -notmatch '^[0-9.]+$' -or
            -not (Test-Path -LiteralPath $cl -PathType Leaf)) {
        throw 'The pinned MSVC toolset or Windows SDK could not be resolved.'
    }
    $activeBranch = @(& $git -C $root symbolic-ref --quiet --short HEAD)
    if ($LASTEXITCODE -ne 0 -or $activeBranch.Count -ne 1 -or
            $activeBranch[0] -cne 'Codex') {
        throw 'Local variable dependency QA requires the active Codex branch.'
    }

    $native = Join-Path $root 'native'
    $sources = Join-Path $native 'core'
    $driverDirectory = Join-Path $root 'scripts\curriculum'
    $build = Join-Path $root "out\build\$Preset\local-variable-qa"
    $logs = Join-Path $root "out\build\$Preset\logs"
    [void] [System.IO.Directory]::CreateDirectory($build)
    [void] [System.IO.Directory]::CreateDirectory($logs)
    $stdoutPath = Join-Path $logs 'local-variable-qa.stdout.log'
    $stderrPath = Join-Path $logs 'local-variable-qa.stderr.log'

    $flags = '/nologo /std:c++latest /EHsc /W4 /WX /permissive- /utf-8'
    $compile = {
        param([string] $sourceFile, [string] $target)
        $executable = Join-Path $build "$target.exe"
        $object = Join-Path $build "$target.obj"
        ('{0} {1} /I {2} {3} {4} {5}' -f
            (Quote-CmdArg $cl), $flags, (Quote-CmdArg $native),
            (Quote-CmdArg (Join-Path $sources $sourceFile)),
            (Quote-CmdArg "/Fe:$executable"), (Quote-CmdArg "/Fo:$object"))
    }

    $intUnit = Join-Path $build 'local-int-probe-tests.exe'
    $textUnit = Join-Path $build 'local-string-probe-tests.exe'
    $intEmitter = Join-Path $build 'local-int-fixtures.exe'
    $textEmitter = Join-Path $build 'local-string-fixtures.exe'

    # Both Python drivers compile every generated original and mutation under
    # this same child vcvars environment. They enforce 55 integer and 44 string
    # private cases, including deliberately incorrect outputs.
    $commands = @(
        'chcp 65001 >nul',
        ('call {0} x64 {1} -vcvars_ver={2}' -f (Quote-CmdArg $vcvars), $sdk, $toolset),
        (& $compile 'cpp_local_int_probe_tests.cpp' 'local-int-probe-tests'),
        (Quote-CmdArg $intUnit),
        (& $compile 'cpp_local_string_probe_tests.cpp' 'local-string-probe-tests'),
        (Quote-CmdArg $textUnit),
        (& $compile 'cpp_local_int_live_fixtures.cpp' 'local-int-fixtures'),
        (& $compile 'cpp_local_string_live_fixtures.cpp' 'local-string-fixtures'),
        ('{0} -B {1} --emitter {2} --compiler MSVC' -f
            (Quote-CmdArg $python),
            (Quote-CmdArg (Join-Path $driverDirectory 'test_local_int_live_compilation.py')),
            (Quote-CmdArg $intEmitter)),
        ('{0} -B {1} --emitter {2} --compiler MSVC' -f
            (Quote-CmdArg $python),
            (Quote-CmdArg (Join-Path $driverDirectory 'test_local_string_live_compilation.py')),
            (Quote-CmdArg $textEmitter))
    )
    $commandLine = $commands -join ' && '

    $options = @{
        FilePath = $env:ComSpec
        ArgumentList = ('/d /v:off /s /c "' + $commandLine + '"')
        WorkingDirectory = $root
        WindowStyle = 'Hidden'
        PassThru = $true
        RedirectStandardOutput = $stdoutPath
        RedirectStandardError = $stderrPath
    }
    $process = $null
    try {
        $process = Start-Process @options
        $processHandle = $process.Handle
        $process.WaitForExit()
        $process.Refresh()
        $exitCode = $process.ExitCode
    }
    finally {
        if ($null -ne $process) { $process.Dispose() }
    }

    $stdout = [string] (Get-Content -LiteralPath $stdoutPath -Raw -Encoding UTF8)
    $stderr = [string] (Get-Content -LiteralPath $stderrPath -Raw -Encoding UTF8)
    if ($stdout) { [Console]::Write($stdout) }
    if ($stderr) { [Console]::Error.Write($stderr) }
    if ($exitCode -ne 0) {
        throw "Local variable dependency QA failed ($exitCode). Logs: $stdoutPath and $stderrPath"
    }
    Write-Host "Local MSVC source QA passed: both probe units, 55 integer and 44 string compiled cases, plus two current U2L5 starters."
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $originalEnvironment[$name], 'Process')
    }
}
