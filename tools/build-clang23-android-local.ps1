# Local Windows-to-Android LLVM/Clang 23.1.3 cross-build.
# Existing Clang 21 remains the default until device tests pass.
param(
    [ValidateRange(1,32)][int]$Jobs = 16,
    [string]$BuildRoot = ""
)

$ErrorActionPreference = "Stop"
$project = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildRoot) {
    $BuildRoot = Join-Path ([IO.Path]::GetPathRoot($project)) "LGP-LLVM23"
}
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$source = Join-Path $BuildRoot "llvm-project"
$host = Join-Path $BuildRoot "host"
$android = Join-Path $BuildRoot "android"
$dist = Join-Path $BuildRoot "dist"
$archive = Join-Path $BuildRoot "clang-23.1.3-android-arm64-EXPERIMENTAL.tar.gz"

function Check([string]$step) {
    if ($LASTEXITCODE -ne 0) {
        throw "$step ha fallado con código $LASTEXITCODE. Puedes ejecutar de nuevo el script para reutilizar los archivos."
    }
}
function Program([string]$name, [string[]]$alternatives) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($p in $alternatives) {
        if ($p -and (Test-Path $p -PathType Leaf)) { return $p }
    }
    throw "No se encontró $name. Instala el componente correspondiente en Visual Studio / Android Studio."
}
function UnixPath([string]$path) { return $path.Replace('\','/') }

Write-Host "LLVM 23.1.3 para Android ARM64: compilación local con $Jobs tareas" -ForegroundColor Cyan
Write-Host "Archivos temporales: $BuildRoot"
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$git = Program "git.exe" @()
$sdk = Join-Path $env:LOCALAPPDATA "Android\Sdk"
if ($env:ANDROID_HOME -and (Test-Path $env:ANDROID_HOME)) { $sdk = $env:ANDROID_HOME }
$ndkDir = Join-Path $sdk "ndk"
if (-not (Test-Path $ndkDir)) { throw "Falta Android NDK en $ndkDir (Android Studio > SDK Manager > SDK Tools)." }
$ndk = Get-ChildItem $ndkDir -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "build\cmake\android.toolchain.cmake") } |
    Sort-Object { [version]($_.Name -replace '[^0-9.].*$','') } -Descending |
    Select-Object -First 1
if (-not $ndk) { throw "No hay NDK con android.toolchain.cmake." }

# Host tools must run on Windows, so initialise native MSVC x64.
$vsPath = ""
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Instala Visual Studio 2022 con Desktop development with C++." }
    $vsPath = & $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Check "Detección de Visual Studio"
    if (-not $vsPath) { throw "Instala las herramientas MSVC x64 de Visual Studio." }
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) { throw "No se encontró vcvars64.bat." }
    $envOutput = & $env:ComSpec /d /c ('call "' + $vcvars + '" >nul && set')
    Check "Configuración de MSVC"
    foreach ($line in $envOutput) {
        $eq = $line.IndexOf("=")
        if ($eq -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0,$eq),$line.Substring($eq+1),"Process")
        }
    }
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw "No se activó cl.exe (MSVC)." }
Write-Host "MSVC listo: $((Get-Command cl.exe).Source)" -ForegroundColor Green

$cmakePaths = @()
$ninjaPaths = @()
if ($vsPath) {
    $cmakePaths += (Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    $ninjaPaths += (Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
}
if (Test-Path (Join-Path $sdk "cmake")) {
    foreach ($folder in (Get-ChildItem (Join-Path $sdk "cmake") -Directory | Sort-Object Name -Descending)) {
        $cmakePaths += (Join-Path $folder.FullName "bin\cmake.exe")
        $ninjaPaths += (Join-Path $folder.FullName "bin\ninja.exe")
    }
}
$cmake = Program "cmake.exe" $cmakePaths
$ninja = Program "ninja.exe" $ninjaPaths
Write-Host "NDK: $($ndk.FullName); CMake: $cmake; Ninja: $ninja"

if (-not (Test-Path (Join-Path $source ".git"))) {
    Write-Host "[1/5] Descargando el código oficial exacto LLVM 23.1.3..." -ForegroundColor Cyan
    & $git -c core.longpaths=true clone --depth 1 --branch llvmorg-23.1.3 https://github.com/llvm/llvm-project.git $source
    Check "Descarga de LLVM"
} else {
    Write-Host "[1/5] Reutilizando descarga anterior." -ForegroundColor Cyan
}
$tag = (& $git -C $source describe --tags --exact-match | Out-String).Trim()
Check "Verificación de etiqueta LLVM"
if ($tag -ne "llvmorg-23.1.3") { throw "La fuente no corresponde a llvmorg-23.1.3: $tag" }

$common = @(
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$(UnixPath $ninja)",
    "-DLLVM_TARGETS_TO_BUILD=WebAssembly",
    "-DLLVM_INCLUDE_TESTS=OFF",
    "-DLLVM_INCLUDE_EXAMPLES=OFF",
    "-DLLVM_INCLUDE_BENCHMARKS=OFF",
    "-DLLVM_ENABLE_ZLIB=OFF",
    "-DLLVM_ENABLE_ZSTD=OFF",
    "-DLLVM_ENABLE_TERMINFO=OFF",
    "-DLLVM_ENABLE_LIBXML2=OFF",
    "-DLLVM_ENABLE_ASSERTIONS=OFF",
    "-DLLVM_ENABLE_RTTI=OFF",
    "-DLLVM_PARALLEL_LINK_JOBS=1"
)
$llvmDir = Join-Path $source "llvm"
Write-Host "[2/5] Compilando generadores de Windows..." -ForegroundColor Cyan
$hostArgs = @("-S",$llvmDir,"-B",$host) + $common + @("-DCMAKE_BUILD_TYPE=Release","-DLLVM_ENABLE_PROJECTS=clang")
& $cmake @hostArgs
Check "Configuración de generadores nativos"
& $cmake --build $host --parallel $Jobs --target llvm-tblgen clang-tblgen
Check "Compilación de generadores nativos"
$llvmTbl = Join-Path $host "bin\llvm-tblgen.exe"
$clangTbl = Join-Path $host "bin\clang-tblgen.exe"
if (-not (Test-Path $llvmTbl) -or -not (Test-Path $clangTbl)) { throw "Faltan los generadores TableGen de Windows." }

Write-Host "[3/5] Configurando Clang 23 Android ARM64..." -ForegroundColor Cyan
$toolchain = Join-Path $ndk.FullName "build\cmake\android.toolchain.cmake"
$targetArgs = @("-S",$llvmDir,"-B",$android) + $common + @(
    "-DCMAKE_TOOLCHAIN_FILE=$(UnixPath $toolchain)",
    "-DANDROID_ABI=arm64-v8a",
    "-DANDROID_PLATFORM=android-28",
    "-DANDROID_STL=c++_static",
    "-DCMAKE_BUILD_TYPE=MinSizeRel",
    "-DLLVM_ENABLE_PROJECTS=clang;lld",
    "-DLLVM_NATIVE_TOOL_DIR=$(UnixPath (Join-Path $host 'bin'))",
    "-DLLVM_TABLEGEN=$(UnixPath $llvmTbl)",
    "-DCLANG_TABLEGEN=$(UnixPath $clangTbl)",
    "-DLLVM_DEFAULT_TARGET_TRIPLE=wasm32-wasip1",
    "-DCLANG_INCLUDE_TESTS=OFF",
    "-DLLD_INCLUDE_TESTS=OFF",
    "-DLLVM_ENABLE_LIBEDIT=OFF",
    "-DLLVM_BUILD_LLVM_DYLIB=OFF",
    "-DLLVM_LINK_LLVM_DYLIB=OFF"
)
& $cmake @targetArgs
Check "Configuración para Android"

Write-Host "[4/5] Compilando Clang y LLD ARM64 ($Jobs procesos)..." -ForegroundColor Cyan
& $cmake --build $android --parallel $Jobs --target clang lld
Check "Compilación Android ARM64"

Write-Host "[5/5] Verificando y empaquetando..." -ForegroundColor Cyan
$ndkReadelf = Join-Path $ndk.FullName "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe"
if (-not (Test-Path $ndkReadelf)) { throw "Falta $ndkReadelf." }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$inputs = @{"liblgp_clang23.so"="clang";"liblgp_lld23.so"="lld"}
foreach ($name in @("liblgp_clang23.so","liblgp_lld23.so")) {
    $base = $inputs[$name]
    $binary = Join-Path $android "bin\$base"
    if (-not (Test-Path $binary)) { $binary += ".exe" }
    if (-not (Test-Path $binary)) { throw "Falta ejecutable Android: $base." }
    $destination = Join-Path $dist $name
    Copy-Item -LiteralPath $binary -Destination $destination -Force
    $elf = (& $ndkReadelf -h $destination | Out-String)
    Check "Verificación ELF de $name"
    if ($elf -notmatch "Machine:\s*AArch64" -or $elf -notmatch "Type:\s*DYN") {
        throw "El archivo $name no es un ejecutable Android ARM64 PIE."
    }
    $dynamic = (& $ndkReadelf -d $destination | Out-String)
    Check "Dependencias ELF de $name"
    if ($dynamic -match "libc\.so\.6|libstdc\+\+\.so\.6|ld-linux") {
        throw "Se detectaron dependencias GNU/Linux, incompatibles con Android."
    }
    Write-Host "$name: ELF ARM64 verificado" -ForegroundColor Green
}
$shaLines = foreach ($name in @("liblgp_clang23.so","liblgp_lld23.so")) {
    $sha = (Get-FileHash (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$sha  $name"
}
[IO.File]::WriteAllLines((Join-Path $dist "SHA256SUMS"), [string[]]@($shaLines))
$commit = (& $git -C $source rev-parse HEAD | Out-String).Trim()
[IO.File]::WriteAllText((Join-Path $dist "llvm-source-commit.txt"), $commit + [Environment]::NewLine)
[IO.File]::WriteAllText((Join-Path $dist "README.txt"),
    "LLVM 23.1.3 experimental Android ARM64. Still requires on-device tests." + [Environment]::NewLine)
if (Test-Path $archive) { Remove-Item $archive -Force }
& tar.exe -czf $archive -C $dist .
Check "Empaquetado Clang 23"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $project "tools\import-clang23-arm64.ps1") -ArchivePath $archive
Check "Importación experimental"
Write-Host "Compilación e importación completadas: $archive" -ForegroundColor Green
Write-Host "El Clang 21 anterior continúa siendo la opción predeterminada."
