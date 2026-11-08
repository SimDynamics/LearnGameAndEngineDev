[CmdletBinding()]
param(
    [ValidateSet('StateImport', 'NarrationPlayer')]
    [string] $Suite = 'StateImport'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

# This provisioning entry point is only for a disposable GitHub-hosted runner.
# Local application builds keep using Build-Native.ps1 and the existing tools.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Run this provisioning script only on a disposable GitHub-hosted Windows runner.'
}

$qaRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qaManifest = Get-Content -LiteralPath (Join-Path $qaRoot 'toolchains\windows.versions.json') -Raw |
    ConvertFrom-Json
$qaSuiteConfig = switch ($Suite) {
    'StateImport' {
        @{
            Directory = 'state-import-ci'
            CMakeOption = 'LGAED_STATE_IMPORT_QA_ONLY'
            Target = 'lgaed_state_tests'
            Results = 'state-import'
            Tests = @(
                'native.remainder_published26_state_import',
                'native.remainder_published30_state_import',
                'native.u11_assignment_state_import',
                'native.compiler_contract_state_import'
            )
        }
    }
    'NarrationPlayer' {
        @{
            Directory = 'narration-player-ci'
            CMakeOption = 'LGAED_NARRATION_PLAYER_QA_ONLY'
            Target = 'lgaed_narration_player_tests'
            Results = 'narration-player'
            Tests = @(
                'native.narration_player_rates',
                'native.narration_player_qa_path'
            )
        }
    }
}
$qaBuild = Join-Path $qaRoot "out\qa\$($qaSuiteConfig.Directory)"
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
$qaSdkTools = @('rc.exe', 'mt.exe') | ForEach-Object {
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
foreach ($qaPath in @($qaRoot, $qaBuild, $qaVcVars, $qaCMake, $qaCTest, $qaPython, $qaGit)) {
    if ($qaPath -match '["%!?&|<>^\r\n]') { throw "Unsupported command metacharacter in native QA path: $qaPath" }
}
# CMake checks the actual Python/Git versions against the same manifest and
# retains the normal compiler, SDK, dependency and exact C++23 checks.
$qaCommands = @(
    '@echo off',
    'chcp 65001 >nul',
    "call `"$qaVcVars`" x64 $qaSdkVersion -vcvars_ver=$($qaManifest.visualStudio.msvcToolset)",
    'if errorlevel 1 exit /b %errorlevel%',
    "`"$qaCMake`" -S `"$qaRoot`" -B `"$qaBuild`" -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/WindowsToolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DLGAED_DEFAULT_PROFILE=Codex -D$($qaSuiteConfig.CMakeOption)=ON `"-DPython3_EXECUTABLE=$qaPython`" `"-DGIT_EXECUTABLE=$qaGit`"",
    'if errorlevel 1 exit /b %errorlevel%',
    "`"$qaCMake`" --build `"$qaBuild`" --target $($qaSuiteConfig.Target) --parallel 2",
    'exit /b %errorlevel%'
)
$qaCommandFile = Join-Path $env:RUNNER_TEMP "lgaed-$($qaSuiteConfig.Results)-build.cmd"
[System.IO.File]::WriteAllLines($qaCommandFile, $qaCommands, [System.Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /v:off /c $qaCommandFile
if ($LASTEXITCODE -ne 0) { throw "The verified Windows $Suite target did not configure or compile." }

$qaListingText = & $qaCTest --test-dir $qaBuild --show-only=json-v1
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the focused native test registration.' }
$qaListing = ($qaListingText -join "`n") | ConvertFrom-Json
$qaExpected = $qaSuiteConfig.Tests
$qaActualTests = @($qaListing.tests | ForEach-Object { $_.name })
if ($qaActualTests.Count -ne $qaExpected.Count -or
    (Compare-Object -ReferenceObject $qaExpected -DifferenceObject $qaActualTests)) {
    throw "The focused $Suite configuration must register exactly its $($qaExpected.Count) declared tests."
}

if ($Suite -eq 'NarrationPlayer') {
    # Observe the actual ephemeral machine. Starting an existing, enabled audio
    # service is preparation; no drivers, endpoints or startup types are replaced.
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

& $qaCTest --test-dir $qaBuild --parallel 1 --verbose --output-on-failure --no-tests=error `
    --output-junit (Join-Path $qaBuild "$($qaSuiteConfig.Results)-results.xml") `
    --output-log (Join-Path $qaBuild "$($qaSuiteConfig.Results)-results.log")
if ($LASTEXITCODE -ne 0) { throw "The native $Suite tests failed. Preserve the CTest output." }
