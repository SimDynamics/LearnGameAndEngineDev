[CmdletBinding()]
param(
    [ValidateSet('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')]
    [string] $Preset = 'windows-x64-debug',
    [switch] $Fresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Native Windows integration QA of local-variable and input dependencies:
# real lesson loaders, completion receipts, isolated StateStore and MSVC runs.
# Neither this script nor its tests touch the learner's stored progress.
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ((Split-Path -Leaf $root) -cne 'LearnGameAndEngineDev-Codex') {
    throw "Run from the authorized LearnGameAndEngineDev-Codex checkout: $root"
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The local-variable StateStore integration requires Windows.'
}
if ($env:GITHUB_ACTIONS -eq 'true') {
    throw 'Run this physical Windows QA locally, not on a hosted CI runner.'
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
        throw "Pinned Windows CTest was not found: $ctest"
    }
    $activeBranch = @(& $git -C $root symbolic-ref --quiet --short HEAD)
    if ($LASTEXITCODE -ne 0 -or $activeBranch.Count -ne 1 -or
            $activeBranch[0] -cne 'Codex') {
        throw 'Native local-variable QA requires the current Codex branch.'
    }

    # Compile the focused Win32 integration tests and their shared
    # production dependencies, including the reader used by the view tests.
    $buildArgs = @{
        Preset = $Preset
        Profile = 'Codex'
        SkipTests = $true
        Fresh = [bool]$Fresh
        Target = 'lgaed_local_variable_state_qa_targets'
    }
    & (Join-Path $PSScriptRoot 'Build-Native.ps1') @buildArgs

    $expected = @('native.local_int_state', 'native.local_string_state',
        'native.input_probe', 'native.input_dependency_state', 'native.input_dependency_authoring',
        'native.continued_input_dependency_state', 'native.local_role_probe',
        'native.current_u2l10_local_role_state', 'native.local_role_authoring',
        'native.local_dependency_receipts', 'native.local_dependency_view_authoring',
        'native.local_dependency_view_u2l1', 'native.local_dependency_view_u2l2',
        'native.local_dependency_view_u2l3', 'native.local_dependency_view_u2l7',
        'native.local_dependency_view_u2l8', 'native.local_dependency_view_u2l9',
        'native.local_dependency_view_u2l10', 'native.local_dependency_view_u3l1',
        'native.local_dependency_view_u3l2')
    $regex = '^native[.](local_(int|string)_state|input_(probe|dependency_state|dependency_authoring)|continued_input_dependency_state|local_role_(probe|authoring)|current_u2l10_local_role_state|local_dependency_(receipts|view_(authoring|u[23]l[0-9]+)))$'
    Push-Location -LiteralPath $root
    try {
        $json = & $ctest --preset $Preset --show-only=json-v1 -R $regex
        $inventoryExit = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($inventoryExit -ne 0) {
        throw 'CTest failed to enumerate local-variable integration tests.'
    }
    $inventory = ($json -join [Environment]::NewLine) | ConvertFrom-Json
    $found = @($inventory.tests | ForEach-Object { [string] $_.name })
    if ($found.Count -ne $expected.Count) {
        throw "Expected $($expected.Count) focused tests; found: $($found -join ', ')"
    }
    foreach ($name in $expected) {
        if ($found -cnotcontains $name) {
            throw "Missing native local-variable integration test: $name"
        }
    }

    $logs = Join-Path $root "out\build\$Preset\logs"
    [void][System.IO.Directory]::CreateDirectory($logs)
    $stdoutPath = Join-Path $logs 'local-variable-state.stdout.log'
    $stderrPath = Join-Path $logs 'local-variable-state.stderr.log'
    $process = $null
    try {
        $options = @{
            FilePath = $ctest
            ArgumentList = @(
                '--preset', $Preset, '--tests-regex', $regex,
                '--output-on-failure', '--no-tests=error',
                '--timeout', '900', '-j', '1'
            )
            WorkingDirectory = $root
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

    $stdout = [string](Get-Content -LiteralPath $stdoutPath -Raw -Encoding UTF8)
    $stderr = [string](Get-Content -LiteralPath $stderrPath -Raw -Encoding UTF8)
    if ($stdout) { [Console]::Write($stdout) }
    if ($stderr) { [Console]::Error.Write($stderr) }
    if ($exitCode -ne 0) {
        throw "Local-variable Win32 integration failed ($exitCode). Logs: $stdoutPath and $stderrPath"
    }
    Write-Host "PASS native local-variable and input integration: sixteen Win32 editors, preserved receipts, core and authoring tests."
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $originalEnvironment[$name], 'Process')
    }
}

