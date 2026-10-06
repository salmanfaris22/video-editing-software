# Build a portable 64-bit Windows app folder + ZIP (Windows 10 1903+ / Windows 11).
# Run in PowerShell from the repo root on a Windows PC with Visual Studio, Qt 6.8+, and vcpkg.
param(
    [string]$BuildDir = "build\win",
    [string]$InstallDir = "dist\Lectern",
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$QtDir = $env:Qt6_DIR
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

if (-not $VcpkgRoot) { throw "Set VCPKG_ROOT to your vcpkg clone (e.g. C:\vcpkg)" }
if (-not $QtDir) { throw "Set Qt6_DIR to Qt's lib/cmake/Qt6 (e.g. C:\Qt\6.8\msvc2022_64\lib\cmake\Qt6)" }

$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
cmake -S . -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=$Toolchain `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    -DQt6_DIR=$QtDir `
    -DLECTERN_BUILD_TESTS=OFF

cmake --build $BuildDir --target lectern
cmake --install $BuildDir --prefix $InstallDir --component app
cmake --build $BuildDir --target package

Write-Host ""
Write-Host "Portable app: $InstallDir\$env:LECTERN_PRODUCT_NAME.exe (or Lectern.exe)"
Write-Host "ZIP package:  $BuildDir\package\Lectern-*-win64.zip"
