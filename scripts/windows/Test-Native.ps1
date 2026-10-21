[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string] $Configuration = 'Debug',

    [ValidateSet('Work', 'Codex')]
    [string] $ExpectedDefaultProfile = 'Codex',

    [ValidateRange(5, 120)]
    [int] $TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$outputDirectory = Join-Path $repositoryRoot 'out/qa'
[void] [System.IO.Directory]::CreateDirectory($outputDirectory)
$reportPath = Join-Path $outputDirectory ("native-{0}.json" -f $Configuration.ToLowerInvariant())
$localData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$instance = 'native-{0}-{1}-{2}' -f $Configuration.ToLowerInvariant(), $PID,
    ([Guid]::NewGuid().ToString('N').Substring(0, 8))
$checks = [System.Collections.Generic.List[object]]::new()
$startedProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$runtimeResults = [System.Collections.Generic.List[object]]::new()
$failure = $null

function Assert-Condition([bool] $Condition, [string] $Description) {
    if (-not $Condition) {
        throw $Description
    }
    $checks.Add([pscustomobject]@{ check = $Description; passed = $true })
}

function Get-ProfileRoot([string] $ProfileName, [string] $Token) {
    return Join-Path (Join-Path (Join-Path (Join-Path $localData 'LearnGameAndEngineDev') $ProfileName) 'qa') $Token
}

function Read-RuntimeResult([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "The executable did not produce its native window result: $Path"
    }
    $values = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($Path, [System.Text.Encoding]::UTF8)) {
        $pair = $line.Split([char[]]@('='), 2)
        if ($pair.Count -eq 2) {
            $values[$pair[0]] = $pair[1]
        }
    }
    return $values
}

function Start-TestWindow([string[]] $Arguments, [string] $WorkingDirectory) {
    # These windows are the GUI under test, rather than background helpers.
    $process = Start-Process -FilePath $executablePath -ArgumentList $Arguments -PassThru `
        -WorkingDirectory $WorkingDirectory -WindowStyle Normal
    $startedProcesses.Add($process)
    return $process
}

function Wait-TestWindow([System.Diagnostics.Process] $Process, [string] $ProfileName) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "The $ProfileName window exited before it was available (code $($Process.ExitCode))."
        }
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            Assert-Condition ($Process.MainWindowTitle.Contains($ProfileName)) "The native window identifies profile $ProfileName"
            return
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "The $ProfileName process did not create a native top-level window."
}

function Assert-RuntimeResult([hashtable] $Result, [string] $ProfileName, [string] $Token,
                              [int] $ExpectedProcessId) {
    $expectedRoot = Get-ProfileRoot $ProfileName $Token
    Assert-Condition ($Result['profile'] -eq $ProfileName) "Native runtime selected $ProfileName"
    Assert-Condition ($Result['qa_instance'] -eq $Token) "Native runtime kept the isolated QA instance $Token"
    Assert-Condition ([int] $Result['pid'] -eq $ExpectedProcessId) 'The result belongs to the process just launched'
    Assert-Condition ([string]::Equals($Result['data_root'], $expectedRoot,
        [StringComparison]::OrdinalIgnoreCase)) "Native data stays in its $ProfileName QA profile"
    Assert-Condition ($Result['window_painted'] -eq '1') 'The actual Win32 window handled WM_PAINT'
    Assert-Condition ($Result['pointer_bits'] -eq '64') 'The running native process is 64-bit'
    Assert-Condition ([int] $Result['dpi'] -gt 0) 'The native window obtained a valid DPI'
    foreach ($category in @('progress', 'drafts', 'preferences', 'logs', 'projects', 'cache')) {
        Assert-Condition ([string]::Equals($Result[$category], (Join-Path $expectedRoot $category),
            [StringComparison]::OrdinalIgnoreCase)) "Native $ProfileName reports its own $category path"
    }
    $runtimeResults.Add([pscustomobject] $Result)
}

function Invoke-Smoke([string] $ProfileName, [string] $Token, [string] $WorkingDirectory,
                      [switch] $UseDefaultProfile) {
    $arguments = @('--qa-instance', $Token, '--smoke-test')
    if (-not $UseDefaultProfile) {
        $arguments += @('--profile', $ProfileName)
    }
    $process = Start-TestWindow $arguments $WorkingDirectory
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        throw "The real $ProfileName smoke window did not finish."
    }
    $process.Refresh()
    Assert-Condition ($process.ExitCode -eq 0) "The $ProfileName smoke executable exits successfully"
    $resultPath = Join-Path (Get-ProfileRoot $ProfileName $Token) 'logs/smoke-result.txt'
    $result = Read-RuntimeResult $resultPath
    Assert-RuntimeResult $result $ProfileName $Token $process.Id
}

try {
    $stream = [System.IO.File]::OpenRead($executablePath)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        Assert-Condition ($reader.ReadUInt16() -eq 0x5A4D) 'The executable has an MZ header'
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        Assert-Condition ($peOffset -ge 0x40 -and $peOffset -lt ($stream.Length - 96)) 'The PE header offset is valid'
        $stream.Position = $peOffset
        Assert-Condition ($reader.ReadUInt32() -eq 0x00004550) 'The executable has a PE signature'
        Assert-Condition ($reader.ReadUInt16() -eq 0x8664) 'The executable targets AMD64'
        $stream.Position = $peOffset + 24
        Assert-Condition ($reader.ReadUInt16() -eq 0x20B) 'The executable uses the PE32+ format'
        $stream.Position = $peOffset + 24 + 68
        Assert-Condition ($reader.ReadUInt16() -eq 2) 'The executable uses the Windows graphical subsystem'
    }
    finally {
        $reader.Dispose()
    }

    $workRoot = Get-ProfileRoot 'Work' $instance
    $codexRoot = Get-ProfileRoot 'Codex' $instance
    Assert-Condition (-not (Test-Path -LiteralPath $workRoot) -and
        -not (Test-Path -LiteralPath $codexRoot)) 'This run uses new QA data, without personal progress'

    $work = Start-TestWindow @('--profile', 'Work', '--qa-instance', $instance) $repositoryRoot
    $codex = Start-TestWindow @('--profile', 'Codex', '--qa-instance', $instance) $outputDirectory
    Wait-TestWindow $work 'Work'
    Wait-TestWindow $codex 'Codex'
    Assert-Condition ($work.Id -ne $codex.Id -and $work.MainWindowHandle -ne $codex.MainWindowHandle) `
        'Work and Codex run simultaneously in distinct native windows'

    foreach ($entry in @(@('Work', $work), @('Codex', $codex))) {
        $profileName = [string] $entry[0]
        $process = [System.Diagnostics.Process] $entry[1]
        $profileRoot = Get-ProfileRoot $profileName $instance
        $resultPath = Join-Path $profileRoot ("logs/instance-{0}.txt" -f $process.Id)
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while (-not (Test-Path -LiteralPath $resultPath) -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 50
        }
        Assert-RuntimeResult (Read-RuntimeResult $resultPath) $profileName $instance $process.Id
        foreach ($category in @('progress', 'drafts', 'preferences', 'logs', 'projects', 'cache')) {
            $directory = Join-Path $profileRoot $category
            Assert-Condition (Test-Path -LiteralPath $directory -PathType Container) "$profileName creates its own $category directory"
            [System.IO.File]::WriteAllText((Join-Path $directory 'qa-preserve.txt'),
                ("{0}/{1}/{2}`r`n  preserved `t`n" -f $instance, $profileName, $category),
                [System.Text.UTF8Encoding]::new($false))
        }
    }

    Assert-Condition ($work.CloseMainWindow()) 'The Work window accepts an ordinary close request'
    Assert-Condition ($work.WaitForExit($TimeoutSeconds * 1000)) 'The Work window closes normally'
    Assert-Condition ($work.ExitCode -eq 0) 'Closing Work exits successfully'
    $codex.Refresh()
    Assert-Condition (-not $codex.HasExited -and $codex.MainWindowHandle -ne [IntPtr]::Zero) `
        'Closing Work leaves the Codex window running'
    Assert-Condition ($codex.CloseMainWindow()) 'The Codex window accepts an ordinary close request'
    Assert-Condition ($codex.WaitForExit($TimeoutSeconds * 1000)) 'The Codex window closes normally'
    Assert-Condition ($codex.ExitCode -eq 0) 'Closing Codex exits successfully'

    # Reopen the same isolated data from different working directories.
    Invoke-Smoke 'Work' $instance (Join-Path $repositoryRoot 'native')
    Invoke-Smoke 'Codex' $instance $outputDirectory
    Invoke-Smoke $ExpectedDefaultProfile ($instance + '-default') $outputDirectory -UseDefaultProfile

    foreach ($profileName in @('Work', 'Codex')) {
        foreach ($category in @('progress', 'drafts', 'preferences', 'logs', 'projects', 'cache')) {
            $marker = Join-Path (Join-Path (Get-ProfileRoot $profileName $instance) $category) 'qa-preserve.txt'
            $expected = "{0}/{1}/{2}`r`n  preserved `t`n" -f $instance, $profileName, $category
            Assert-Condition ([System.IO.File]::ReadAllText($marker, [System.Text.Encoding]::UTF8) -ceq $expected) `
                "Reopening $profileName preserves the isolated $category data exactly"
        }
    }
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
                # Only processes created by this QA invocation may be terminated.
                $process.Kill()
                $process.WaitForExit()
            }
        }
    }
    [pscustomobject]@{
        executable = $executablePath
        configuration = $Configuration
        qa_instance = $instance
        passed = ($null -eq $failure)
        checks_passed = $checks.Count
        failure = $failure
        checks = $checks
        native_results = $runtimeResults
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}

if ($null -ne $failure) {
    throw "Native Windows verification failed: $failure. Evidence: $reportPath"
}
Write-Output ("Native Windows verification: {0} checks passed. Evidence: {1}" -f $checks.Count, $reportPath)
