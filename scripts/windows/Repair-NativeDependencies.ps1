[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $SourceRoot,
    [Parameter(Mandatory = $true)][string] $BinaryRoot,
    [Parameter(Mandatory = $true)][string] $Ninja,
    [string] $CompilerInfo,
    [string] $PrefixFile,
    [switch] $VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceDirectory = [System.IO.Path]::GetFullPath($SourceRoot).TrimEnd('\', '/')
$binaryDirectory = [System.IO.Path]::GetFullPath($BinaryRoot).TrimEnd('\', '/')
$objectDirectory = Join-Path $binaryDirectory 'CMakeFiles'
$epoch = [DateTime]::SpecifyKind([DateTime]::new(1970, 1, 1), [DateTimeKind]::Utc)

function Test-InDirectory([string] $Path, [string] $Directory) {
    return $Path.StartsWith($Directory + [System.IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)
}

function Get-CommandPath([string] $Directory, [string] $Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $Directory $Path))
}

function Assert-GeneratedPath([string] $Path) {
    if (-not (Test-InDirectory $Path $objectDirectory)) { throw "Native object is outside its build tree: $Path" }
    $current = $Path
    while (Test-InDirectory $current $binaryDirectory) {
        if ((Test-Path -LiteralPath $current) -and
                ((Get-Item -LiteralPath $current -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "Native object path crosses a reparse point: $current"
        }
        $current = Split-Path -Parent $current
    }
}

function Get-NativeToolOutput([string] $Tool) {
    $output = & $Ninja -C $binaryDirectory -t $Tool
    if ($LASTEXITCODE -ne 0) { throw "Ninja could not read native $Tool metadata." }
    return ($output -join "`n")
}

$cachePath = Join-Path $binaryDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
    $cache = [System.IO.File]::ReadAllText($cachePath)
    foreach ($location in @(@('CMAKE_HOME_DIRECTORY', $sourceDirectory), @('CMAKE_CACHEFILE_DIR', $binaryDirectory))) {
        $entry = [regex]::Match($cache, '(?m)^' + $location[0] + ':INTERNAL=([^\r\n]+)')
        if (-not $entry.Success -or -not [string]::Equals(
                [System.IO.Path]::GetFullPath($entry.Groups[1].Value).TrimEnd('\', '/'), $location[1],
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Native dependency repair directories do not match their CMake cache.'
        }
    }
}
if ($PrefixFile) {
    $prefixPath = [System.IO.Path]::GetFullPath($PrefixFile)
    Assert-GeneratedPath $prefixPath
    if ($prefixPath -ne (Join-Path $objectDirectory 'NativeDependencies\prefix.bin')) {
        throw 'Dependency prefix validation accepts only the native build probe.'
    }
    $prefixBytes = [System.IO.File]::ReadAllBytes($prefixPath)
    try {
        [void] ([System.Text.UTF8Encoding]::new($false, $true)).GetString($prefixBytes)
    } catch [System.Text.DecoderFallbackException] {
        throw 'MSVC emitted a non-UTF-8 dependency prefix. Use scripts/windows/Build-Native.ps1 or a private console with chcp 65001 for configure and build.'
    }
}
if ($CompilerInfo) {
    if (-not $PrefixFile -or $VerifyOnly) {
        throw 'Compiler prefix migration needs the native probe and cannot run in VerifyOnly mode.'
    }
    $compilerPath = [System.IO.Path]::GetFullPath($CompilerInfo)
    Assert-GeneratedPath $compilerPath
    $relativeCompiler = $compilerPath.Substring($objectDirectory.Length + 1)
    if ($relativeCompiler -notmatch '^[^\\/]+[\\/]CMakeCXXCompiler\.cmake$') {
        throw 'Compiler prefix migration accepts only versioned compiler metadata.'
    }
    # Latin1 maps every byte to the same numbered character. It deliberately
    # performs no UTF-8/OEM decoding, preserving the exact cl/Ninja byte prefix
    # and every other byte (including line endings) in the generated metadata.
    $byteEncoding = [System.Text.Encoding]::GetEncoding(28591)
    $compilerText = $byteEncoding.GetString([System.IO.File]::ReadAllBytes($compilerPath))
    $prefix = $byteEncoding.GetString($prefixBytes)
    if (-not $prefix -or $prefix -match '[\r\n\x00]') { throw 'The native dependency prefix is empty or multiline.' }
    $prefixLines = [regex]::Matches($compilerText, '(?m)^set\(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "[^\r\n]*"\)(?=\r?$)')
    if ($prefixLines.Count -ne 1) { throw 'Expected exactly one native compiler dependency prefix.' }
    $escapedPrefix = $prefix.Replace('\', '\\').Replace('"', '\"')
    $prefixLine = 'set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "' + $escapedPrefix + '")'
    if ($prefixLine -ne $prefixLines[0].Value) {
        $compilerText = $compilerText.Replace($prefixLines[0].Value, $prefixLine)
        [System.IO.File]::WriteAllBytes($compilerPath, $byteEncoding.GetBytes($compilerText))
    }
}
$buildGraph = Join-Path $binaryDirectory 'build.ninja'
if (-not (Test-Path -LiteralPath $buildGraph -PathType Leaf)) {
    return
}
# --fresh removes CMakeFiles before configure, but leaves build.ninja until
# generation. Its old include cannot be parsed by Ninja yet. New compiler and
# dependency probes are expected here; only target objects could need repair.
# Inline Ninja graphs remain valid without CMake's separate rules file.
if (-not (Test-Path -LiteralPath (Join-Path $objectDirectory 'rules.ninja') -PathType Leaf) -and
        [regex]::IsMatch([System.IO.File]::ReadAllText($buildGraph),
            '(?m)^include CMakeFiles[\\/]rules\.ninja\r?$')) {
    foreach ($target in (Get-ChildItem -LiteralPath $objectDirectory -Directory -Filter '*.dir')) {
        foreach ($item in (Get-ChildItem -LiteralPath $target.FullName -Recurse -File)) {
            if ($item.Name -match '\.obj(?:\.ddi)?$') {
                throw 'Native Ninja rules are missing while target objects remain. Configure with -Fresh before building.'
            }
        }
    }
    return
}
$dependencyCounts = [System.Collections.Generic.Dictionary[string, int]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in ((Get-NativeToolOutput 'deps') -split "`n")) {
    if ($line -match '^(.+\.obj(?:\.ddi)?): #deps (\d+),') {
        $relativeObject = $Matches[1].Replace('/', '\')
        $dependencyCounts[$relativeObject] = [int] $Matches[2]
    }
}
$commands = ConvertFrom-Json (Get-NativeToolOutput 'compdb')
$damagedObjects = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$changedObjects = 0
foreach ($command in $commands) {
    if (-not $command.output.EndsWith('.obj', [StringComparison]::OrdinalIgnoreCase)) { continue }
    $relativeObject = $command.output.Replace('/', '\')
    $hasEmptyObject = $dependencyCounts.ContainsKey($relativeObject) -and $dependencyCounts[$relativeObject] -eq 0
    $hasEmptyScan = $dependencyCounts.ContainsKey($relativeObject + '.ddi') -and $dependencyCounts[$relativeObject + '.ddi'] -eq 0
    if (-not $hasEmptyObject -and -not $hasEmptyScan) { continue }
    $source = Get-CommandPath $command.directory $command.file
    if (-not (Test-InDirectory $source $sourceDirectory) -or -not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
    $hasLocalHeader = $false
    $text = [System.IO.File]::ReadAllText($source)
    foreach ($include in [regex]::Matches($text, '(?m)^\s*#\s*include\s*"([^"\r\n]+)"')) {
        foreach ($includeDirectory in @((Split-Path -Parent $source), (Join-Path $sourceDirectory 'native'), (Join-Path $binaryDirectory 'generated'))) {
            $header = [System.IO.Path]::GetFullPath((Join-Path $includeDirectory $include.Groups[1].Value))
            if (((Test-InDirectory $header $sourceDirectory) -or (Test-InDirectory $header $binaryDirectory)) -and
                    (Test-Path -LiteralPath $header -PathType Leaf)) {
                $hasLocalHeader = $true
                break
            }
        }
        if ($hasLocalHeader) { break }
    }
    if (-not $hasLocalHeader) { continue }
    $object = Get-CommandPath $command.directory $command.output
    Assert-GeneratedPath $object
    if (-not (Test-Path -LiteralPath $object -PathType Leaf)) { continue }
    if (-not $damagedObjects.Add($object)) { continue }
    if ($VerifyOnly) { continue }
    $item = Get-Item -LiteralPath $object
    if ($item.LastWriteTimeUtc -ne $epoch) {
        $item.LastWriteTimeUtc = $epoch
        ++$changedObjects
    }
    # C++ module scanning has its own header edges. Recreate those too, rather than
    # retaining an empty scan graph after the ordinary object is rebuilt.
    $scan = $object + '.ddi'
    if (Test-Path -LiteralPath $scan -PathType Leaf) {
        Assert-GeneratedPath $scan
        (Get-Item -LiteralPath $scan).LastWriteTimeUtc = $epoch
    }
}
if ($VerifyOnly -and $damagedObjects.Count -ne 0) {
    throw "Native header dependency tracking is missing for $($damagedObjects.Count) object(s)."
}
if ($changedObjects -ne 0) {
    Write-Output "Rebuilding $changedObjects native object(s) with missing header dependencies."
}
