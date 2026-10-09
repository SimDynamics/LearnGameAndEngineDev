# Extract native crash evidence for LearnGraphicsProgramming from an Android bugreport.
# This does NOT rerun the Android test and does NOT upload the private bugreport.
param(
    [string]$BugreportPath = "",
    [switch]$ReuseExisting
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$destination = Join-Path $root "app\build\wasmtime-diagnostics"
New-Item -ItemType Directory -Force -Path $destination | Out-Null

$archivePath = if ($BugreportPath) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BugreportPath)
} else {
    Join-Path $destination "android-native-crash-bugreport.zip"
}
$filteredPath = Join-Path $destination "native-crash-backtrace.txt"
$package = "com.learngraphicsprogramming.app"
$knownPids = @(24366, 22423, 21008)

if (-not $ReuseExisting -or -not (Test-Path $archivePath)) {
    $adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
    if (-not (Test-Path $adb)) { throw "No se encontro adb.exe: $adb" }
    Write-Host "Capturando informe de Android. Este ZIP es privado; NO lo compartas." -ForegroundColor Yellow
    & $adb bugreport $archivePath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $archivePath)) {
        throw "No se pudo crear el archivo bugreport ZIP."
    }
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
$selected = New-Object 'System.Collections.Generic.List[string]'
$selected.Add("LearnGraphicsProgramming native SIGSEGV diagnostic")
$selected.Add("Crash PIDs from ActivityManager exit history: $($knownPids -join ', ')")
$selected.Add("Read-only extraction. Raw bugreport remains on your own computer.")
$selected.Add("")

function Read-ZipText([System.IO.Compression.ZipArchiveEntry]$entry) {
    $reader = New-Object System.IO.StreamReader($entry.Open())
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Add-Window(
    [System.Collections.Generic.List[string]]$output,
    [string[]]$lines,
    [int]$start,
    [int]$count
) {
    if ($lines.Count -eq 0) { return }
    $first = [Math]::Max(0, $start)
    $last = [Math]::Min($lines.Count - 1, $start + $count - 1)
    for ($i = $first; $i -le $last; $i++) {
        $output.Add([string]$lines[$i])
    }
}

try {
    $tombstones = @($archive.Entries | Where-Object {
        $_.FullName -match '(^|/)tombstone_[0-9]+(\.txt)?$' -and $_.Length -gt 0
    })
    $matchingTombstones = 0
    foreach ($entry in $tombstones) {
        $data = Read-ZipText $entry
        $pidMatched = $false
        foreach ($crashPid in $knownPids) {
            if ($data -match ("(?m)\b(?:pid[=: ]+)" + $crashPid + "\b")) {
                $pidMatched = $true
                break
            }
        }
        if (-not $data.Contains($package) -and -not $pidMatched) { continue }
        $matchingTombstones++
        $selected.Add("=== TOMBSTONE $($entry.FullName) ===")
        $lines = [string[]]($data -split "\r?\n")
        Add-Window $selected $lines 0 40
        $backtraceIndex = -1
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match '^\s*backtrace:') { $backtraceIndex = $i; break }
        }
        if ($backtraceIndex -ge 40) {
            $selected.Add("=== BACKTRACE ===")
            Add-Window $selected $lines $backtraceIndex 48
        }
        $selected.Add("")
    }
    $selected.Add("Matching tombstone files: $matchingTombstones")
    $selected.Add("")

    # Fallback: Android sometimes exposes native crash records only inside
    # the main bugreport text, or omits the old tombstones entirely.
    $mainReport = $archive.Entries |
        Where-Object { $_.FullName -match '(^|/)bugreport-[^/]+\.txt$' } |
        Sort-Object Length -Descending |
        Select-Object -First 1
    if ($null -ne $mainReport) {
        $selected.Add("=== MAIN BUGREPORT: RELEVANT LOG EXCERPTS ===")
        $lines = [string[]]((Read-ZipText $mainReport) -split "\r?\n")
        $hits = 0
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            $pidMatched = $false
            foreach ($crashPid in $knownPids) {
                if ($line -match ("\b" + $crashPid + "\b")) {
                    $pidMatched = $true
                    break
                }
            }
            if (($pidMatched -and $line -match 'crash|SIGSEGV|signal 11|tombstone|DEBUG|libwasmtime') -or
                ($line -match [regex]::Escape($package) -and $line -match 'crash|signal|tombstone|DEBUG')) {
                $selected.Add("--- Log at line $($i + 1) ---")
                Add-Window $selected $lines ($i - 8) 32
                $hits++
                $i += 20
                if ($hits -ge 12) { break }
            }
        }
        if ($hits -eq 0) {
            $selected.Add("No relevant crash records found in main bugreport text.")
        }
    } else {
        $selected.Add("Main bugreport text was not found in ZIP.")
    }
} finally {
    $archive.Dispose()
}

$selected | Set-Content -Path $filteredPath -Encoding UTF8
Write-Host ""
Write-Host "Archivo filtrado: $filteredPath" -ForegroundColor Green
Write-Host "No compartas el ZIP completo; puede contener informacion personal." -ForegroundColor Yellow
Write-Host "Si el TXT no incluye PC/backtrace, sera necesario depurar con LLDB."
