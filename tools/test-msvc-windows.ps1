# Windows Developer PowerShell: MSVC 14.50 (cl.exe 19.50.x).
# This verifies compatibility with a Windows C++20 subset used by Unreal.
param([switch]$StrictVersion)

$ErrorActionPreference = "Stop"
$expected = "19.50"
$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw "No se encontró cl.exe. Abre Developer PowerShell for Visual Studio."
}
$info = (& cl.exe 2>&1 | Out-String)
Write-Host $info
if ($StrictVersion -and ($info -notmatch [regex]::Escape("Version $expected"))) {
    throw "Se esperaba MSVC 14.50 (cl.exe $expected.x)."
}
$source = @'
#include <array>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>
int main() {
    std::array<float, 3> point{1, 2, 3};
    std::span<float> data{point};
    std::vector<int> samples{3, 5, 8};
    constexpr std::string_view title = "LearnGraphicsProgramming";
    if (data.size() != 3 || samples.size() != 3 || title.empty()) return 1;
    std::cout << title << ": MSVC OK\n";
}
'@
$temp = Join-Path $env:TEMP ("lgp-msvc-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $sourceFile = Join-Path $temp "smoke.cpp"
    Set-Content -Path $sourceFile -Value $source -Encoding UTF8
    & cl.exe /nologo /EHsc /std:c++20 /Zc:__cplusplus /permissive- /utf-8 "/Fe:$temp\smoke.exe" $sourceFile
    if ($LASTEXITCODE -ne 0) { throw "Falló la compilación MSVC" }
    & "$temp\smoke.exe"
    if ($LASTEXITCODE -ne 0) { throw "Falló la ejecución MSVC" }
    Write-Host "MSVC / C++20: OK" -ForegroundColor Green
} finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
