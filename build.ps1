# Сборка примера. Предполагается Qt6 из MSYS2 (ucrt64).
#   .\build.ps1          собрать
#   .\build.ps1 -Clean   пересобрать с нуля
param(
    [switch]$Clean,
    [string]$QtDir = "C:\msys64\ucrt64",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$env:PATH = "$QtDir\bin;C:\msys64\usr\bin;" + $env:PATH

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item $BuildDir -Recurse -Force
}

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QtDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "Готово: $BuildDir\simple-io-app.exe"
Write-Host "Запуск: .\run.cmd"
