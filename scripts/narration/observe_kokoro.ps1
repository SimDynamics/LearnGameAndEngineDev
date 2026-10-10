# Capture one real narration session without clearing Logcat, app data or audio.
# Open the desired lesson first. -ScreenOff verifies preparation while locked.
[CmdletBinding()]
param(
    [string]$Repository = 'E:\PersonalFiles\Projects\LearnGraphicsProgramming',
    [string]$Adb = 'C:\Users\GameDev\AppData\Local\Android\Sdk\platform-tools\adb.exe',
    [string]$Serial = 'ARGK6R3417001970',
    [ValidateRange(5, 3600)][int]$Seconds = 90,
    [switch]$ScreenOff,
    [switch]$InspectInstalledApk
)

$ErrorActionPreference = 'Stop'
$package = 'com.learngraphicsprogramming.app'
$repositoryPath = (Resolve-Path -LiteralPath $Repository).Path
$adbPath = (Resolve-Path -LiteralPath $Adb).Path
$output = Join-Path $repositoryPath ('build\kokoro-observation-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $output | Out-Null
$capture = $null
$slept = $false

function Invoke-Device([string[]]$Arguments) {
    $result = & $adbPath -s $Serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ADB failed ($LASTEXITCODE): $($Arguments -join ' '): $($result -join [Environment]::NewLine)" }
    return $result
}

function Get-WavNames {
    $names = Invoke-Device @('shell', 'run-as', $package, 'ls', 'files/kokoro-narration-cache')
    return @($names | ForEach-Object { "$($_)".Trim() } | Where-Object { $_ -match '^[a-f0-9]{64}\.wav$' } | Sort-Object -Unique)
}

function Save-Snapshot([string]$Label) {
    Invoke-Device @('shell', 'dumpsys', 'activity', 'services', $package) | Set-Content -LiteralPath (Join-Path $output "$Label-service.txt") -Encoding UTF8
    Invoke-Device @('shell', 'dumpsys', 'power') | Set-Content -LiteralPath (Join-Path $output "$Label-power.txt") -Encoding UTF8
    $processResult = & $adbPath -s $Serial shell pidof $package 2>&1
    $appProcess = "$($processResult | Select-Object -First 1)".Trim()
    if ($LASTEXITCODE -eq 0 -and $appProcess -match '^\d+$') {
        Invoke-Device @('shell', 'ps', '-T', '-p', $appProcess) | Set-Content -LiteralPath (Join-Path $output "$Label-threads.txt") -Encoding UTF8
    } else {
        'Application process is not running.' | Set-Content -LiteralPath (Join-Path $output "$Label-threads.txt") -Encoding UTF8
    }
    # Full status, errors and pending/completed counts: no Logcat tag filter.
    foreach ($file in @('kokoro-status.json', 'kokoro-diagnostics.jsonl', 'kokoro-page-preparation.json')) {
        $value = & $adbPath -s $Serial shell run-as $package cat "files/$file" 2>&1
        if ($LASTEXITCODE -eq 0) { $value | Set-Content -LiteralPath (Join-Path $output "$Label-$file") -Encoding UTF8 }
    }
}

function Save-OpusInventory([string]$Apk, [string]$Name) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Apk)
    try {
        $entries = @($zip.Entries | Where-Object { $_.FullName -match '^assets/narration/prerendered/[^/]+\.ogg$' })
        $entries | ForEach-Object { "{0}`t{1}" -f $_.FullName, $_.Length } | Set-Content -LiteralPath (Join-Path $output "$Name-opus.txt") -Encoding UTF8
        Write-Host "$Name contains $($entries.Count) Opus files."
    } finally { $zip.Dispose() }
}

try {
    Invoke-Device @('get-state') | Out-Null
    $initialPower = Invoke-Device @('shell', 'dumpsys', 'power')
    $initiallyAwake = ($initialPower -join "`n") -match 'mWakefulness=Awake'
    if ($ScreenOff -and -not $initiallyAwake) { throw 'Start this screen-off check with the lesson visible and the screen on.' }
    $before = Get-WavNames
    $before | Set-Content -LiteralPath (Join-Path $output 'before-wav-names.txt') -Encoding UTF8
    Save-Snapshot 'before'
    $capture = Start-Process -FilePath $adbPath -ArgumentList @('-s', $Serial, 'logcat', '-b', 'all', '-v', 'threadtime', '-T', '1', '*:V') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output 'logcat-all.txt') -RedirectStandardError (Join-Path $output 'logcat-errors.txt')
    if ($ScreenOff) {
        Invoke-Device @('shell', 'input', 'keyevent', 'KEYCODE_SLEEP') | Out-Null
        $slept = $true
    }
    for ($elapsed = 0; $elapsed -lt $Seconds; $elapsed += 5) {
        Start-Sleep -Seconds ([Math]::Min(5, $Seconds - $elapsed))
        $current = Get-WavNames
        $added = @($current | Where-Object { $_ -notin $before })
        Write-Host "WAV: $($before.Count) before; $($current.Count) now; $($added.Count) new names."
        Save-Snapshot 'latest'
    }
    $after = Get-WavNames
    $after | Set-Content -LiteralPath (Join-Path $output 'after-wav-names.txt') -Encoding UTF8
    @($after | Where-Object { $_ -notin $before }) | Set-Content -LiteralPath (Join-Path $output 'new-wav-names.txt') -Encoding UTF8
    $missing = @($before | Where-Object { $_ -notin $after })
    $missing | Set-Content -LiteralPath (Join-Path $output 'missing-wav-names.txt') -Encoding UTF8
    if ($missing.Count) { Write-Warning "$($missing.Count) existing WAV names are missing; inspect missing-wav-names.txt." }
    Save-Snapshot 'after'
    $builtApk = Join-Path $repositoryPath 'app\build\outputs\apk\debug\app-debug.apk'
    if (Test-Path -LiteralPath $builtApk) { Save-OpusInventory $builtApk 'built-apk' }
    $installedPaths = Invoke-Device @('shell', 'pm', 'path', $package)
    $installedPaths | Set-Content -LiteralPath (Join-Path $output 'installed-apk-paths.txt') -Encoding UTF8
    if ($InspectInstalledApk) {
        $basePath = ($installedPaths | Where-Object { $_ -match '/base\.apk$' } | Select-Object -First 1) -replace '^package:', ''
        if (-not $basePath) { throw 'No installed base APK found.' }
        $installedApk = Join-Path $output 'installed-base.apk'
        Invoke-Device @('pull', $basePath.Trim(), $installedApk) | Out-Null
        Save-OpusInventory $installedApk 'installed-apk'
    }
    Write-Host "Captured service, threads, counts, screen state and complete Logcat in $output"
} finally {
    # Stop only the Logcat reader started by this script; never stop the app.
    if ($capture -and -not $capture.HasExited) { $capture.Kill(); $capture.WaitForExit() }
    if ($slept -and $initiallyAwake) { Invoke-Device @('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP') | Out-Null }
}
