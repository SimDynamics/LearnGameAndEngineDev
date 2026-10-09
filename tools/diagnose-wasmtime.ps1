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

# Gradle writes test failures to stderr. In Windows PowerShell 5.1,
# redirecting native stderr (2>&1) turns it into NativeCommandError;
# ErrorActionPreference=Stop would abort BEFORE collecting logcat.
# The native failure is expected here: it is what we are diagnosing.
$previousErrorPreference = $ErrorActionPreference
$gradleExit = -1
$logcatExit = -1
$collectionError = $null

try {
    Write-Host "Ejecutando una sola prueba de Wasmtime..." -ForegroundColor Cyan
    try {
        $ErrorActionPreference = "Continue"
        & .\gradlew.bat :app:connectedDebugAndroidTest "-Plgp.wasmtime=true" $testArgument --no-configuration-cache 2>&1 |
            Tee-Object -FilePath $gradleLog
        $gradleExit = $LASTEXITCODE
    } catch {
        # A failed test must not prevent gathering evidence of the crash.
        $collectionError = $_.Exception.Message
        Write-Host "Gradle informó un error; continuamos con logcat." -ForegroundColor Yellow
    } finally {
        $ErrorActionPreference = $previousErrorPreference
    }
} finally {
    # Always collect after the test, even when Gradle reports failed tests.
    Write-Host "Extrayendo logcat completo y crash buffer..." -ForegroundColor Cyan
    $ErrorActionPreference = "Continue"
    try {
        & $adb logcat -d -b main -b system -b crash -v threadtime 2>&1 |
            Out-File -FilePath $rawLog -Encoding UTF8
        $logcatExit = $LASTEXITCODE
    } catch {
        $collectionError = $_.Exception.Message
    } finally {
        $ErrorActionPreference = $previousErrorPreference
    }

    # Always create the filtered artifact so its path is predictable.
    if (-not (Test-Path $rawLog)) {
        "ADB no pudo crear logcat. Error: $collectionError" |
            Set-Content -Path $rawLog -Encoding UTF8
    }
    $pattern = "LGP-Wasmtime|LGP-CppEngine|TestRunner|INSTRUMENTATION_STATUS|FATAL EXCEPTION|Fatal signal|SIGSEGV|SIGABRT|SIGILL|crash_dump|tombstoned|Abort message|backtrace|libwasmtime|libapp.so|AndroidRuntime|DEBUG *:"
    $matches = Select-String -Path $rawLog -Pattern $pattern -Context 7,24
    if ($matches) {
        $matches | Out-String -Width 220 |
            Set-Content -Path $filteredLog -Encoding UTF8
    } else {
        @(
            "No se encontraron marcadores de caída o LGP-Wasmtime en logcat.",
            "Revisa android-crash-complete.txt para más contexto.",
            "Si el proceso se cerró, comprueba también los registros de Android Studio."
        ) | Set-Content -Path $filteredLog -Encoding UTF8
    }

    # Include non-native failures. Java assertions and build errors are
    # often absent from Android logcat and still explain a failed test.
    $summary = New-Object 'System.Collections.Generic.List[string]'
    $summary.Add("Wasmtime diagnostic: native traces, Gradle, instrumentation XML")
    $summary.Add("Gradle exit code: $gradleExit")
    $summary.Add("")
    $summary.Add("=== ANDROID LOGCAT ===")
    $nativeText = Get-Content -Path $filteredLog -Raw -ErrorAction SilentlyContinue
    if ($nativeText) { $summary.Add($nativeText) }
    $summary.Add("")
    $summary.Add("=== GRADLE OUTPUT: LAST 150 LINES ===")
    $gradleLines = @(Get-Content -Path $gradleLog -Tail 150 -ErrorAction SilentlyContinue)
    if ($gradleLines.Count -gt 0) {
        foreach ($line in $gradleLines) { $summary.Add([string]$line) }
    } else {
        $summary.Add("No Gradle output was recorded.")
    }
    $summary.Add("")
    $summary.Add("=== TEST XML ERRORS ===")
    $xmlDirectory = Join-Path $root "app\build\outputs\androidTest-results\connected"
    if (Test-Path $xmlDirectory) {
        $xmlReports = @(Get-ChildItem -Path $xmlDirectory -Recurse -File -Filter "*.xml" |
            Sort-Object LastWriteTime -Descending | Select-Object -First 3)
        foreach ($report in $xmlReports) {
            $summary.Add("Report: $($report.FullName)")
            $xmlContent = Get-Content -Path $report.FullName -Raw -ErrorAction SilentlyContinue
            if ($xmlContent) {
                $blocks = [regex]::Matches($xmlContent, "(?s)<(?:failure|error)\b.*?</(?:failure|error)>")
                if ($blocks.Count -gt 0) {
                    foreach ($block in @($blocks | Select-Object -First 3)) {
                        $length = [Math]::Min(12000, $block.Value.Length)
                        $summary.Add($block.Value.Substring(0, $length))
                    }
                } else {
                    $summary.Add($xmlContent.Substring(0, [Math]::Min(2000, $xmlContent.Length)))
                }
            }
        }
    } else {
        $summary.Add("No instrumentation XML reports were found.")
    }
    $summary | Set-Content -Path $filteredLog -Encoding UTF8

    Write-Host ""
    Write-Host "Gradle exit code: $gradleExit"
    Write-Host "ADB logcat exit code: $logcatExit"
    Write-Host "Diagnóstico filtrado: $filteredLog" -ForegroundColor Green
    Write-Host "Logcat completo: $rawLog"
    Write-Host "Salida Gradle: $gradleLog"
    if ($collectionError) {
        Write-Host "Detalle adicional: $collectionError" -ForegroundColor Yellow
    }
    Write-Host "Revisa los archivos antes de compartirlos: podrían contener rutas privadas."
    if ($gradleExit -ne 0) {
        Write-Host "La prueba falló, pero se conservaron los archivos de diagnóstico." -ForegroundColor Yellow
    }
}
