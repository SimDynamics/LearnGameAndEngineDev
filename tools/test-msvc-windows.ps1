# Windows Developer PowerShell: the same C++23 profile as the application.
param([switch]$StrictVersion)

$ErrorActionPreference = "Stop"
$toolProfile = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '..\toolchains\windows.versions.json') | ConvertFrom-Json
$expected = $toolProfile.visualStudio.compilerVersion
$standardOption = $toolProfile.cpp.msvcStandardOption
if ($toolProfile.cpp.standard -ne 23 -or $standardOption -notmatch '^/std:c\+\+23(preview)?$') {
    throw 'El perfil Windows debe seleccionar explícitamente C++23.'
}
$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw "No se encontró cl.exe. Abre Developer PowerShell for Visual Studio."
}
$info = (& cl.exe 2>&1 | Out-String)
Write-Host $info
if ($StrictVersion -and (Get-Item -LiteralPath $compiler.Source).VersionInfo.FileVersion -ne $expected) {
    throw "Se esperaba el compilador MSVC $expected del manifiesto Windows."
}
$source = @'
#include <array>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>
#include <expected>
static_assert(__cplusplus == 202302L && _MSVC_LANG == 202302L);
constexpr int cpp23() { if consteval { return 23; } else { return 0; } }
static_assert(cpp23() == 23);
int main() {
    std::expected<int, int> result = 23;
    std::array<float, 3> point{1, 2, 3};
    std::span<float> data{point};
    std::vector<int> samples{3, 5, 8};
    constexpr std::string_view title = "LearnGraphicsProgramming";
    if (data.size() != 3 || samples.size() != 3 || title.empty() || result.value() != 23) return 1;
    std::cout << title << ": MSVC OK\n";
}
'@
$temp = Join-Path $env:TEMP ("lgp-msvc-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $sourceFile = Join-Path $temp "smoke.cpp"
    Set-Content -Path $sourceFile -Value $source -Encoding UTF8
    & cl.exe /nologo /EHsc $standardOption /Zc:__cplusplus /permissive- /utf-8 "/Fe:$temp\smoke.exe" "/Fo:$temp\smoke.obj" $sourceFile
    if ($LASTEXITCODE -ne 0) { throw "Falló la compilación MSVC" }
    & "$temp\smoke.exe"
    if ($LASTEXITCODE -ne 0) { throw "Falló la ejecución MSVC" }
    Write-Host "MSVC / C++23: OK" -ForegroundColor Green
} finally {
    $verifiedTemporaryRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    $verifiedTemporaryTarget = [IO.Path]::GetFullPath($temp)
    if (-not $verifiedTemporaryTarget.StartsWith($verifiedTemporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'La carpeta temporal de la prueba no está dentro de TEMP.'
    }
    Remove-Item -LiteralPath $verifiedTemporaryTarget -Recurse -Force -ErrorAction SilentlyContinue
}
