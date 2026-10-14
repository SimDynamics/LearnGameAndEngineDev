# Build real CMake and Ninja from pinned upstream sources for Android ARM64.
# No APK/process-global PATH mutation; all outputs stay under this checkout.
param(
    [ValidateRange(1,32)][int]$Jobs = 8,
    [string]$NdkVersion = "28.2.13676358",
    [string]$LlvmRoot = "E:/LGP-LLVM23"
)
$ErrorActionPreference = "Stop"
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$buildRoot = Join-Path $project ".gradle/cmake-android-build"
$dist = Join-Path $project ".gradle/cmake-android-arm64"
$sdk = Join-Path $env:LOCALAPPDATA "Android/Sdk"
$ndk = Join-Path $sdk "ndk/$NdkVersion"
$cmake = Join-Path $sdk "cmake/4.1.2/bin/cmake.exe"
$ninja = Join-Path $sdk "cmake/4.1.2/bin/ninja.exe"
$toolchain = Join-Path $ndk "build/cmake/android.toolchain.cmake"
$readelf = Join-Path $ndk "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe"
$strip = Join-Path $ndk "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
foreach ($program in @($cmake,$ninja,$toolchain,$readelf,$strip)) {
    if (-not (Test-Path -LiteralPath $program -PathType Leaf)) { throw "Missing local build prerequisite: $program" }
}
function Check([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed with exit $LASTEXITCODE" } }
function UnixPath([string]$path) { return $path.Replace('\','/') }
function Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Acquire([string]$name,[string]$url,[string]$sha) {
    $path = Join-Path $buildRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Hash $path) -ne $sha) {
        Write-Host "Downloading verified source: $name"
        Invoke-WebRequest -Uri $url -OutFile $path
    }
    if ((Hash $path) -ne $sha) { throw "Source digest mismatch: $name" }
    return $path
}
function ApplyPatch([string]$source,[string]$patchFile) {
    # Each intentionally small patch contains one exact-context hunk. This
    # avoids requiring a Git checkout or an external patch executable.
    $lines = [IO.File]::ReadAllLines($patchFile)
    $oldLines = [Collections.Generic.List[string]]::new()
    $newLines = [Collections.Generic.List[string]]::new()
    $insideHunk = $false
    foreach ($line in $lines) {
        if ($line.StartsWith('@@')) {
            if ($insideHunk) { throw "Patch must contain one hunk: $patchFile" }
            $insideHunk = $true; continue
        }
        if (-not $insideHunk) { continue }
        if ($line.StartsWith(' ')) { $oldLines.Add($line.Substring(1)); $newLines.Add($line.Substring(1)) }
        elseif ($line.StartsWith('-')) { $oldLines.Add($line.Substring(1)) }
        elseif ($line.StartsWith('+')) { $newLines.Add($line.Substring(1)) }
        elseif ($line -eq '\ No newline at end of file') { continue }
        else { throw "Invalid patch line: $patchFile" }
    }
    $old = [string]::Join("`n",$oldLines) + "`n"
    $replacement = [string]::Join("`n",$newLines) + "`n"
    $text = [IO.File]::ReadAllText($source).Replace("`r`n","`n")
    if ($text.Contains($replacement)) { return }
    if (-not $text.Contains($old) -or $text.IndexOf($old) -ne $text.LastIndexOf($old)) { throw "Patch context does not match exactly once: $source" }
    [IO.File]::WriteAllText($source,$text.Replace($old,$replacement),[Text.UTF8Encoding]::new($false))
}
New-Item -ItemType Directory -Force -Path $buildRoot,$dist | Out-Null
$cmakeArchive = Acquire "cmake-4.1.2.tar.gz" "https://cmake.org/files/v4.1/cmake-4.1.2.tar.gz" "643f04182b7ba323ab31f526f785134fb79cba3188a852206ef0473fee282a15"
$ninjaArchive = Acquire "ninja-1.12.1.tar.gz" "https://codeload.github.com/ninja-build/ninja/tar.gz/refs/tags/v1.12.1" "821bdff48a3f683bc4bb3b6f0b5fe7b2d647cf65d52aeb63328c91a6c6df285a"
$cmakeSource = Join-Path $buildRoot "cmake-4.1.2"
$ninjaSource = Join-Path $buildRoot "ninja-1.12.1"
if (-not (Test-Path -LiteralPath (Join-Path $cmakeSource ".lgp-source-extracted"))) {
    & python.exe (Join-Path $PSScriptRoot 'extract-cmake-source.py') $cmakeArchive $buildRoot
    Check "CMake source extraction"
}
if (-not (Test-Path -LiteralPath (Join-Path $ninjaSource "CMakeLists.txt"))) {
    & tar.exe -xzf $ninjaArchive -C $buildRoot
    Check "Ninja source extraction"
}
ApplyPatch (Join-Path $cmakeSource "Source/cmSystemTools.cxx") (Join-Path $PSScriptRoot "cmake-android-resources.patch")
ApplyPatch (Join-Path $cmakeSource "Utilities/cmlibuv/CMakeLists.txt") (Join-Path $PSScriptRoot "cmake-android-libuv.patch")
ApplyPatch (Join-Path $cmakeSource "Utilities/cmlibuv/CMakeLists.txt") (Join-Path $PSScriptRoot "cmake-android-pthread-source.patch")
ApplyPatch (Join-Path $cmakeSource "Utilities/cmlibuv/src/unix/process.c") (Join-Path $PSScriptRoot "cmake-android-affinity.patch")
ApplyPatch (Join-Path $cmakeSource "Utilities/cmlibarchive/libarchive/archive.h") (Join-Path $PSScriptRoot "cmake-android-archive-h.patch")
ApplyPatch (Join-Path $cmakeSource "Utilities/cmlibarchive/libarchive/archive_entry.h") (Join-Path $PSScriptRoot "cmake-android-archive_entry-h.patch")
ApplyPatch (Join-Path $ninjaSource "src/subprocess-posix.cc") (Join-Path $PSScriptRoot "ninja-android-shell.patch")
$common = @(
    "-G","Ninja","-DCMAKE_MAKE_PROGRAM=$(UnixPath $ninja)",
    "-DCMAKE_TOOLCHAIN_FILE=$(UnixPath $toolchain)","-DANDROID_ABI=arm64-v8a",
    "-DANDROID_PLATFORM=android-28","-DANDROID_STL=c++_static",
    "-DCMAKE_BUILD_TYPE=MinSizeRel","-DBUILD_TESTING=OFF",
    "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-z,max-page-size=16384"
)
$cmakeBuild = Join-Path $buildRoot "cmake-build"
& $cmake -S $cmakeSource -B $cmakeBuild @common `
    -DCMAKE_USE_SYSTEM_LIBRARIES=OFF -DCMAKE_USE_OPENSSL=OFF `
    -DCMake_ENABLE_DEBUGGER=OFF -DBUILD_CursesDialog=OFF -DBUILD_QtDialog=OFF `
    -DCMake_BUILD_PCH=OFF
Check "Android CMake configuration"
& $cmake --build $cmakeBuild --parallel $Jobs --target cmake
Check "Android CMake compilation"
$ninjaBuild = Join-Path $buildRoot "ninja-build"
& $cmake -S $ninjaSource -B $ninjaBuild @common
Check "Android Ninja configuration"
& $cmake --build $ninjaBuild --parallel $Jobs --target ninja
Check "Android Ninja compilation"
$llvmSource = Join-Path $LlvmRoot 'llvm-project'
$llvmBuild = Join-Path $LlvmRoot 'android'
$llvmTableGen = Join-Path $LlvmRoot 'host/bin/llvm-tblgen.exe'
$llvmCommit = (& git.exe -C $llvmSource rev-parse HEAD | Out-String).Trim()
Check "Read LLVM source identity"
if ($llvmCommit -ne '0d261d1ca552c95a8f007e061c787ac7132fbcbc') { throw 'llvm-ar inputs must use verified LLVM 23.1.3 sources.' }
$arBuild = Join-Path $buildRoot 'llvm-ar-build'
& $cmake -S (Join-Path $PSScriptRoot 'llvm-ar') -B $arBuild @common `
    "-DLGP_LLVM_SOURCE=$(UnixPath $llvmSource)" `
    "-DLGP_LLVM_BUILD=$(UnixPath $llvmBuild)" `
    "-DLGP_LLVM_TABLEGEN=$(UnixPath $llvmTableGen)"
Check "Android llvm-ar configuration"
& $cmake --build $arBuild --parallel $Jobs --target lgp_llvm_ar
Check "Android llvm-ar compilation"
$binaries = @{
    "liblgp_cmake.so" = (Join-Path $cmakeBuild "bin/cmake")
    "liblgp_ninja.so" = (Join-Path $ninjaBuild "ninja")
    "liblgp_llvm_ar.so" = (Join-Path $arBuild "lgp_llvm_ar")
}
foreach ($name in $binaries.Keys) {
    $destination = Join-Path $dist $name
    Copy-Item -LiteralPath $binaries[$name] -Destination $destination -Force
    & $strip --strip-unneeded $destination
    Check "Strip $name"
    $elf = (& $readelf -h -l -d $destination | Out-String)
    Check "Verify $name"
    if ($elf -notmatch 'Machine:\s*AArch64' -or $elf -notmatch 'Type:\s*DYN' -or $elf -notmatch '/system/bin/linker64') { throw "Not an Android ARM64 PIE: $name" }
    if ($elf -match 'libc\+\+_shared|libc\.so\.6|libstdc\+\+\.so\.6|ld-linux') { throw "Unexpected shared/host dependency: $name" }
    [IO.File]::WriteAllText((Join-Path $dist "$name.elf.txt"),$elf,[Text.UTF8Encoding]::new($false))
}
# Data resources are writable extraction payloads, never executable tools.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$resourceZip = Join-Path $dist "cmake-4.1.2-resources.zip"
$stream = [IO.File]::Open($resourceZip,[IO.FileMode]::Create)
$zip = [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($folder in @('Modules','Templates')) {
        foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $cmakeSource $folder) -Recurse -File | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($cmakeSource.Length + 1).Replace('\','/')
            $entry = $zip.CreateEntry($relative,[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]::new(2025,9,30,0,0,0,[TimeSpan]::Zero)
            $output = $entry.Open()
            try { $input = $file.OpenRead(); try { $input.CopyTo($output) } finally { $input.Dispose() } } finally { $output.Dispose() }
        }
    }
    # Preserve upstream and vendored third-party notices with the runtime.
    foreach ($file in (Get-ChildItem -LiteralPath $cmakeSource -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE|Copyright|CONTRIBUTORS)([._-].*)?$' -or $_.FullName.StartsWith((Join-Path $cmakeSource 'Licenses') + [IO.Path]::DirectorySeparatorChar) } | Sort-Object FullName)) {
        $relative = 'licenses/cmake/' + $file.FullName.Substring($cmakeSource.Length + 1).Replace('\','/')
        $entry = $zip.CreateEntry($relative,[IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]::new(2025,9,30,0,0,0,[TimeSpan]::Zero)
        $output = $entry.Open()
        try { $input = $file.OpenRead(); try { $input.CopyTo($output) } finally { $input.Dispose() } } finally { $output.Dispose() }
    }
    $entry = $zip.CreateEntry('licenses/ninja/COPYING',[IO.Compression.CompressionLevel]::Optimal)
    $entry.LastWriteTime = [DateTimeOffset]::new(2025,9,30,0,0,0,[TimeSpan]::Zero)
    $output = $entry.Open()
    try { $input = [IO.File]::OpenRead((Join-Path $ninjaSource 'COPYING')); try { $input.CopyTo($output) } finally { $input.Dispose() } } finally { $output.Dispose() }
    foreach ($notice in @('NOTICE','NOTICE.toolchain')) {
        $entry = $zip.CreateEntry("licenses/android-ndk/$notice",[IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]::new(2025,9,30,0,0,0,[TimeSpan]::Zero)
        $output = $entry.Open()
        try { $input = [IO.File]::OpenRead((Join-Path $ndk $notice)); try { $input.CopyTo($output) } finally { $input.Dispose() } } finally { $output.Dispose() }
    }
    $entry = $zip.CreateEntry('licenses/llvm/LICENSE.TXT',[IO.Compression.CompressionLevel]::Optimal)
    $entry.LastWriteTime = [DateTimeOffset]::new(2025,9,30,0,0,0,[TimeSpan]::Zero)
    $output = $entry.Open()
    try { $input = [IO.File]::OpenRead((Join-Path $llvmSource 'llvm/LICENSE.TXT')); try { $input.CopyTo($output) } finally { $input.Dispose() } } finally { $output.Dispose() }
} finally { $zip.Dispose(); $stream.Dispose() }
$manifest = [ordered]@{
    schema = 1; abi = 'arm64-v8a'; minimumAndroidApi = 28; androidNdk = $NdkVersion
    cmake = [ordered]@{ version = '4.1.2'; source = 'https://cmake.org/files/v4.1/cmake-4.1.2.tar.gz'; sourceSha256 = Hash $cmakeArchive; commit = '11f3f34077ba6fc1a2be6fe0f1ae542ebf884675'; license = 'BSD-3-Clause' }
    ninja = [ordered]@{ version = '1.12.1'; source = 'https://codeload.github.com/ninja-build/ninja/tar.gz/refs/tags/v1.12.1'; sourceSha256 = Hash $ninjaArchive; commit = '2daa09ba270b0a43e1929d29b073348aa985dfaa'; license = 'Apache-2.0' }
    llvmAr = [ordered]@{ version = '23.1.3'; source = 'https://github.com/llvm/llvm-project/tree/llvmorg-23.1.3'; commit = $llvmCommit; license = 'Apache-2.0 WITH LLVM-exception'; inputs = [ordered]@{} }
    cppRuntime = 'static libc++'; generator = 'Ninja'; resourceEnvironment = 'LGP_CMAKE_ROOT'
    buildScriptSha256 = Hash $PSCommandPath
    extractorSha256 = Hash (Join-Path $PSScriptRoot 'extract-cmake-source.py')
    llvmArBuildScriptSha256 = Hash (Join-Path $PSScriptRoot 'llvm-ar/CMakeLists.txt')
    profile = @('arm64-v8a','android-28','MinSizeRel','c++_static','BUILD_TESTING=OFF','CMAKE_USE_SYSTEM_LIBRARIES=OFF','CMAKE_USE_OPENSSL=OFF','CMake_ENABLE_DEBUGGER=OFF','-Wl,-z,max-page-size=16384')
    patches = [ordered]@{}; files = [ordered]@{}
}
foreach ($patch in (Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.patch' | Sort-Object Name)) { $manifest.patches[$patch.Name] = Hash $patch.FullName }
$llvmComponents = @('WebAssemblyAsmParser','WebAssemblyDesc','WebAssemblyInfo','BinaryFormat','Core','LibDriver','Object','Support','TargetParser','CodeGenTypes','MCParser','MC','DebugInfoDWARFLowLevel','IRReader','AsmParser','TextAPI','Option','BitReader','Remarks','BitstreamReader','Demangle')
foreach ($component in $llvmComponents) {
    $name = "libLLVM$component.a"
    $manifest.llvmAr.inputs[$name] = Hash (Join-Path $llvmBuild "lib/$name")
}
foreach ($relative in @('llvm/tools/llvm-ar/llvm-ar.cpp','llvm/lib/ToolDrivers/llvm-dlltool/DlltoolDriver.cpp','llvm/lib/ToolDrivers/llvm-dlltool/Options.td','llvm/LICENSE.TXT')) { $manifest.llvmAr.inputs[$relative] = Hash (Join-Path $llvmSource $relative) }
$manifest.llvmAr.inputs['generated/llvm-ar-driver.cpp'] = Hash (Join-Path $llvmBuild 'tools/llvm-ar/llvm-ar-driver.cpp')
foreach ($name in @('liblgp_cmake.so','liblgp_ninja.so','liblgp_llvm_ar.so','cmake-4.1.2-resources.zip')) { $manifest.files[$name] = Hash (Join-Path $dist $name) }
[IO.File]::WriteAllText((Join-Path $dist 'manifest.json'),($manifest | ConvertTo-Json -Depth 6) + "`n",[Text.UTF8Encoding]::new($false))
Write-Host "Android CMake/Ninja packaged: $dist"
