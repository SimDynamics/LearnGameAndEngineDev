[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug',
    [switch] $Fresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Only the user's authorized Windows Codex checkout may run this QA.
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ((Split-Path -Leaf $repositoryRoot) -cne 'LearnGameAndEngineDev-Codex') {
    throw "Use the authorized LearnGameAndEngineDev-Codex checkout: $repositoryRoot"
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The native general-choice reader requires the authorized Windows PC.'
}
if ($env:GITHUB_ACTIONS -eq 'true') {
    throw 'Run editorial choice QA locally, not in GitHub Actions.'
}

# Native tool discovery may change this process's environment temporarily.
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
    $currentBranch = @(& $git -C $repositoryRoot symbolic-ref --quiet --short HEAD)
    if ($LASTEXITCODE -ne 0 -or $currentBranch.Count -ne 1 -or
            $currentBranch[0] -cne 'Codex') {
        throw 'General-choice editorial QA requires Codex, not main or detached HEAD.'
    }

    # Only the two real native QA hosts and their pinned U9/U2L10 fixtures.
    & (Join-Path $PSScriptRoot 'Build-Native.ps1') -Preset $Preset -Profile Codex -SkipTests -Fresh:$Fresh -Target lgaed_general_choice_editorial_qa_targets

    $expected = @(
        'native.general_choice_editorial_authoring',
        'native.general_choice_editorial',
        'native.general_choice_editorial_reader',
        'native.general_choice_prose_authoring',
        'native.general_choice_prose_history'
    )
    $regex = '^native[.]general_choice_(editorial(_authoring|_reader)?|prose_(authoring|history))$'

    # CTest resolves presets from the CURRENT working directory. The script
    # also works when called by absolute path from another folder.
    Push-Location -LiteralPath $repositoryRoot
    try {
        $json = & $ctest --preset $Preset --show-only=json-v1 -R $regex
        $enumerationExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($enumerationExitCode -ne 0) {
        throw 'Could not enumerate the focused Windows choice editorial tests.'
    }
    $inventory = ($json -join [Environment]::NewLine) | ConvertFrom-Json
    $discovered = @($inventory.tests | ForEach-Object { [string] $_.name })
    if ($discovered.Count -ne $expected.Count) {
        throw "Expected 5 choice editorial tests, got $($discovered.Count): $($discovered -join ', ')"
    }
    foreach ($name in $expected) {
        if ($discovered -cnotcontains $name) {
            throw "Missing required choice editorial test: $name"
        }
    }

    $logs = Join-Path $repositoryRoot "out\build\$Preset\logs"
    [void] [System.IO.Directory]::CreateDirectory($logs)
    $stdoutPath = Join-Path $logs 'general-choice-editorial-qa.stdout.log'
    $stderrPath = Join-Path $logs 'general-choice-editorial-qa.stderr.log'
    $arguments = @(
        '--preset', $Preset, '--tests-regex', $regex,
        '--output-on-failure', '--no-tests=error', '--timeout', '250', '-j', '1'
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
        throw "Choice editorial QA failed ($exitCode). Logs: $stdoutPath and $stderrPath"
    }
    Write-Host "Local Windows editorial choice QA: 5 focused tests passed ($Preset)."
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $originalEnvironment[$name], 'Process')
    }
}
