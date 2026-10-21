[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $WorkExecutable,

    [Parameter(Mandatory = $true)]
    [string] $CodexExecutable,

    # The caller supplies the authorized Work build. It must rebuild/link the
    # Work executable (for example Build-Native.ps1 -CleanFirst -SkipTests) and
    # throw on failure. This script never edits sources or selects Git branches.
    [Parameter(Mandatory = $true)]
    [scriptblock] $RebuildWork,

    [ValidateRange(5, 120)]
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptCheckout = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$outputDirectory = Join-Path $scriptCheckout 'out/qa'
[void] [System.IO.Directory]::CreateDirectory($outputDirectory)
$reportPath = Join-Path $outputDirectory 'checkout-isolation.json'
$instance = 'checkout-{0}-{1}' -f $PID, ([Guid]::NewGuid().ToString('N').Substring(0, 12))
$localData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$checks = [System.Collections.Generic.List[object]]::new()
$startedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$runtimeResults = [System.Collections.Generic.List[object]]::new()
$failure = $null
$workInfo = $null
$codexInfo = $null
$workBinaryBefore = $null
$workBinaryAfter = $null
$codexArtifactsBefore = $null
$codexArtifactsAfter = $null

function Assert-Condition([bool] $Condition, [string] $Description) {
    if (-not $Condition) { throw $Description }
    $checks.Add([pscustomobject]@{ check = $Description; passed = $true })
}

function Test-SamePath([string] $First, [string] $Second) {
    return [string]::Equals([System.IO.Path]::GetFullPath($First),
        [System.IO.Path]::GetFullPath($Second), [StringComparison]::OrdinalIgnoreCase)
}

function Get-CheckoutInfo([string] $Executable, [string] $Owner) {
    $executablePath = (Resolve-Path -LiteralPath $Executable).Path
    Assert-Condition ((Split-Path -Leaf $executablePath) -ceq 'LearnGameAndEngineDev.exe') `
        "$Owner uses the requested native executable"
    $binaryDirectory = Split-Path -Parent $executablePath
    $buildDirectory = Split-Path -Parent $binaryDirectory
    $preset = Split-Path -Leaf $buildDirectory
    Assert-Condition ($preset -cin @('windows-x64-debug', 'windows-x64-relwithdebinfo', 'windows-x64-release')) `
        "$Owner uses a configured x64 build preset"
    $sourceDirectory = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $buildDirectory))
    Assert-Condition ((Split-Path -Leaf $sourceDirectory) -ceq "LearnGameAndEngineDev-$Owner") `
        "$Owner executable belongs to its own checkout"
    $expectedBinary = Join-Path $sourceDirectory "out/build/$preset/bin/LearnGameAndEngineDev.exe"
    Assert-Condition (Test-SamePath $executablePath $expectedBinary) "$Owner keeps its executable under its own build tree"
    $cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
    Assert-Condition (Test-Path -LiteralPath $cachePath -PathType Leaf) "$Owner has its own CMake cache"
    return [pscustomobject]@{
        Owner = $Owner
        Executable = $executablePath
        SourceDirectory = $sourceDirectory
        BuildDirectory = $buildDirectory
        Cache = $cachePath
        Preset = $preset
    }
}

function Assert-CheckoutCache([object] $Info) {
    $entries = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($Info.Cache)) {
        if ($line -match '^([^:#=]+):[^=]+=(.*)$') { $entries[$Matches[1]] = $Matches[2] }
    }
    Assert-Condition (Test-SamePath $entries['CMAKE_HOME_DIRECTORY'] $Info.SourceDirectory) `
        "$($Info.Owner) cache refers to its own sources"
    Assert-Condition (Test-SamePath $entries['CMAKE_CACHEFILE_DIR'] $Info.BuildDirectory) `
        "$($Info.Owner) cache refers to its own build directory"
    Assert-Condition ($entries['LGAED_DEFAULT_PROFILE'] -ceq $Info.Owner) `
        "$($Info.Owner) cache selects the matching default profile"
}

function Get-FileState([string] $Path) {
    $file = Get-Item -LiteralPath $Path
    return [pscustomobject]@{
        path = $file.FullName
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        length = $file.Length
        last_write_utc_ticks = $file.LastWriteTimeUtc.Ticks
    }
}

function Assert-UnchangedFile([object] $Before, [object] $After, [string] $Description) {
    Assert-Condition ($Before.sha256 -ceq $After.sha256 -and
        $Before.length -eq $After.length -and
        $Before.last_write_utc_ticks -eq $After.last_write_utc_ticks) $Description
}

function Get-ProfileRoot([string] $Owner) {
    return Join-Path (Join-Path (Join-Path (Join-Path $localData 'LearnGameAndEngineDev') $Owner) 'qa') $instance
}

function Read-RuntimeResult([string] $Path) {
    $values = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($Path, [System.Text.Encoding]::UTF8)) {
        $pair = $line.Split([char[]]@('='), 2)
        if ($pair.Count -eq 2) { $values[$pair[0]] = $pair[1] }
    }
    return $values
}

function Start-CheckoutWindow([object] $Info) {
    # No --profile: the generated default in each separately built executable
    # must select the correct owner. These visible windows are the GUI test.
    $process = Start-Process -FilePath $Info.Executable -ArgumentList @('--qa-instance', $instance) `
        -WorkingDirectory $Info.SourceDirectory -WindowStyle Normal -PassThru
    $startedProcesses.Add($process)
    $resultPath = Join-Path (Get-ProfileRoot $Info.Owner) ("logs/instance-{0}.txt" -f $process.Id)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "$($Info.Owner) exited before creating its GUI." }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero -and (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
            $result = Read-RuntimeResult $resultPath
            Assert-Condition ($process.MainWindowTitle.Contains($Info.Owner)) "$($Info.Owner) window identifies its profile"
            Assert-Condition ($result['profile'] -ceq $Info.Owner) "$($Info.Owner) executable uses its own default without --profile"
            Assert-Condition ([int] $result['pid'] -eq $process.Id) "$($Info.Owner) result belongs to the current process"
            Assert-Condition ($result['qa_instance'] -ceq $instance) "$($Info.Owner) uses this isolated QA instance"
            Assert-Condition (Test-SamePath $result['exe_path'] $Info.Executable) "$($Info.Owner) runs its own executable"
            Assert-Condition (Test-SamePath $result['data_root'] (Get-ProfileRoot $Info.Owner)) "$($Info.Owner) uses its own QA data root"
            Assert-Condition ($result['pointer_bits'] -eq '64' -and $result['window_painted'] -eq '1') `
                "$($Info.Owner) x64 window actually handles WM_PAINT"
            foreach ($category in @('progress', 'drafts', 'preferences', 'logs', 'projects', 'cache')) {
                Assert-Condition (Test-SamePath $result[$category] (Join-Path (Get-ProfileRoot $Info.Owner) $category)) `
                    "$($Info.Owner) has an independent $category path"
            }
            $runtimeResults.Add([pscustomobject] $result)
            return $process
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$($Info.Owner) did not create and paint its native window."
}

function Assert-Running([System.Diagnostics.Process] $Process, [string] $Description) {
    $Process.Refresh()
    Assert-Condition (-not $Process.HasExited -and $Process.MainWindowHandle -ne [IntPtr]::Zero) $Description
}

function Close-CheckoutWindow([System.Diagnostics.Process] $Process, [string] $Owner) {
    Assert-Condition ($Process.CloseMainWindow()) "$Owner accepts a normal close request"
    Assert-Condition ($Process.WaitForExit($TimeoutSeconds * 1000)) "$Owner closes normally"
    Assert-Condition ($Process.ExitCode -eq 0) "$Owner exits successfully"
}

function Set-QaMarkers([string] $Owner) {
    foreach ($category in @('progress', 'drafts', 'preferences', 'logs', 'projects', 'cache')) {
        $directory = Join-Path (Get-ProfileRoot $Owner) $category
        Assert-Condition (Test-Path -LiteralPath $directory -PathType Container) "$Owner creates its own $category directory"
        [System.IO.File]::WriteAllText((Join-Path $directory 'qa-checkout-preserve.txt'),
            ("{0}/{1}/{2}`r`n  exact `t`n" -f $instance, $Owner, $category),
            [System.Text.UTF8Encoding]::new($false))
    }
}

function Assert-QaMarkers([string] $Owner) {
    foreach ($category in @('progress', 'drafts', 'preferences', 'logs', 'projects', 'cache')) {
        $path = Join-Path (Join-Path (Get-ProfileRoot $Owner) $category) 'qa-checkout-preserve.txt'
        $expected = "{0}/{1}/{2}`r`n  exact `t`n" -f $instance, $Owner, $category
        Assert-Condition ([System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8) -ceq $expected) `
            "$Owner preserves the exact isolated $category marker"
    }
}

try {
    $workInfo = Get-CheckoutInfo $WorkExecutable 'Work'
    $codexInfo = Get-CheckoutInfo $CodexExecutable 'Codex'
    Assert-Condition (-not (Test-SamePath $workInfo.SourceDirectory $codexInfo.SourceDirectory)) 'Work and Codex use distinct source roots'
    Assert-Condition (-not (Test-SamePath $workInfo.Executable $codexInfo.Executable)) 'Work and Codex use distinct executable files'
    Assert-Condition (-not (Test-SamePath $workInfo.Cache $codexInfo.Cache)) 'Work and Codex use distinct CMake caches'
    Assert-Condition ($workInfo.Preset -ceq $codexInfo.Preset) 'Both copies use the same requested build configuration'
    Assert-CheckoutCache $workInfo
    Assert-CheckoutCache $codexInfo
    foreach ($owner in @('Work', 'Codex')) {
        Assert-Condition (-not (Test-Path -LiteralPath (Get-ProfileRoot $owner))) "$owner QA does not reuse personal or previous test data"
    }

    $workWindow = Start-CheckoutWindow $workInfo
    $codexWindow = Start-CheckoutWindow $codexInfo
    Assert-Condition ($workWindow.Id -ne $codexWindow.Id -and
        $workWindow.MainWindowHandle -ne $codexWindow.MainWindowHandle) 'Both separately built executables run simultaneously'
    Set-QaMarkers 'Work'
    Set-QaMarkers 'Codex'
    Close-CheckoutWindow $workWindow 'Work'
    Assert-Running $codexWindow 'Closing Work leaves the same Codex process and window running'

    $workBinaryBefore = Get-FileState $workInfo.Executable
    $codexArtifactsBefore = @((Get-FileState $codexInfo.Executable), (Get-FileState $codexInfo.Cache))
    Assert-QaMarkers 'Work'
    Assert-QaMarkers 'Codex'

    & $RebuildWork

    Assert-Condition (Test-Path -LiteralPath $workInfo.Executable -PathType Leaf) 'The Work rebuild produces its native executable'
    $workBinaryAfter = Get-FileState $workInfo.Executable
    Assert-Condition ($workBinaryAfter.last_write_utc_ticks -gt $workBinaryBefore.last_write_utc_ticks) `
        'The Work executable was actually rewritten by the authorized rebuild'
    $codexArtifactsAfter = @((Get-FileState $codexInfo.Executable), (Get-FileState $codexInfo.Cache))
    Assert-UnchangedFile $codexArtifactsBefore[0] $codexArtifactsAfter[0] 'Rebuilding Work preserves the exact Codex executable and its timestamp'
    Assert-UnchangedFile $codexArtifactsBefore[1] $codexArtifactsAfter[1] 'Rebuilding Work preserves the exact Codex CMake cache and its timestamp'
    Assert-Running $codexWindow 'The same Codex process and window survive the complete Work rebuild'
    Assert-QaMarkers 'Work'
    Assert-QaMarkers 'Codex'
    Assert-CheckoutCache $workInfo
    Assert-CheckoutCache $codexInfo

    $workWindowAfter = Start-CheckoutWindow $workInfo
    Assert-Running $codexWindow 'The rebuilt Work executable coexists with the original Codex process'
    Assert-QaMarkers 'Work'
    Assert-QaMarkers 'Codex'
    Close-CheckoutWindow $workWindowAfter 'Work'
    Assert-Running $codexWindow 'Closing the rebuilt Work executable still leaves Codex running'
    Close-CheckoutWindow $codexWindow 'Codex'
}
catch {
    $failure = $_.Exception.Message
}
finally {
    foreach ($process in $startedProcesses) {
        $process.Refresh()
        if (-not $process.HasExited) {
            [void] $process.CloseMainWindow()
            if (-not $process.WaitForExit(3000)) {
                # Never terminate an unrelated app or a process not started here.
                $process.Kill()
                $process.WaitForExit()
            }
        }
    }
    [pscustomobject]@{
        passed = ($null -eq $failure)
        qa_instance = $instance
        failure = $failure
        checks_passed = $checks.Count
        checks = $checks
        work_checkout = $workInfo
        codex_checkout = $codexInfo
        work_binary_before = $workBinaryBefore
        work_binary_after = $workBinaryAfter
        codex_artifacts_before = $codexArtifactsBefore
        codex_artifacts_after = $codexArtifactsAfter
        native_results = $runtimeResults
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}

if ($null -ne $failure) { throw "Checkout isolation verification failed: $failure. Evidence: $reportPath" }
Write-Output ("Checkout isolation verification: {0} checks passed. Evidence: {1}" -f $checks.Count, $reportPath)
