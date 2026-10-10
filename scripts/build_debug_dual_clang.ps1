& {
    $ErrorActionPreference = "Stop"
    Push-Location -LiteralPath (Split-Path $PSScriptRoot -Parent)

    try {
        .\gradlew :app:assembleDebug :clang21:assembleDebug `
            "-Plgp.clang23=true" `
            "-Plgp.wasmtime=true" `
            "-Plgp.cppExceptions=true" `
            "-Plgp.playClang21Feature=true" `
            --console=plain

        if ($LASTEXITCODE -ne 0) {
            throw "Fallo la compilacion con Clang 23 y Clang 21."
        }
    } finally {
        Pop-Location
    }
}
