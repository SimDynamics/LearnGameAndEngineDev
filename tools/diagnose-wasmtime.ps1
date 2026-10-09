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
$liveLog = Join-Path $logDirectory "android-live-logcat.txt"
$liveErrors = Join-Path $logDirectory "android-live-errors.txt"
$phaseLog = Join-Path $logDirectory "cpp-runtime-phase.txt"
$packageStateFile = Join-Path $logDirectory "android-package-state.txt"
$instrumentLog = Join-Path $logDirectory "instrumentation-output.txt"
$targetPackage = "com.learngraphicsprogramming.app"
$testPackage = "$targetPackage.test"

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
$liveCollector = $null
$instrumentationResult = "NOT_RUN"
$packageState = New-Object 'System.Collections.Generic.List[string]'
$packageState.Add("Package availability during direct ADB instrumentation")

# Start continuous logcat BEFORE Gradle starts instrumentation.
# Gradle's own log retrieval can fail when the Android process crashes.
try {
    $liveCollector = Start-Process -FilePath $adb -PassThru -WindowStyle Hidden `
        -ArgumentList @("logcat", "-v", "threadtime", "-b", "main", "-b", "system", "-b", "crash", "-b", "events") `
        -RedirectStandardOutput $liveLog -RedirectStandardError $liveErrors
    Start-Sleep -Milliseconds 400
    Write-Host "Captura continua de logcat iniciada." -ForegroundColor Cyan
} catch {
    $collectionError = "No se pudo iniciar logcat en vivo: $($_.Exception.Message)"
    Write-Host $collectionError -ForegroundColor Yellow
}

try {
    Write-Host "Ejecutando una sola prueba de Wasmtime..." -ForegroundColor Cyan
    try {
        $ErrorActionPreference = "Continue"
        # Install both APKs but DO NOT use connectedDebugAndroidTest: its
        # cleanup can remove the app/test package before run-as inspects files.
        & .\gradlew.bat :app:installDebug :app:installDebugAndroidTest "-Plgp.wasmtime=true" --no-configuration-cache 2>&1 |
            Tee-Object -FilePath $gradleLog
        $gradleExit = $LASTEXITCODE
        $packageState.Add("Gradle APK install exit: $gradleExit")
        if ($gradleExit -eq 0) {
            $targetBefore = (& $adb shell pm path $targetPackage 2>&1 | Out-String).Trim()
            $testBefore = (& $adb shell pm path $testPackage 2>&1 | Out-String).Trim()
            $packageState.Add("Target APK before test: $targetBefore")
            $packageState.Add("Test APK before test: $testBefore")

            if ($targetBefore -notmatch "package:" -or $testBefore -notmatch "package:") {
                $instrumentationResult = "ERROR: expected test and target APKs were not installed"
                Write-Host $instrumentationResult -ForegroundColor Red
                $gradleExit = 1
            } else {
                Write-Host "Ejecutando instrumentacion directamente con ADB..." -ForegroundColor Cyan
                $runner = "$testPackage/androidx.test.runner.AndroidJUnitRunner"
                $selectedTest = "$testClass#$NdkTestName"
                $instrumentLines = @(
                    & $adb shell am instrument -w -r -e class $selectedTest $runner 2>&1 |
                        Tee-Object -FilePath $instrumentLog
                )
                $adbRunCode = $LASTEXITCODE
                $instrumentText = $instrumentLines | Out-String
                $packageState.Add("ADB instrument exit: $adbRunCode")
                if ($instrumentText -match "OK \(" -and
                    $instrumentText -notmatch "FAILURES!!!|Process crashed|INSTRUMENTATION_FAILED|shortMsg=") {
                    $instrumentationResult = "PASS (ADB runner)"
                } elseif ($instrumentText -match "FAILURES!!!|Process crashed|INSTRUMENTATION_FAILED|shortMsg=|INSTRUMENTATION_STATUS_CODE: -2") {
                    $instrumentationResult = "FAILED (ADB runner)"
                } else {
                    $instrumentationResult = "UNKNOWN: inspect instrumentation-output.txt"
                }
                if ($adbRunCode -ne 0 -or $instrumentationResult -eq "FAILED (ADB runner)") {
                    $gradleExit = 1
                }
            }
        } else {
            $instrumentationResult = "NOT RUN (Gradle APK installation failed)"
        }
    } catch {
        # A failed test must not prevent gathering evidence of the crash.
        $collectionError = $_.Exception.Message
        $instrumentationResult = "ERROR: $collectionError"
        Write-Host "Error de instalacion o instrumentacion; continuamos con logcat." -ForegroundColor Yellow
    } finally {
        $ErrorActionPreference = $previousErrorPreference
    }
} finally {
    # End continuous capture, but leave already written evidence on disk.
    if ($null -ne $liveCollector) {
        try {
            $liveCollector.Refresh()
            if (-not $liveCollector.HasExited) { $liveCollector.Kill() }
            $null = $liveCollector.WaitForExit(5000)
            $liveCollector.Dispose()
        } catch {
            $collectionError = "No se pudo cerrar el capturador: $($_.Exception.Message)"
        }
    }

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

    # Confirm whether the package is still installed when the runner returns.
    $targetAfter = (& $adb shell pm path $targetPackage 2>&1 | Out-String).Trim()
    $testAfter = (& $adb shell pm path $testPackage 2>&1 | Out-String).Trim()
    $packageState.Add("Target APK after test: $targetAfter")
    $packageState.Add("Test APK after test: $testAfter")
    $packageState.Add("Instrumentation result: $instrumentationResult")
    $packageState | Set-Content -Path $packageStateFile -Encoding UTF8

    # When logcat is missing, retrieve persistent checkpoints from app-private storage.
    # These markers contain stages only, no source code or standard input.
    try {
        $ErrorActionPreference = "Continue"
        $checkpointOutput = & $adb exec-out run-as $targetPackage `
            cat files/cpp-runtime-phase.txt 2>&1
        $checkpointExit = $LASTEXITCODE
        if ($checkpointExit -eq 0 -and $checkpointOutput -and ($checkpointOutput -match "K00|K01|K02|K03|K04|JNI execute|constructing|compiling WASM|calling _start")) {
            $checkpointOutput | Set-Content -Path $phaseLog -Encoding UTF8
        } else {
            @(
                "No se pudo recuperar el archivo privado de etapas.",
                "Codigo ADB: $checkpointExit",
                "Detalle: $($checkpointOutput | Out-String)"
            ) | Set-Content -Path $phaseLog -Encoding UTF8
        }
    } catch {
        "No se pudieron recuperar las etapas JNI: $($_.Exception.Message)" |
            Set-Content -Path $phaseLog -Encoding UTF8
    } finally {
        $ErrorActionPreference = $previousErrorPreference
    }

    # Always create the filtered artifact so its path is predictable.
    if (-not (Test-Path $rawLog)) {
        "ADB no pudo crear logcat. Error: $collectionError" |
            Set-Content -Path $rawLog -Encoding UTF8
    }
    $pattern = "LGP-Wasmtime|LGP-CppEngine|TestRunner|INSTRUMENTATION_STATUS|FATAL EXCEPTION|Fatal signal|SIGSEGV|SIGABRT|SIGILL|crash_dump|tombstoned|Abort message|backtrace|libwasmtime|libapp.so|AndroidRuntime|DEBUG *:"
    $logFiles = @($rawLog)
    if (Test-Path $liveLog) { $logFiles += $liveLog }
    $matches = Select-String -Path $logFiles -Pattern $pattern -Context 7,24
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
    $summary.Add("Install/instrumentation exit code: $gradleExit")
    $summary.Add("Instrumentation result: $instrumentationResult")
    $summary.Add("=== PACKAGE STATE ===")
    $stateText = Get-Content -Path $packageStateFile -Raw -ErrorAction SilentlyContinue
    if ($stateText) { $summary.Add($stateText) }
    $liveBytes = if (Test-Path $liveLog) { (Get-Item $liveLog).Length } else { 0 }
    $rawBytes = if (Test-Path $rawLog) { (Get-Item $rawLog).Length } else { 0 }
    $summary.Add("Live logcat bytes: $liveBytes; final snapshot bytes: $rawBytes")
    if ($collectionError) { $summary.Add("Collector warning: $collectionError") }
    $summary.Add("")
    $summary.Add("=== PERSISTENT CPP / JNI STAGES ===")
    $phaseText = Get-Content -Path $phaseLog -Raw -ErrorAction SilentlyContinue
    if ($phaseText) { $summary.Add($phaseText) }
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
    Write-Host "Instalacion/instrumentacion exit code: $gradleExit"
    Write-Host "ADB logcat exit code: $logcatExit"
    Write-Host "Diagnóstico filtrado: $filteredLog" -ForegroundColor Green
    Write-Host "Logcat completo: $rawLog"
    Write-Host "Logcat en vivo: $liveLog"
    Write-Host "Etapas privadas de C++/JNI: $phaseLog"
    Write-Host "Salida Gradle: $gradleLog"
    Write-Host "Salida ADB instrument: $instrumentLog"
    Write-Host "Estado de paquetes Android: $packageStateFile"
    if ($collectionError) {
        Write-Host "Detalle adicional: $collectionError" -ForegroundColor Yellow
    }
    Write-Host "Revisa los archivos antes de compartirlos: podrían contener rutas privadas."
    if ($gradleExit -ne 0 -or $instrumentationResult -ne "PASS (ADB runner)") {
        Write-Host "La instrumentacion no se confirmó exitosa; se guardaron los registros." -ForegroundColor Yellow
    }
}
