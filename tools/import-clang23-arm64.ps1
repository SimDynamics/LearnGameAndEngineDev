# Import a GitHub Actions LLVM/Clang 23.1.3 Android ARM64 build.
# Does NOT modify the running application and keeps Clang 21 as the default.
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$archive = (Resolve-Path $ArchivePath).Path
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("lgp-clang23-" + [guid]::NewGuid().ToString("N"))
$payload = Join-Path $temp "payload"
New-Item -ItemType Directory -Force -Path $payload | Out-Null

function Test-Arm64Pie([string]$path) {
    $header = New-Object byte[] 20
    $file = [System.IO.File]::OpenRead($path)
    try {
        if ($file.Read($header, 0, 20) -ne 20) { throw "Archivo ELF incompleto: $path" }
    } finally { $file.Dispose() }
    if ($header[0] -ne 127 -or $header[1] -ne 69 -or
        $header[2] -ne 76 -or $header[3] -ne 70 -or
        $header[4] -ne 2 -or $header[5] -ne 1 -or
        $header[16] -ne 3 -or $header[17] -ne 0 -or
        $header[18] -ne 183 -or $header[19] -ne 0) {
        throw "No es un ejecutable Android ARM64 ELF64 PIE: $path"
    }
}
try {
    if ($archive.EndsWith(".zip", [StringComparison]::OrdinalIgnoreCase)) {
        Expand-Archive -LiteralPath $archive -DestinationPath $temp
        $nested = Get-ChildItem -Path $temp -Filter "*.tar.gz" -Recurse -File |
            Select-Object -First 1
        if (-not $nested) {
            throw "El ZIP de GitHub Actions no contiene el archivo .tar.gz esperado."
        }
        $tarPath = $nested.FullName
    } elseif ($archive.EndsWith(".tar.gz", [StringComparison]::OrdinalIgnoreCase)) {
        $tarPath = $archive
    } else {
        throw "Usa el ZIP de GitHub Actions o el archivo .tar.gz interno."
    }

    & tar.exe -xf $tarPath -C $payload
    if ($LASTEXITCODE -ne 0) { throw "tar.exe no pudo extraer el compilador." }

    $checksumFile = Join-Path $payload "SHA256SUMS"
    if (-not (Test-Path $checksumFile)) {
        throw "El paquete carece de SHA256SUMS."
    }
    $checksums = @(Get-Content $checksumFile)
    $names = @("liblgp_clang23.so", "liblgp_lld23.so")
    foreach ($name in $names) {
        $path = Join-Path $payload $name
        if (-not (Test-Path $path)) { throw "Falta $name en el paquete." }
        $line = @($checksums | Where-Object { $_ -match ("\s+" + [regex]::Escape($name) + "$") })
        if ($line.Count -ne 1) { throw "No se encontro una suma SHA256 unica de $name." }
        $expected = ($line[0] -split "\s+")[0].ToUpperInvariant()
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -ne $expected) {
            throw "SHA256 no coincide para $name."
        }
        Test-Arm64Pie $path
        Write-Host "$name : SHA256 y ELF ARM64 verificados" -ForegroundColor Green
    }

    $target = Join-Path $root ".gradle\clang23-android-arm64"
    $staging = "$target-import"
    if (Test-Path $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $staging | Out-Null
    foreach ($name in $names) {
        Copy-Item -LiteralPath (Join-Path $payload $name) -Destination $staging
    }
    foreach ($name in @("SHA256SUMS", "README.txt", "llvm-source-commit.txt")) {
        $source = Join-Path $payload $name
        if (Test-Path $source) {
            Copy-Item -LiteralPath $source -Destination $staging
        }
    }
    if (Test-Path $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Move-Item -LiteralPath $staging -Destination $target

    Write-Host "Paquete experimental importado en $target" -ForegroundColor Green
    Write-Host "El compilador activo sigue siendo Clang 21 hasta usar -Plgp.clang23=true."
    Write-Host "Antes de activarlo, verifica '--version' con Clang23InstrumentedTest."
} finally {
    if (Test-Path $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
