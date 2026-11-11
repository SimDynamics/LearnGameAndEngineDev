[CmdletBinding()]
param(
    [ValidateSet('StateImport', 'NarrationPlayer', 'ReaderBuild', 'WasiRunner', 'NarrationWorker', 'U12Reader',
        'U11EditorialReader', 'CompilerHistoricalReader', 'U6DiagnosticReader')]
    [string] $Suite = 'StateImport',
    [ValidateSet('All', 'DeclarationsAssignments', 'Characters')]
    [string] $CompilerHistoryGroup = 'All',
    [ValidateSet('All', 'Remainder', 'AssignmentP3')]
    [string] $EditorialGroup = 'All',
    [switch] $VirtualAudio
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

$qaRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaManifest = Get-Content -LiteralPath (Join-Path $qaRoot 'toolchains\windows.versions.json') -Raw |
    ConvertFrom-Json
$qaSuiteConfig = switch ($Suite) {
    'StateImport' {
        @{
            Directory = 'state-import-ci'
            CMakeOption = 'LGAED_STATE_IMPORT_QA_ONLY'
            Targets = @('lgaed_state_tests', 'lgaed_stderr_exact_tests', 'lgaed_cpp_project_history_tests', 'lgaed_cpp_history_training_tests')
            Results = 'state-import'
            Tests = @(
                'native.cpp_project_history',
                'native.cpp_history_training',
                'native.remainder_published26_state_import',
                'native.remainder_published30_state_import',
                'native.u11_assignment_state_import',
                'native.compiler_contract_state_import',
                'native.stderr_exact_contract'
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
                'lgaed_u11_declarations_reader_tests',
                'lgaed_u6_diagnostic_flow_tests',
                'lgaed_cpp_project_view_tests'
            )
            Results = 'reader-build'
        }
    }
    'WasiRunner' {
        @{
            Directory = 'wasi-runner-ci'
            CMakeOption = 'LGAED_WASI_RUNNER_QA_ONLY'
            Targets = @('lgaed_cpp_runner_tests')
            Results = 'wasi-runner'
            Tests = @('native.wasi_runner')
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
    throw "The image lacks MSVC $($qaManifest.visualStudio.msvcToolset) with cl.exe $($qaManifest.visualStudio.compilerVersion). No alternate toolset will be selected."
}
$qaVcVars = Join-Path $qaVsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
Write-Host "Image: $env:ImageOS $env:ImageVersion"
Write-Host "Verified MSVC: $($qaManifest.visualStudio.msvcToolset); cl.exe $($qaManifest.visualStudio.compilerVersion)"

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
if ($Suite -in @('NarrationWorker', 'U12Reader', 'U11EditorialReader',
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
            throw "Real synthesis CI requires a fresh destination; existing data is preserved: $qaFreshPath"
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
foreach ($qaPath in @($qaRoot, $qaBuild, $qaVcVars, $qaCMake, $qaCTest, $qaPython, $qaGit) + $qaRuntimePaths) {
    if ($qaPath -match '["%!?&|<>^;\r\n]') { throw "Unsupported command metacharacter in native QA path: $qaPath" }
}
# The disposable runner owns its own isolated dependencies; a CI test
# must never require or create the user's E:\ project dependency directory.
$qaWasiDependencyOption = ''
if ($Suite -eq 'WasiRunner') {
    $qaWasiRoot = (Join-Path $env:RUNNER_TEMP 'lgaed-wasi-deps').Replace('\', '/')
    $qaWasiDependencyOption = "`"-DLGAED_DEP_ROOT=$qaWasiRoot`""
}
# CMake checks the actual Python/Git versions against the same manifest and
# retains the normal compiler, SDK, dependency and exact C++23 checks.
$qaCommands = @(
    '@echo off',
    'chcp 65001 >nul',
    "call `"$qaVcVars`" x64 $qaSdkVersion -vcvars_ver=$($qaManifest.visualStudio.msvcToolset)",
    'if errorlevel 1 exit /b %errorlevel%',
    "`"$qaCMake`" -S `"$qaRoot`" -B `"$qaBuild`" -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/WindowsToolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DLGAED_DEFAULT_PROFILE=Codex -D$($qaSuiteConfig.CMakeOption)=ON `"-DPython3_EXECUTABLE=$qaPython`" `"-DGIT_EXECUTABLE=$qaGit`" $qaRuntimeOptions $qaWasiDependencyOption",
    'if errorlevel 1 exit /b %errorlevel%',
    "`"$qaCMake`" --build `"$qaBuild`" --target $qaBuildTargets --parallel 2",
    'exit /b %errorlevel%'
)
$qaCommandFile = Join-Path $env:RUNNER_TEMP "lgaed-$($qaSuiteConfig.Results)-build.cmd"
[System.IO.File]::WriteAllLines($qaCommandFile, $qaCommands, [System.Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $qaBuild -Force | Out-Null
& $env:ComSpec /d /v:off /c $qaCommandFile 2>&1 |
    Tee-Object -FilePath (Join-Path $qaBuild 'native-build.log') | Write-Host
if ($LASTEXITCODE -ne 0) { throw "The verified Windows $Suite target did not configure or compile." }

if ($Suite -eq 'ReaderBuild') {
    # The original reader/U6 executables are only compiled. The separate
    # project-view executable exercises the real Win32 history dialog in
    # an isolated QA directory; it does NOT grade any lesson or play audio.
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
    # A focused registration cannot silently omit the UI regression.
    $qaUiListingText = & $qaCTest --test-dir $qaBuild --show-only=json-v1
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the project history UI test registration.' }
    $qaUiListing = ($qaUiListingText -join "`n") | ConvertFrom-Json
    $qaUiNames = @($qaUiListing.tests | ForEach-Object { $_.name })
    $qaUiExpected = @('native.cpp_project_internal_dialogs', 'native.cpp_project_u3_selective_lesson', 'native.cpp_project_u3_original_gates', 'native.cpp_project_u3_declaration', 'native.cpp_project_u3_declared_selective', 'native.cpp_project_u3_original_prerequisites', 'native.cpp_project_u3_first_cmake', 'native.cpp_project_u3_first_cmake_repair', 'native.cpp_project_u3_real_history_gate', 'native.cpp_project_u3_entire_original', 'native.cpp_project_other_cpp_multicase')
    if ($qaUiNames.Count -ne $qaUiExpected.Count -or
        (Compare-Object -ReferenceObject $qaUiExpected -DifferenceObject $qaUiNames)) {
        throw 'ReaderBuild must register all eleven Win32 QA cases: original History, declared workflow, entry gates and unrelated C++ multicase.'
    }
    & $qaCTest --test-dir $qaBuild --output-on-failure --no-tests=error --timeout 300
    if ($LASTEXITCODE -ne 0) { throw 'The native Win32 History or focused U3 lesson regression failed.' }
    Write-Host 'PASS original reader/U6 executables compiled and linked; actual Win32 History dialogs and focused published U3 lesson execution/reopen/reset tested. No reader/audio playback executed.'
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
