[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug',
    [switch] $Fresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Run only in the authorized Windows Codex checkout. Never use WSL or CI.
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ((Split-Path -Leaf $repositoryRoot) -cne 'LearnGameAndEngineDev-Codex') {
    throw "Expected LearnGameAndEngineDev-Codex, not $repositoryRoot"
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Curricular edition QA requires the authorized Windows PC.'
}

# Tool discovery changes process-local PATH. Restore it on all exit paths.
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
        throw "Windows CTest was not found: $ctest"
    }

    # Check the actual checkout before any configure/build action.
    $currentBranch = @(& $git -C $repositoryRoot symbolic-ref --quiet --short HEAD)
    if ($LASTEXITCODE -ne 0 -or $currentBranch.Count -ne 1 -or
            $currentBranch[0] -cne 'Codex') {
        throw 'Curricular edition QA requires the local Codex branch, not main or detached HEAD.'
    }

    # Prepare ONLY the isolated C++ test and its disposable course fixtures,
    # never the complete app or the Work checkout.
    & (Join-Path $PSScriptRoot 'Build-Native.ps1') -Preset $Preset -Profile Codex -SkipTests -Fresh:$Fresh -Target lgaed_course_edition_tests

    $expected = @(
        'native.ordinary_explanation_authoring',
        'native.course_edition_authoring',
        'native.course_edition'
    )
    $regex = '^native[.](ordinary_explanation_authoring|course_edition_authoring|course_edition)$'

    # Fail closed if a test was renamed, omitted or an unintended test matched.
    $json = & $ctest --preset $Preset --show-only=json-v1 -R $regex
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not enumerate the focused Windows curricular edition tests.'
    }
    $parsed = ($json -join [Environment]::NewLine) | ConvertFrom-Json
    $discovered = @($parsed.tests | ForEach-Object { [string] $_.name })
    if ($discovered.Count -ne $expected.Count) {
        throw "Expected 3 curricular QA tests, found $($discovered.Count): $($discovered -join ', ')"
    }
    foreach ($test in $expected) {
        if ($discovered -cnotcontains $test) {
            throw "A required curricular edition test is missing: $test"
        }
    }

    $logDirectory = Join-Path $repositoryRoot "out\build\$Preset\logs"
    [void] [System.IO.Directory]::CreateDirectory($logDirectory)
    $stdoutPath = Join-Path $logDirectory 'curriculum-edition-qa.stdout.log'
    $stderrPath = Join-Path $logDirectory 'curriculum-edition-qa.stderr.log'

    $process = $null
    try {
        # Serial tests use synthetic sources and an isolated QA profile.
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
        throw "Curricular edition QA failed ($exitCode). Logs: $stdoutPath and $stderrPath"
    }
    Write-Host "Local Windows curricular edition QA: 3 focused tests passed ($Preset)."
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $originalEnvironment[$name], 'Process')
    }
}
