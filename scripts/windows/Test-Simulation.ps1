[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug',
    [switch] $Fresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Run only from the user's authorized Windows Codex checkout.
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ((Split-Path -Leaf $repositoryRoot) -cne 'LearnGameAndEngineDev-Codex') {
    throw "Expected the authorized LearnGameAndEngineDev-Codex checkout: $repositoryRoot"
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Native simulation QA must run on Windows.'
}

# The resolver modifies only this PowerShell process. Restore even when the
# checkout is wrong, a compiler is missing, or CTest reports a failure.
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
    $ctest = [string] $tools.CTest
    $git = [string] $tools.Git
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
        throw "Windows CTest is unavailable: $ctest"
    }

    # Validate branch before configure/build, not just the checkout folder.
    # The user controls merges and publication through their own buttons.
    $currentBranch = @(& $git -C $repositoryRoot symbolic-ref --quiet --short HEAD)
    if ($LASTEXITCODE -ne 0 -or $currentBranch.Count -ne 1 -or
            $currentBranch[0] -cne 'Codex') {
        throw 'Native simulation QA requires the local Codex branch, not main or detached HEAD.'
    }

    # Build only the three native simulation executables, never the full app.
    & (Join-Path $PSScriptRoot 'Build-Native.ps1') -Preset $Preset -Profile Codex -SkipTests -Fresh:$Fresh -Target lgaed_simulation_qa_targets

    $expected = @(
        'native.simulation_model',
        'native.simulation_view',
        'native.simulation_lesson',
        'native.simulation_authoring'
    )
    $regex = '^native[.]simulation_(model|view|lesson|authoring)$'

    # Verify that no test was accidentally omitted or renamed.
    # CTest resolves presets relative to the caller's working directory.
    # The script can be launched from any folder; use this checkout only for
    # discovery and restore the caller's location even on enumeration failure.
    Push-Location -LiteralPath $repositoryRoot
    try {
        $json = & $ctest --preset $Preset --show-only=json-v1 -R $regex
        $enumerationExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($enumerationExitCode -ne 0) {
        throw 'Could not enumerate the focused simulation tests.'
    }
    $parsed = ($json -join [Environment]::NewLine) | ConvertFrom-Json
    $discovered = @($parsed.tests | ForEach-Object { [string] $_.name })
    if ($discovered.Count -ne $expected.Count) {
        throw "Expected 4 simulation tests, found $($discovered.Count): $($discovered -join ', ')"
    }
    foreach ($test in $expected) {
        if ($discovered -cnotcontains $test) {
            throw "A required simulation test is missing: $test"
        }
    }

    $logs = Join-Path $repositoryRoot "out\build\$Preset\logs"
    [void] [System.IO.Directory]::CreateDirectory($logs)
    $stdoutPath = Join-Path $logs 'simulation-qa.stdout.log'
    $stderrPath = Join-Path $logs 'simulation-qa.stderr.log'

    # CTest executes local Win32 tests serially; the test host stays hidden.
    # No personal profile, foreground input or GitHub Actions are involved.
    $process = $null
    try {
        $arguments = @('--preset', $Preset, '--tests-regex', $regex,
                       '--output-on-failure', '--no-tests=error', '-j', '1')
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

    # Share the completed logs with any inherited helper handles; preserve text verbatim.
    $stdout = [string] (Get-Content -LiteralPath $stdoutPath -Raw -Encoding UTF8)
    $stderr = [string] (Get-Content -LiteralPath $stderrPath -Raw -Encoding UTF8)
    if ($stdout) { [Console]::Write($stdout) }
    if ($stderr) { [Console]::Error.Write($stderr) }
    if ($exitCode -ne 0) {
        throw "Simulation QA failed ($exitCode). Logs: $stdoutPath and $stderrPath"
    }
    Write-Host "Local Windows simulation QA: 4 focused tests passed ($Preset)."
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $originalEnvironment[$name], 'Process')
    }
}
