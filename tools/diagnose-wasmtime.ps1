# Reproduce the first Wasmtime test and collect only the relevant Android
# crash markers and native JNI checkpoints in a readable text file.
param(
    [string]$NdkTestName = "readsLineWithSpacesFromStandardInput"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $root

$adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adb)) { throw "No se encontró adb.exe: $adb" }

$device = & $adb get-state 2>&1
if ($LASTEXITCODE -ne 0 -or ($device -notmatch "device")) {
    throw "No hay un dispositivo Android conectado mediante ADB."
}

$logDirectory = Join-Path $root "app\build\wasmtime-diagnostics"
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$rawLog = Join-Path $logDirectory "android-crash-complete.txt"
$filteredLog = Join-Path $logDirectory "wasmtime-crash-filtered.txt"
$gradleLog = Join-Path $logDirectory "gradle-test-output.txt"

Write-Host "Limpiando logcat para reproducir el cierre..." -ForegroundColor Cyan
& $adb logcat -c
if ($LASTEXITCODE -ne 0) { throw "No se pudo limpiar logcat." }

$testClass = "com.learngraphicsprogramming.app.CppExecutionEngineInstrumentedTest"
$testArgument = "-Pandroid.testInstrumentationRunnerArguments.class=$testClass#$NdkTestName"

Write-Host "Ejecutando una sola prueba de Wasmtime..." -ForegroundColor Cyan
& .\gradlew :app:connectedDebugAndroidTest "-Plgp.wasmtime=true" $testArgument --no-configuration-cache 2>&1 |
    Tee-Object -FilePath $gradleLog
$gradleExit = $LASTEXITCODE

Write-Host "Extrayendo logcat completo y crash buffer..." -ForegroundColor Cyan
& $adb logcat -d -b main -b system -b crash -v threadtime > $rawLog
if ($LASTEXITCODE -ne 0) { throw "ADB no pudo obtener logcat." }

# Context includes the signal, native backtrace, and our last JNI stage.
$pattern = "LGP-Wasmtime|FATAL EXCEPTION|Fatal signal|SIGSEGV|SIGABRT|crash_dump|tombstoned|Abort message|libwasmtime|libapp.so|DEBUG *:|AndroidRuntime"
$matches = Select-String -Path $rawLog -Pattern $pattern -Context 5,15
$matches | Out-String -Width 220 | Set-Content -Path $filteredLog -Encoding UTF8

Write-Host ""
Write-Host "Gradle exit code: $gradleExit"
Write-Host "Diagnóstico filtrado: $filteredLog" -ForegroundColor Green
Write-Host "Logcat completo: $rawLog"
Write-Host "Revisa el archivo filtrado antes de compartirlo: puede contener rutas de archivos."
if ($gradleExit -ne 0) {
    Write-Host "La prueba falló, pero los registros de la caída quedaron guardados." -ForegroundColor Yellow
}
