[CmdletBinding()]
param(
    [string]$Root = 'E:\PersonalFiles\Projects\LearnGameAndEngineDev-Dep'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# One-time provisioning exclusively inside the project-specific dependency root.
# Never move existing dependencies, change Windows Security, trust stores,
# antivirus preferences, other projects or the installed MSVC/Android toolchains.
$root = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
$sdkDir = Join-Path $root 'WASI-SDK\34.0'
$runtimeDir = Join-Path $root 'Wasmtime\50.0.1'
$sdkCompiler = Join-Path $sdkDir 'bin\clang++.exe'
$sdkSysroot = Join-Path $sdkDir 'share\wasi-sysroot'
$runtimeExe = Join-Path $runtimeDir 'wasmtime.exe'

$packages = @(
    @{
        Name = 'WASI SDK 34.0 (Windows x64)'
        Uri = 'https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-34.0-x86_64-windows.tar.gz'
        Sha256 = 'cccb5c323a9b34f0349a9b09e8804a0a7632c68c3310f4b5f437ed57d7e71d8f'
        Kind = 'tar.gz'
        Destination = $sdkDir
        Binary = 'clang++.exe'
        Check = $sdkCompiler
    },
    @{
        Name = 'Wasmtime 50.0.1 (Windows x64)'
        Uri = 'https://github.com/bytecodealliance/wasmtime/releases/download/v50.0.1/wasmtime-v50.0.1-x86_64-windows.zip'
        Sha256 = 'c3646ee2de8cfcefe532e75852e1fd2728d78c3fcc43251794803a698d1c30ca'
        Kind = 'zip'
        Destination = $runtimeDir
        Binary = 'wasmtime.exe'
        Check = $runtimeExe
    }
)

# A named lock prevents Work and Codex builds racing over the same installation.
$mutex = [System.Threading.Mutex]::new($false, 'Local\LGAED_WasiTools_34_50')
$locked = $false
try {
    $locked = $mutex.WaitOne([TimeSpan]::FromMinutes(30))
    if (-not $locked) { throw 'Timeout waiting for the shared WASI installation lock.' }
    [System.IO.Directory]::CreateDirectory($root) | Out-Null
    foreach ($package in $packages) {
        if (Test-Path -LiteralPath $package.Check -PathType Leaf) { continue }
        if (Test-Path -LiteralPath $package.Destination) {
            throw "The WASI destination exists but is incomplete: $($package.Destination). Keep its files and investigate; do not overwrite it."
        }

        $stage = Join-Path $root ('._wasi_install_' + [guid]::NewGuid().ToString('N'))
        [System.IO.Directory]::CreateDirectory($stage) | Out-Null
        try {
            $archive = Join-Path $stage ('download.' + $package.Kind)
            $extracted = Join-Path $stage 'extracted'
            [System.IO.Directory]::CreateDirectory($extracted) | Out-Null
            Write-Host "Downloading official $($package.Name) archive."
            Invoke-WebRequest -Uri $package.Uri -OutFile $archive -MaximumRedirection 10 -ErrorAction Stop
            $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -cne $package.Sha256) {
                throw "SHA-256 verification failed for $($package.Name); refusing to install unverified bytes."
            }
            if ($package.Kind -eq 'zip') {
                Expand-Archive -LiteralPath $archive -DestinationPath $extracted
            } else {
                & tar.exe -xzf $archive -C $extracted
                if ($LASTEXITCODE -ne 0) { throw "Cannot extract verified $($package.Name) archive." }
            }
            $found = @(Get-ChildItem -LiteralPath $extracted -Recurse -File -Filter $package.Binary |
                Where-Object { $_.FullName -like '*\bin\clang++.exe' -or $_.Name -eq 'wasmtime.exe' })
            if ($found.Count -ne 1) { throw "Cannot uniquely locate $($package.Binary) in official archive." }
            $location = $found[0].Directory
            if ($package.Binary -eq 'clang++.exe') { $location = $location.Parent }
            # Official archives may contain a single versioned root directory.
            # Move that exact tree; never wipe a pre-existing installation.
            [System.IO.Directory]::CreateDirectory((Split-Path -Parent $package.Destination)) | Out-Null
            Move-Item -LiteralPath $location.FullName -Destination $package.Destination -ErrorAction Stop
            if (-not (Test-Path -LiteralPath $package.Check -PathType Leaf)) {
                throw "Installed $($package.Name) has no expected executable."
            }
        } finally {
            if (Test-Path -LiteralPath $stage) {
                Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
    if (-not (Test-Path -LiteralPath $sdkSysroot -PathType Container)) {
        throw "WASI SDK installed without its sysroot: $sdkSysroot"
    }
    # Versions are pinned by verified download hashes, not guessed from PATH.
    Write-Host "WASI tools ready: $sdkCompiler ; $runtimeExe"
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
