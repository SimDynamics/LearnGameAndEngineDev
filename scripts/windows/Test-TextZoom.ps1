[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug',
    [switch] $Fresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Local physical Windows QA. Never use Work, a detached checkout or CI.
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ((Split-Path -Leaf $repositoryRoot) -cne 'LearnGameAndEngineDev-Codex') {
    throw "Use the authorized LearnGameAndEngineDev-Codex checkout: $repositoryRoot"
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Native RichEdit zoom QA requires Windows.'
}
if ($env:GITHUB_ACTIONS -eq 'true') {
    throw 'Run native zoom QA locally on the authorized Windows PC, not in CI.'
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

try {
    $tools = & (Join-Path $PSScriptRoot 'Resolve-NativeTools.ps1')
    $git = [string] $tools.Git
    $ctest = [string] $tools.CTest
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
        throw "CTest was not found in the authorized toolchain: $ctest"
    }
    $activeBranch = @(& $git -C $repositoryRoot symbolic-ref --quiet --short HEAD)
    if ($LASTEXITCODE -ne 0 -or $activeBranch.Count -ne 1 -or
            $activeBranch[0] -cne 'Codex') {
        throw 'Native zoom QA requires an active Codex branch before compiling.'
    }

    # Compile six native test hosts, not the application executable.
    & (Join-Path $PSScriptRoot 'Build-Native.ps1') -Preset $Preset -Profile Codex -SkipTests -Fresh:$Fresh -Target lgaed_text_zoom_qa_targets

    $expected = @(
        'native.rich_text',
        'native.cpp_fill_view',
        'native.order_code_view',
        'native.math_puzzle_view',
        'native.simulation_view',
        'native.simulation_lesson'
    )
    $regex = '^native[.](rich_text|cpp_fill_view|order_code_view|math_puzzle_view|simulation_view|simulation_lesson)$'

    # Do not credit incomplete discovery as a successful regression.
    $json = & $ctest --preset $Preset --show-only=json-v1 -R $regex
    if ($LASTEXITCODE -ne 0) {
        throw 'CTest failed to enumerate native zoom regressions.'
    }
    $inventory = ($json -join [Environment]::NewLine) | ConvertFrom-Json
    $discovered = @($inventory.tests | ForEach-Object { [string] $_.name })
    if ($discovered.Count -ne $expected.Count) {
        throw "Expected six zoom tests, discovered $($discovered.Count): $($discovered -join ', ')"
    }
    foreach ($name in $expected) {
        if ($discovered -cnotcontains $name) {
            throw "Missing native zoom regression: $name"
        }
    }

    $logs = Join-Path $repositoryRoot "out\build\$Preset\logs"
    [void] [System.IO.Directory]::CreateDirectory($logs)
    $stdoutPath = Join-Path $logs 'text-zoom-qa.stdout.log'
    $stderrPath = Join-Path $logs 'text-zoom-qa.stderr.log'
    $arguments = @(
        '--preset', $Preset, '--tests-regex', $regex,
        '--output-on-failure', '--no-tests=error', '-j', '1'
    )
    $process = $null
    try {
        $options = @{
            FilePath = $ctest
            ArgumentList = $arguments
            WorkingDirectory = $repositoryRoot
            WindowStyle = 'Hidden'
            PassThru = $true
            RedirectStandardOutput = $stdoutPath
            RedirectStandardError = $stderrPath
        }
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
        throw "Native text zoom QA failed ($exitCode). Logs: $stdoutPath and $stderrPath"
    }

    Write-Host "Local Windows RichEdit zoom QA: six focused tests passed ($Preset)."
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $originalEnvironment[$name], 'Process')
    }
}
