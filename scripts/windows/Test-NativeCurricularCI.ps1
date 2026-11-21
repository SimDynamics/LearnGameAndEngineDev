[CmdletBinding()]
param(
    [ValidateSet('StateImport', 'NarrationPlayer', 'ReaderBuild', 'MathEditorial', 'NarrationWorker', 'U12Reader',
        'U11EditorialReader', 'CompilerHistoricalReader', 'U6DiagnosticReader')]
    [string] $Suite = 'StateImport',
    [ValidateSet('All', 'DeclarationsAssignments', 'Characters')]
    [string] $CompilerHistoryGroup = 'All',
    [ValidateSet('All', 'Remainder', 'AssignmentP3')]
    [string] $EditorialGroup = 'All',
    [switch] $VirtualAudio,
    [switch] $MsvcPreflightOnly
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

# This provisioning entry point is only for a disposable GitHub-hosted runner.
# Local application builds keep using Build-Native.ps1 and the existing tools.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Run this provisioning script only on a disposable GitHub-hosted Windows runner.'
}
if ($VirtualAudio -and $Suite -ne 'NarrationPlayer') {
    throw 'Evaluate the virtual audio endpoint with the real NarrationPlayer tests first.'
}
if ($CompilerHistoryGroup -ne 'All' -and $Suite -ne 'CompilerHistoricalReader') {
    throw 'A compiler-history selection requires the CompilerHistoricalReader suite.'
}
if ($EditorialGroup -ne 'All' -and $Suite -ne 'U11EditorialReader') {
    throw 'An editorial selection requires the U11EditorialReader suite.'
}
if ($MsvcPreflightOnly -and $Suite -notin @('StateImport', 'ReaderBuild')) {
    throw 'The early MSVC preflight is available only for hosted StateImport and ReaderBuild suites.'
}

$qaRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaManifest = Get-Content -LiteralPath (Join-Path $qaRoot 'toolchains\windows.versions.json') -Raw |
    ConvertFrom-Json
$qaSuiteConfig = switch ($Suite) {
    'StateImport' {
        @{
            Directory = 'state-import-ci'
            CMakeOption = 'LGAED_STATE_IMPORT_QA_ONLY'
            Targets = @('lgaed_state_tests', 'lgaed_stderr_exact_tests', 'lgaed_cpp_project_history_tests', 'lgaed_cpp_history_training_tests', 'lgaed_code_format_loader_tests', 'lgaed_u6_compilecheck_format_runner_tests', 'lgaed_compiler_case_order_state_tests', 'lgaed_compiler_case_revision_state_tests', 'lgaed_compiler_guidance_state_tests')
            Results = 'state-import'
            Tests = @(
                'native.cpp_project_history',
                'native.cpp_history_training',
                'native.code_format_loader',
                'native.u6_compilecheck_formatted_msvc',
                'native.remainder_published26_state_import',
                'native.remainder_published30_state_import',
                'native.u11_assignment_state_import',
                'native.compiler_contract_state_import',
                'native.compiler_case_order_state',
                'native.compiler_case_revision_state',
                'native.stderr_exact_contract',
                'native.math_matching_text_live',
                'native.general_choice_live',
                'native.identifier_rename_live',
                'native.indexed_unique_transfer_state_live',
                'native.defaulted_move_policy_state_live',
                'native.independent_copy_assignment_state_live',
                'native.move_assignment_invariant_state_live',
                'native.array_owner_read_state_live',
                'native.array_copy_move_state_live',
                'native.array_return_take_state_live',
                'native.polymorphic_destruction_state_live',
                'native.virtual_lifetime_state_live',
                'native.derived_preparation_state_live',
                'native.abstract_interface_state_live',
                'native.interface_collection_state_live',
                'native.polymorphic_clone_state_live',
                'native.dual_interface_state_live'
            )
        }
    }
    'NarrationPlayer' {
        @{
            Directory = 'narration-player-ci'
            CMakeOption = 'LGAED_NARRATION_PLAYER_QA_ONLY'
            Targets = @('lgaed_narration_player_tests')
            Results = 'narration-player'
            Tests = @(
                'native.narration_player_failure_diagnostic',
                'native.narration_player_rates',
                'native.narration_player_qa_path'
            )
        }
    }
    'ReaderBuild' {
        @{
            Directory = 'reader-build-ci'
            CMakeOption = 'LGAED_READER_BUILD_QA_ONLY'
            Targets = @(
                'lgaed_lesson_reader_tests',
                'lgaed_lesson_view_tests',
                'lgaed_lesson_tests',
                'lgaed_compiler_guidance_state_tests',
                'lgaed_code_gutter_wheel_tests',
                'lgaed_lesson_scroll_tests',
                'lgaed_diagnostic_path_tests',
                'lgaed_simulation_tests',
                'lgaed_simulation_view_tests',
                'lgaed_simulation_lesson_tests',
                'lgaed_u11_declarations_reader_tests',
                'lgaed_u6_diagnostic_flow_tests',
                'lgaed_cpp_project_view_tests',
                'lgaed_order_spacing_contract_tests',
                'lgaed_order_code_whitespace_view_tests',
                'lgaed_rich_text_tests',
                'lgaed_code_editor_viewport_tests',
                'lgaed_code_editor_tests',
                'lgaed_code_format_compat_tests',
                'lgaed_cpp_expression_repair_tests',
                'lgaed_rejection_diagnostic_facts_tests',
                'lgaed_named_rule_check_tests',
                'lgaed_named_rule_runtime_tests',
                'lgaed_cpp_cmake_runner_tests',
                'lgaed_cpp_runner_tests',
                'lgaed_u3_runtime_semantics_tests',
                'lgaed_premium_windows_tests',
                'lgaed_compiler_first_case_view_tests',
                'lgaed_cpp_history_training_tests',
                'lgaed_course_edition_tests',
                'lgaed_optional_prose_format_tests',
                'lgaed_semantic_retirement_view_tests',
                'lgaed_compiler_case_order_view_tests',
                'lgaed_catalog_tests',
                'lgaed_general_choice_editorial_tests',
                'lgaed_general_choice_editorial_view_tests',
                'lgaed_boolean_battery_check_tests',
                'lgaed_boolean_battery_runtime_tests',
                'lgaed_reduction_check_tests',
                'lgaed_reduction_runtime_tests',
                'lgaed_integer_battery_check_tests',
                'lgaed_integer_battery_runtime_tests',
                'lgaed_open_battery_check_tests',
                'lgaed_open_battery_runtime_tests'
            )
            Results = 'reader-build'
        }
    }
    'MathEditorial' {
        @{
            Directory = 'math-editorial-ci'
            CMakeOption = 'LGAED_READER_BUILD_QA_ONLY'
            Targets = @('lgaed_editorial_matching_view_tests')
            Results = 'math-editorial'
            Tests = @('native.assetcheck_prompt_editorial')
        }
    }
    'NarrationWorker' {
        @{
            Directory = 'narration-worker-ci'
            CMakeOption = 'LGAED_NARRATION_WORKER_QA_ONLY'
            Targets = @('lgaed_narration_worker_tests', 'lgaed_narration_player_tests')
            Results = 'narration-worker'
            Tests = @(
                'native.narration_worker',
                'native.narration_spanish_pcm',
                'native.simulation_narration',
                'native.narration_player_failure_diagnostic'
            )
        }
    }
    'U12Reader' {
        @{
            Directory = 'u12-reader-ci'
            CMakeOption = 'LGAED_U12_READER_QA_ONLY'
            Targets = @('lgaed_lesson_reader_tests')
            Results = 'u12-reader'
            Tests = @(
                'native.u12_composed_output_published17_reader_0',
                'native.u12_composed_output_published17_reader_1',
                'native.u12_composed_output_published17_reader_2'
            )
        }
    }
    'U11EditorialReader' {
        @{
            Directory = 'u11-editorial-reader-ci'
            CMakeOption = 'LGAED_U11_EDITORIAL_READER_QA_ONLY'
            Targets = @('lgaed_lesson_reader_tests')
            Results = 'u11-editorial-reader'
            Tests = @(
                'native.u11_assignment_published_reader_0',
                'native.u11_assignment_published_reader_1',
                'native.u11_assignment_published_reader_2',
                'native.u11_assignment_published_reader_3',
                'native.u11_assignment_published_reader_4',
                'native.u11_assignment_published_reader_5',
                'native.u11_remainder_published26_reader_0',
                'native.u11_remainder_published26_reader_1',
                'native.u11_remainder_published30_reader_0',
                'native.u11_remainder_published30_reader_1'
            )
        }
    }
    'CompilerHistoricalReader' {
        @{
            Directory = 'compiler-historical-reader-ci'
            CMakeOption = 'LGAED_COMPILER_HISTORICAL_READER_QA_ONLY'
            Targets = @('lgaed_u11_declarations_reader_tests')
            Results = 'compiler-historical-reader'
            Tests = @(
                'native.u11_declarations_historical_reader_0',
                'native.u11_declarations_historical_reader_1',
                'native.u11_assignments_historical_reader_0',
                'native.u11_assignments_historical_reader_1',
                'native.u11_constants_historical_reader_0',
                'native.u11_constants_historical_reader_1',
                'native.u11_session_historical_reader_0',
                'native.u11_session_historical_reader_1',
                'native.u11_session_historical_reader_2',
                'native.u11_session_historical_reader_3',
                'native.u12_characters_historical_reader_0',
                'native.u12_characters_historical_reader_1',
                'native.u12_characters_historical_reader_2',
                'native.u12_characters_historical_reader_3'
            )
        }
    }
    'U6DiagnosticReader' {
        @{
            Directory = 'u6-diagnostic-reader-ci'
            CMakeOption = 'LGAED_U6_DIAGNOSTIC_READER_QA_ONLY'
            Targets = @('lgaed_u6_diagnostic_flow_tests')
            Results = 'u6-diagnostic-reader'
            Tests = @(
                'native.u6_diagnostic_flow_declarations',
                'native.u6_diagnostic_flow_interfaces',
                'native.u6_diagnostic_flow_project'
            )
        }
    }
}
$qaBuild = Join-Path $qaRoot "out\qa\$($qaSuiteConfig.Directory)"
$qaBuildTargets = $qaSuiteConfig.Targets -join ' '
Write-Host "Native QA suite: $Suite"
$qaVsWhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) `
    'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $qaVsWhere -PathType Leaf)) {
    throw 'The hosted image does not contain the Visual Studio locator.'
}

function Get-PhysicalFileVersion([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required native tool is absent: $Path"
    }
    $info = (Get-Item -LiteralPath $Path).VersionInfo
    return '{0}.{1}.{2}.{3}' -f `
        $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart
}

$qaVsCandidates = @(& $qaVsWhere -products '*' `
    -version $qaManifest.visualStudio.versionRange `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the installed Visual Studio instances.' }
$qaVsRoot = $null
foreach ($candidate in $qaVsCandidates) {
    $candidateCompiler = Join-Path $candidate `
        "VC\Tools\MSVC\$($qaManifest.visualStudio.msvcToolset)\bin\Hostx64\x64\cl.exe"
    if ((Test-Path -LiteralPath $candidateCompiler -PathType Leaf) -and
        (Get-PhysicalFileVersion $candidateCompiler) -eq $qaManifest.visualStudio.compilerVersion) {
        $qaVsRoot = $candidate
        break
    }
}
if (-not $qaVsRoot) {
    # Diagnose the exact mismatch before refusing the build. A hosted image
    # can ship the requested toolset folder with a serviced cl.exe, or a
    # different MSVC toolset. Neither case authorizes a silent substitution.
    # This inventory is read-only and contains no student or profile paths.
    if ($qaVsCandidates.Count -eq 0) {
        Write-Host 'MSVC preflight: no Visual Studio 2026 instance with the required x64/x86 C++ tools was discovered.'
    }
    foreach ($candidate in $qaVsCandidates) {
        $toolsets = Join-Path $candidate 'VC\Tools\MSVC'
        $expected = Join-Path $toolsets "$($qaManifest.visualStudio.msvcToolset)\bin\Hostx64\x64\cl.exe"
        Write-Host "MSVC preflight: Visual Studio at $candidate"
        if (Test-Path -LiteralPath $expected -PathType Leaf) {
            $actual = Get-PhysicalFileVersion $expected
            Write-Host "MSVC preflight: requested toolset folder exists; cl.exe version $actual; expected $($qaManifest.visualStudio.compilerVersion)."
        } else {
            Write-Host "MSVC preflight: requested cl.exe path is absent: $expected"
        }
        if (Test-Path -LiteralPath $toolsets -PathType Container) {
            $available = @(Get-ChildItem -LiteralPath $toolsets -Directory -ErrorAction Stop |
                Sort-Object Name | Select-Object -ExpandProperty Name)
            Write-Host "MSVC preflight: available toolset directories: $($available -join ', ')"
        } else {
            Write-Host 'MSVC preflight: no VC\Tools\MSVC directory is installed.'
        }
    }
    throw "The image lacks MSVC $($qaManifest.visualStudio.msvcToolset) with cl.exe $($qaManifest.visualStudio.compilerVersion). No alternate toolset will be selected."
}
$qaVcVars = Join-Path $qaVsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $qaVcVars -PathType Leaf)) {
    throw 'The verified MSVC toolset has no x64 environment initializer.'
}
Write-Host "Image: $env:ImageOS $env:ImageVersion"
Write-Host "Verified MSVC: $($qaManifest.visualStudio.msvcToolset); cl.exe $($qaManifest.visualStudio.compilerVersion)"
if ($MsvcPreflightOnly) {
    # The full suite independently repeats this exact check before CMake/CTest.
    # A read-only initial pass avoids running lengthy source tests or obtaining
    # tools that cannot be used with the declared compiler. It neither changes
    # the toolchain pin nor selects a different MSVC installed on the runner.
    Write-Host 'Pinned MSVC preflight passed. No tools were installed and no native build or CTest execution was performed.'
    return
}

$qaSdkRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits\10'
$qaSdkVersion = $qaManifest.visualStudio.windowsSdk
$qaSdkMinimum = [version]$qaManifest.visualStudio.windowsSdkServicing
$qaSdkToolNames = @('rc.exe', 'mt.exe')
if ($VirtualAudio) { $qaSdkToolNames += 'signtool.exe' }
$qaSdkTools = $qaSdkToolNames | ForEach-Object {
    Join-Path $qaSdkRoot "bin\$qaSdkVersion\x64\$_"
}
$qaSdkReady = $true
foreach ($qaTool in $qaSdkTools) {
    if (-not (Test-Path -LiteralPath $qaTool -PathType Leaf)) {
        $qaSdkReady = $false
    } elseif ([version](Get-PhysicalFileVersion $qaTool) -lt $qaSdkMinimum) {
        $qaSdkReady = $false
    }
}
if (-not $qaSdkReady) {
    # The exact Microsoft release is listed at:
    # https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads
    # Keep its SDK feature selection aligned with the official runner recipe:
    # actions/runner-images/images/windows/scripts/build/Install-VisualStudio.ps1
    if ($qaSdkVersion -ne '10.0.28000.0' -or $qaSdkMinimum -ne [version]'10.0.28000.2957') {
        throw 'Update and verify the official SDK installer URL for the changed tools manifest.'
    }
    $qaInstaller = Join-Path $env:RUNNER_TEMP 'lgaed-winsdk-28000.2957.exe'
    $qaSdkUrl = 'https://download.microsoft.com/download/e6cfffdc-0d3e-4c6c-96d7-d491da459a07/KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/winsdksetup.exe'
    Invoke-WebRequest -Uri $qaSdkUrl -OutFile $qaInstaller
    $qaSignature = Get-AuthenticodeSignature -LiteralPath $qaInstaller
    if ($qaSignature.Status -ne 'Valid' -or
        $qaSignature.SignerCertificate.Subject -notmatch '(^|,\s*)(CN|O)=Microsoft Corporation(,|$)') {
        throw 'The SDK installer does not have a valid Microsoft signature.'
    }
    $qaSdkArguments = @(
        '/q', '/norestart', '/ceip', 'off', '/features',
        'OptionId.UWPManaged', 'OptionId.UWPCPP', 'OptionId.UWPLocalized',
        'OptionId.DesktopCPPx86', 'OptionId.DesktopCPPx64', 'OptionId.DesktopCPParm64'
    )
    $qaInstall = Start-Process -FilePath $qaInstaller -ArgumentList $qaSdkArguments -Wait -PassThru
    try {
        Write-Host "SDK installer exit code: $($qaInstall.ExitCode)"
        if ($qaInstall.ExitCode -notin @(0, 3010)) { throw 'The required Windows SDK installation failed.' }
    } finally {
        $qaInstall.Dispose()
    }
}
foreach ($qaTool in $qaSdkTools) {
    $qaActual = Get-PhysicalFileVersion $qaTool
    if ([version]$qaActual -lt $qaSdkMinimum) {
        throw "Windows SDK servicing is too old: $qaTool ($qaActual)."
    }
    Write-Host "Verified SDK tool: $qaTool ($qaActual)"
}

$qaCMake = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$qaCTest = Join-Path (Split-Path -Parent $qaCMake) 'ctest.exe'
$qaPython = (Get-Command python.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$qaGit = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$qaRuntimeOptions = ''
$qaRuntimePaths = @()
if ($Suite -in @('MathEditorial', 'NarrationWorker', 'U12Reader', 'U11EditorialReader',
    'CompilerHistoricalReader', 'U6DiagnosticReader')) {
    # This job uses a new checkout with no restored runtime or audio caches.
    # Existing QA/user data is never removed to manufacture a first generation.
    $qaResourceRoot = Join-Path $qaRoot 'out\qa\narration-ci-resources'
    foreach ($qaFreshPath in @(
        $qaBuild, $qaResourceRoot, (Join-Path $qaRoot 'out\narration'),
        (Join-Path $qaRoot 'out\qa\narration-native-tests'),
        (Join-Path $qaRoot 'out\qa\narration-worker-tests')
    )) {
        if (Test-Path -LiteralPath $qaFreshPath) {
            throw "Real worker CI requires a fresh destination; existing data is preserved: $qaFreshPath"
        }
    }
    New-Item -ItemType Directory -Path $qaBuild | Out-Null
    $qaNarrationTools = & (Join-Path $PSScriptRoot 'Prepare-NarrationCITools.ps1')
    $qaNarrationTools | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $qaBuild 'narration-tool-archives.json') -Encoding utf8
    $qaKokoro = Join-Path $qaResourceRoot 'kokoro'
    $qaEnglish = Join-Path $qaResourceRoot 'english'
    & $qaPython -B (Join-Path $PSScriptRoot 'prepare_narration_ci_resources.py') `
        --resources $qaKokoro --english-resources $qaEnglish 2>&1 |
        Tee-Object -FilePath (Join-Path $qaBuild 'narration-resources.log') | Write-Host
    if ($LASTEXITCODE -ne 0) { throw 'The authentic narration resources could not be prepared.' }
    $qaRuntimePaths = @($qaNarrationTools.JavaHome, $qaNarrationTools.FFmpeg, $qaKokoro, $qaEnglish)
    $qaRuntimeOptions = @(
        "`"-DLGAED_JAVA_HOME=$($qaNarrationTools.JavaHome)`"",
        "`"-DLGAED_FFMPEG_EXECUTABLE=$($qaNarrationTools.FFmpeg)`"",
        "`"-DLGAED_NARRATION_RESOURCES=$qaKokoro`"",
        "`"-DLGAED_ENGLISH_RESOURCES=$qaEnglish`""
    ) -join ' '
}
if ($Suite -eq 'MathEditorial') {
    $qaRuntimeOptions += ' -DLGAED_MATH_EDITORIAL_QA_ONLY=ON'
}
foreach ($qaPath in @($qaRoot, $qaBuild, $qaVcVars, $qaCMake, $qaCTest, $qaPython, $qaGit) + $qaRuntimePaths) {
    if ($qaPath -match '["%!?&|<>^;\r\n]') { throw "Unsupported command metacharacter in native QA path: $qaPath" }
}
# The disposable runner owns its own isolated dependencies; a CI test
# must never require or create the user's E:\ project dependency directory.
# CMake checks the actual Python/Git versions against the same manifest and
# retains the normal compiler, SDK, dependency and exact C++23 checks.
$qaCommands = @(
    '@echo off',
    'chcp 65001 >nul',
    "call `"$qaVcVars`" x64 $qaSdkVersion -vcvars_ver=$($qaManifest.visualStudio.msvcToolset)",
    'if errorlevel 1 exit /b %errorlevel%',
    "`"$qaCMake`" -S `"$qaRoot`" -B `"$qaBuild`" -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/WindowsToolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DLGAED_DEFAULT_PROFILE=Codex -D$($qaSuiteConfig.CMakeOption)=ON `"-DPython3_EXECUTABLE=$qaPython`" `"-DGIT_EXECUTABLE=$qaGit`" $qaRuntimeOptions",
    'if errorlevel 1 exit /b %errorlevel%'
)
if ($Suite -eq 'ReaderBuild') {
    # Check simulations and real code gestures before compiling the other
    # historical test translation units. Failed UI iterations retain their
    # native captures and diagnostics without repeating the older suites.
    $qaSimulationPattern = '^native\.(simulation_model|simulation_view|simulation_lesson|simulation_authoring)$'
    $qaGesturePattern = '^native\.(lesson_menu|lesson_scroll_render|lesson_optional_order_help)$'
    $qaCommands += @(
        "`"$qaCMake`" --build `"$qaBuild`" --target lgaed_simulation_tests lgaed_simulation_view_tests lgaed_simulation_lesson_tests --parallel 2",
        'if errorlevel 1 exit /b %errorlevel%',
        "`"$qaCTest`" --test-dir `"$qaBuild`" -R `"$qaSimulationPattern`" --verbose --output-on-failure --no-tests=error --output-junit `"$qaBuild\simulation-results.xml`"",
        'if errorlevel 1 exit /b %errorlevel%',
        "`"$qaCMake`" --build `"$qaBuild`" --target lgaed_lesson_view_tests --parallel 2",
        'if errorlevel 1 exit /b %errorlevel%',
        "`"$qaCTest`" --test-dir `"$qaBuild`" -R `"$qaGesturePattern`" --verbose --output-on-failure --no-tests=error --timeout 300 --output-junit `"$qaBuild\lesson-gesture-results.xml`"",
        'if errorlevel 1 exit /b %errorlevel%'
    )
}
$qaCommands += @(
    "`"$qaCMake`" --build `"$qaBuild`" --target $qaBuildTargets --parallel 2",
    'exit /b %errorlevel%'
)
$qaCommandFile = Join-Path $env:RUNNER_TEMP "lgaed-$($qaSuiteConfig.Results)-build.cmd"
[System.IO.File]::WriteAllLines($qaCommandFile, $qaCommands, [System.Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $qaBuild -Force | Out-Null
& $env:ComSpec /d /v:off /c $qaCommandFile 2>&1 |
    Tee-Object -FilePath (Join-Path $qaBuild 'native-build.log') | Write-Host
if ($LASTEXITCODE -ne 0) { throw "The verified Windows $Suite build or early regression checks failed." }

if ($Suite -eq 'ReaderBuild') {
    # The original reader/U6 executables are only compiled. The focused
    # catalog, compiler and Win32 regressions use isolated QA state and the
    # exact authored/historical inputs; no reader/audio playback runs here.
    $qaBuiltTargets = foreach ($qaTarget in $qaSuiteConfig.Targets) {
        $qaExecutable = Join-Path $qaBuild "$qaTarget.exe"
        if (-not (Test-Path -LiteralPath $qaExecutable -PathType Leaf)) {
            throw "The linked reader executable is missing: $qaExecutable"
        }
        $qaFile = Get-Item -LiteralPath $qaExecutable
        if ($qaFile.Length -eq 0) { throw "The linked reader executable is empty: $qaExecutable" }
        [PSCustomObject]@{
            Target = $qaTarget
            Bytes = $qaFile.Length
            SHA256 = (Get-FileHash -LiteralPath $qaExecutable -Algorithm SHA256).Hash
        }
    }
    $qaBuiltTargets | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $qaBuild 'reader-build-results.json') -Encoding utf8
    $qaBuiltTargets | Format-Table -AutoSize | Out-String | Write-Host
    # Preserve the curricular checks and include the real editor transaction regressions.
    $qaUiListingText = & $qaCTest --test-dir $qaBuild --show-only=json-v1
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the focused reader and catalog test registration.' }
    $qaUiListing = ($qaUiListingText -join "`n") | ConvertFrom-Json
    $qaUiNames = @($qaUiListing.tests | ForEach-Object { $_.name })
    # Check every registered native executable before running the long QA.
    # CTest can list a target even when that target was never built.
    $qaMissingTestExecutables = @(
        foreach ($qaRegisteredTest in $qaUiListing.tests) {
            $qaCommand = @($qaRegisteredTest.command)
            if ($qaCommand.Count -eq 0) { continue }
            $qaExecutable = [string] $qaCommand[0]
            if ($qaExecutable -match '[/\\]lgaed_[^/\\]+\.exe$' -and
                -not (Test-Path -LiteralPath $qaExecutable -PathType Leaf)) {
                "$($qaRegisteredTest.name): $qaExecutable"
            }
        }
    )
    if ($qaMissingTestExecutables.Count -gt 0) {
        Write-Host ("ReaderBuild registered tests without built executables: " +
            ($qaMissingTestExecutables -join '; '))
        throw 'ReaderBuild must build every native executable registered by CTest.'
    }
    # Keep this exact list synchronized with every add_test registered by
    # LGAED_READER_BUILD_QA_ONLY. New isolated tests cannot silently bypass
    # the build, and obsolete names cannot masquerade as passing regressions.
    $qaUiExpected = @(
        'native.simulation_model',
        'native.simulation_view',
        'native.simulation_lesson',
        'native.simulation_authoring',
        'native.code_gutter_wheel',
        'native.reader_interaction',
        'native.diagnostic_paths',
        'native.order_equivalence_authoring',
        'native.ordinary_explanation_authoring',
        'native.cpp_project_internal_dialogs',
        'native.cpp_history_training',
        'native.p2_u3_workflow_authoring',
        'native.cpp_project_u3_selective_individual',
        'native.cpp_project_u3_selective_lesson',
        'native.cpp_project_u3_original_gates',
        'native.cpp_project_u2_exact_eof',
        'native.cpp_project_u3_declaration',
        'native.cpp_project_u3_declared_selective',
        'native.cpp_project_u3_original_prerequisites',
        'native.cpp_project_u3_first_cmake',
        'native.cpp_project_u3_first_cmake_repair',
        'native.cpp_project_u3_real_history_gate',
        'native.cpp_project_u3_entire_original',
        'native.cpp_project_u3_cmake_structure',
        'native.cpp_project_other_cpp_multicase',
        'native.order_spacing_contract',
        'native.order_code_whitespace_view',
        'native.code_editor_viewport',
        'native.code_editor',
        'native.code_format_compat',
        'native.cpp_expression_repair',
        'native.rejection_diagnostic_facts',
        'native.named_rule_source',
        'native.named_rule_runtime',
        'native.cpp_cmake_graph',
        'native.u3_runtime_semantics',
        'native.premium_windows',
        'native.compiler_puzzles',
        'native.u1_editor_edition',
        'native.u1_editor_contracts',
        'native.u1_main_editor_execution',
        'native.u1_main_editor_contracts',
        'native.lesson_optional_order_help',
        'native.semantic_retirement_view',
        'native.compiler_case_order_view',
        'native.compiler_case_revision_view',
        'native.catalog',
        'native.math_matching_text_live',
        'native.general_choice_live',
        'native.identifier_rename_live',
        'native.indexed_unique_transfer_state_live',
        'native.defaulted_move_policy_state_live',
                'native.independent_copy_assignment_state_live',
                'native.move_assignment_invariant_state_live',
                'native.array_owner_read_state_live',
                'native.array_copy_move_state_live',
                'native.array_return_take_state_live',
        'native.polymorphic_destruction_state_live',
        'native.virtual_lifetime_state_live',
        'native.derived_preparation_state_live',
        'native.abstract_interface_state_live',
        'native.interface_collection_state_live',
        'native.polymorphic_clone_state_live',
        'native.dual_interface_state_live',
        'native.course_edition',
        'native.course_edition_authoring',
        'native.optional_prose_format',
        'native.general_choice_editorial_authoring',
        'native.general_choice_editorial',
        'native.general_choice_editorial_reader',
        'native.boolean_battery',
        'native.boolean_battery_runtime',
        'native.reduction',
        'native.reduction_runtime',
        'native.integer_battery',
        'native.integer_battery_runtime',
        'native.open_battery',
        'native.open_battery_authoring',
        'native.open_battery_runner',
        'native.open_battery_runtime',
        'native.rich_text_hit_geometry',
        'native.lesson_menu',
        'native.lesson_scroll_render'
    )
    $qaUiMissing = @($qaUiExpected | Where-Object { $_ -notin $qaUiNames })
    $qaUiUnexpected = @($qaUiNames | Where-Object { $_ -notin $qaUiExpected })
    if ($qaUiNames.Count -ne $qaUiExpected.Count -or
        $qaUiMissing.Count -gt 0 -or $qaUiUnexpected.Count -gt 0) {
        Write-Host ("ReaderBuild missing CTest registrations: " + ($qaUiMissing -join ', '))
        Write-Host ("ReaderBuild unexpected CTest registrations: " + ($qaUiUnexpected -join ', '))
        throw 'ReaderBuild must register independent native glyph hit geometry, console fullscreen/zoom, menu/scroll, Win32 History, spacing, fullscreen editor viewport, U3 semantic, code-format, Premium, first-case compiler, case-order/revision history, semantic retirement, catalog, general-choice editorial, boolean battery, stateful reduction, integer CMake battery, optional prose/format review and all four open-battery contracts and execution regressions.'
    }
    # Obtain CMake structure, U3 semantics and named-rule variation
    # evidence before the remaining compiler cases. A failure in one must
    # retain the other independent diagnostics.
    # Simulation and console gestures already ran before the larger build.
    # Keep the other focal checks and execute each registered test once.
    $qaFocalPattern = '^native\.(rich_text_hit_geometry|cpp_cmake_graph|cpp_project_u3_cmake_structure|u3_runtime_semantics|named_rule_runtime)$'
    $qaCompletedPattern = "($qaSimulationPattern)|($qaGesturePattern)|($qaFocalPattern)"
    # Keep an independent receipt for the five early checks even if one
    # crashes. Do not abandon the other registered tests or lower their gates.
    & $qaCTest --test-dir $qaBuild -R $qaFocalPattern --verbose --output-on-failure --no-tests=error --timeout 300 `
        --output-junit (Join-Path $qaBuild 'reader-build-focal.xml') `
        --output-log (Join-Path $qaBuild 'reader-build-focal.log')
    $qaFocalExit = $LASTEXITCODE
    if ($qaFocalExit -ne 0) {
        Write-Warning 'ReaderBuild focal regressions failed. Continuing independent cases without granting success.'
    }
    # A failed CMake graph, glyph or U3 focal test must not hide the remaining
    # catalog, compiler or Win32 regressions. Both groups retain independent
    # JUnit/log evidence, and either nonzero exit fails the entire workflow.
    & $qaCTest --test-dir $qaBuild -E $qaCompletedPattern --output-on-failure --no-tests=error --timeout 300 `
        --output-junit (Join-Path $qaBuild 'reader-build-regressions.xml') `
        --output-log (Join-Path $qaBuild 'reader-build-regressions.log')
    $qaRemainingExit = $LASTEXITCODE
    if ($qaFocalExit -ne 0 -or $qaRemainingExit -ne 0) {
        throw "ReaderBuild native regressions failed: focal exit=$qaFocalExit, remaining exit=$qaRemainingExit. See reader-build-focal.xml/log and reader-build-regressions.xml/log."
    }
    Write-Host 'PASS original reader/U6 executables compiled and linked; catalog, general-choice reading preservation, boolean battery execution/provenance, console fullscreen/zoom and Escape, actual Win32 History dialogs and focused published lesson execution/reopen/reset tested. No reader/audio playback executed.'
    return
}

$qaListingText = & $qaCTest --test-dir $qaBuild --show-only=json-v1
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the focused native test registration.' }
$qaListing = ($qaListingText -join "`n") | ConvertFrom-Json
$qaExpected = $qaSuiteConfig.Tests
$qaActualTests = @($qaListing.tests | ForEach-Object { $_.name })
if ($qaActualTests.Count -ne $qaExpected.Count -or
    (Compare-Object -ReferenceObject $qaExpected -DifferenceObject $qaActualTests)) {
    throw "The focused $Suite configuration must register exactly its $($qaExpected.Count) declared tests."
}

# Keep the complete suite registration intact. An explicit focal run
# validates its exact original names before using the same CTest filter.
$qaCTestSelection = @()
if ($CompilerHistoryGroup -ne 'All' -or $EditorialGroup -ne 'All') {
    if ($CompilerHistoryGroup -ne 'All') {
        $qaSelectionLabel = "compiler-history $CompilerHistoryGroup"
        $qaSelectedExpected = if ($CompilerHistoryGroup -eq 'DeclarationsAssignments') {
            @('native.u11_declarations_historical_reader_0', 'native.u11_declarations_historical_reader_1',
              'native.u11_assignments_historical_reader_0', 'native.u11_assignments_historical_reader_1')
        } else {
            @('native.u12_characters_historical_reader_0', 'native.u12_characters_historical_reader_1',
              'native.u12_characters_historical_reader_2', 'native.u12_characters_historical_reader_3')
        }
    } else {
        $qaSelectionLabel = "editorial $EditorialGroup"
        if ($EditorialGroup -eq 'AssignmentP3') {
            $qaSelectedExpected = @('native.u11_assignment_published_reader_2')
        } else {
            $qaSelectedExpected = @('native.u11_remainder_published26_reader_0', 'native.u11_remainder_published26_reader_1',
                'native.u11_remainder_published30_reader_0', 'native.u11_remainder_published30_reader_1')
        }
    }
    $qaSelectionPattern = '^(' + (($qaSelectedExpected | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
    $qaCTestSelection = @('-R', $qaSelectionPattern)
    $qaSelectedText = & $qaCTest --test-dir $qaBuild --show-only=json-v1 @qaCTestSelection
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the selected original reader tests.' }
    $qaSelectedListing = ($qaSelectedText -join "`n") | ConvertFrom-Json
    $qaSelectedActual = @($qaSelectedListing.tests | ForEach-Object { $_.name })
    if ($qaSelectedActual.Count -ne $qaSelectedExpected.Count -or
        (Compare-Object -ReferenceObject $qaSelectedExpected -DifferenceObject $qaSelectedActual)) {
        throw "The $qaSelectionLabel selection must contain exactly its $($qaSelectedExpected.Count) original tests."
    }
    Write-Host "Original reader selection: $qaSelectionLabel ($($qaSelectedActual -join ', '))"
}

if ($Suite -in @('NarrationPlayer', 'U12Reader', 'U11EditorialReader',
    'CompilerHistoricalReader', 'U6DiagnosticReader')) {
    # Observe the actual ephemeral machine. Starting an existing, enabled audio
    # service is preparation; the opt-in endpoint evaluation is a separate step.
    & {
        Write-Output "Windows audio environment: $([DateTime]::UtcNow.ToString('O'))"
        foreach ($qaServiceName in @('AudioEndpointBuilder', 'Audiosrv')) {
            $qaService = Get-Service -Name $qaServiceName -ErrorAction SilentlyContinue
            if ($null -eq $qaService) {
                Write-Output "Audio service absent: $qaServiceName"
                continue
            }
            $qaService | Select-Object Name, Status, StartType | Format-Table -AutoSize | Out-String
            if ($qaService.Status -eq 'Stopped' -and $qaService.StartType -ne 'Disabled') {
                Write-Output "Starting existing enabled service: $qaServiceName"
                $qaService.Start()
                $qaService.WaitForStatus('Running', [TimeSpan]::FromSeconds(15))
                $qaService.Refresh()
                $qaService | Select-Object Name, Status, StartType | Format-Table -AutoSize | Out-String
            }
        }
        try {
            $qaDevices = @(Get-CimInstance -ClassName Win32_SoundDevice -ErrorAction Stop)
            Write-Output "Win32_SoundDevice count: $($qaDevices.Count)"
            $qaDevices | Select-Object Name, Status, PNPDeviceID | Format-Table -AutoSize | Out-String
            $qaEndpoints = @(Get-PnpDevice -Class AudioEndpoint -PresentOnly -ErrorAction Stop)
            Write-Output "Present AudioEndpoint device count: $($qaEndpoints.Count)"
            $qaEndpoints | Select-Object FriendlyName, Status, InstanceId | Format-Table -AutoSize | Out-String
        } catch {
            # Inventory is diagnostic, not a replacement for the real player.
            Write-Output "Audio device inventory unavailable: $($_.Exception.Message)"
        }
    } | Tee-Object -FilePath (Join-Path $qaBuild 'audio-environment.log') | Write-Host
}

if ($VirtualAudio) {
    $qaSignTool = Join-Path $qaSdkRoot "bin\$qaSdkVersion\x64\signtool.exe"
    & (Join-Path $PSScriptRoot 'Prepare-VirtualAudioCI.ps1') -PythonPath $qaPython `
        -SignToolPath $qaSignTool -LogDirectory (Join-Path $qaBuild 'virtual-audio')
}

& $qaCTest --test-dir $qaBuild @qaCTestSelection --parallel 1 --verbose --output-on-failure --no-tests=error `
    --output-junit (Join-Path $qaBuild "$($qaSuiteConfig.Results)-results.xml") `
    --output-log (Join-Path $qaBuild "$($qaSuiteConfig.Results)-results.log")
if ($LASTEXITCODE -ne 0) { throw "The native $Suite tests failed. Preserve the CTest output." }
