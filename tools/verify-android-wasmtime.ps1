# Verify ELF DT_NEEDED and the final Android APK, catching Windows paths.
param([string]$NdkVersion = "28.2.13676358")

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $root "app\build\intermediates\cxx\Debug"
$library = Get-ChildItem $buildRoot -Filter libapp.so -File -Recurse |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $library) { throw "No se encontró libapp.so compilado." }

$ndkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk\$NdkVersion"
$readelf = Join-Path $ndkRoot "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe"
if (-not (Test-Path $readelf)) { throw "No existe llvm-readelf.exe: $readelf" }

$needed = (& $readelf --dynamic $library.FullName | Out-String)
if ($LASTEXITCODE -ne 0) { throw "llvm-readelf falló." }

Write-Host "DT_NEEDED de libapp.so:" -ForegroundColor Cyan
($needed -split '\r?\n') |
    Where-Object { $_ -match "\bNEEDED\b" } |
    ForEach-Object { Write-Host $_ }

if ($needed -notmatch 'Shared library: \[libwasmtime\.so\]') {
    throw "libapp.so no declara [libwasmtime.so] como dependencia."
}
if ($needed -match 'Shared library: \[[A-Za-z]:[/\\]') {
    throw "ERROR: DT_NEEDED contiene una ruta absoluta de Windows."
}

$apk = Join-Path $root "app\build\outputs\apk\debug\app-debug.apk"
if (-not (Test-Path $apk)) { throw "No existe app-debug.apk." }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($apk)
try {
    $entries = @($archive.Entries | ForEach-Object FullName)
    foreach ($entry in @("lib/arm64-v8a/libapp.so", "lib/arm64-v8a/libwasmtime.so")) {
        if ($entries -notcontains $entry) {
            throw "La APK no contiene $entry."
        }
        Write-Host "APK OK: $entry" -ForegroundColor Green
    }
} finally {
    $archive.Dispose()
}
Write-Host "Correcto: dependencia ELF portable y ambas bibliotecas en la APK." -ForegroundColor Green
