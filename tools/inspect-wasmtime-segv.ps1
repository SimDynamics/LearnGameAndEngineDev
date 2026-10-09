# Disassemble exact ARM64 offsets recorded in the Wasmtime tombstone.
# This command does not reinstall or execute the Android application.
param(
    [string]$LibraryPath = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outDir = Join-Path $root "app\build\wasmtime-diagnostics"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outputFile = Join-Path $outDir "wasmtime-arm64-disassembly.txt"

if (-not $LibraryPath) {
    $LibraryPath = Join-Path $root "app\build\generated\wasmtime\sdk\lib\libwasmtime.so"
}
if (-not (Test-Path $LibraryPath)) {
    throw "No se encontro la biblioteca ARM64 de Wasmtime: $LibraryPath"
}

$ndkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk"
if (-not (Test-Path $ndkRoot)) {
    throw "No se encontro NDK. Verifica Android SDK > SDK Tools > NDK."
}
$ndks = @(Get-ChildItem -Path $ndkRoot -Directory | Sort-Object Name -Descending)
$objdump = $null
$addr2line = $null
foreach ($item in $ndks) {
    $candidate = Join-Path $item.FullName "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-objdump.exe"
    if (Test-Path $candidate) {
        $objdump = $candidate
        $addr2line = Join-Path (Split-Path $candidate -Parent) "llvm-addr2line.exe"
        break
    }
}
if (-not $objdump) { throw "No se encontro llvm-objdump.exe en los NDK instalados." }

$report = New-Object 'System.Collections.Generic.List[string]'
$report.Add("ARM64 Wasmtime SIGSEGV disassembly")
$report.Add("libwasmtime.so SHA256: $((Get-FileHash -Path $LibraryPath -Algorithm SHA256).Hash)")
$report.Add("Tombstone PC #00: 0x0098b38c (identical in repeated crashes)")
$report.Add("Caller PC #01: 0x00e2e66c (wasmtime_module_new+24)")
$report.Add("Fault: SEGV_MAPERR, address 0x00000000000002a9")
$report.Add("")

# Keep a small window around the two addresses. This should not dump the entire library.
$windows = @(
    @{ Label = "Crashing instruction and surrounding ARM64 operations"; Start = "0x98b340"; Stop = "0x98b3e0" },
    @{ Label = "Exported wasmtime_module_new entry and call site"; Start = "0xe2e640"; Stop = "0xe2e6b0" }
)
foreach ($window in $windows) {
    $report.Add("=== $($window.Label) ===")
    $report.Add("Range: $($window.Start) to $($window.Stop)")
    try {
        $result = & $objdump -d --no-show-raw-insn "--start-address=$($window.Start)" "--stop-address=$($window.Stop)" $LibraryPath 2>&1
        foreach ($line in @($result)) { $report.Add([string]$line) }
        $report.Add("llvm-objdump exit code: $LASTEXITCODE")
    } catch {
        $report.Add("objdump error: $($_.Exception.Message)")
    }
    $report.Add("")
}

if (Test-Path $addr2line) {
    $report.Add("=== OPTIONAL SYMBOLIZATION ===")
    try {
        $result = & $addr2line -f -C -e $LibraryPath 0x98b38c 0xe2e66c 2>&1
        foreach ($line in @($result)) { $report.Add([string]$line) }
    } catch {
        $report.Add("addr2line error: $($_.Exception.Message)")
    }
}

$report | Set-Content -Path $outputFile -Encoding UTF8
Write-Host "Informe creado: $outputFile" -ForegroundColor Green
Write-Host "No se ha iniciado ninguna prueba ni modificado la APK."
