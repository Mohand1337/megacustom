# MegaCustomGUI Local Windows Build Script
# Run this in PowerShell after installing prerequisites

param(
    [string]$QtPath = "C:\Qt\6.6.0\msvc2019_64",
    [string]$VcpkgPath = "C:\vcpkg",
    [switch]$SkipSdk,
    [switch]$SkipVcpkg
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  MegaCustomGUI Windows Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check prerequisites
Write-Host "[1/6] Checking prerequisites..." -ForegroundColor Yellow

if (!(Test-Path "$QtPath\bin\qmake.exe")) {
    Write-Host "ERROR: Qt not found at $QtPath" -ForegroundColor Red
    Write-Host "Download Qt from: https://www.qt.io/download-qt-installer" -ForegroundColor Yellow
    Write-Host "Install Qt 6.6.x with MSVC 2019 64-bit component" -ForegroundColor Yellow
    exit 1
}
Write-Host "  Qt found: $QtPath" -ForegroundColor Green

if (!(Test-Path "$VcpkgPath\vcpkg.exe")) {
    Write-Host "ERROR: vcpkg not found at $VcpkgPath" -ForegroundColor Red
    Write-Host "Run these commands to install:" -ForegroundColor Yellow
    Write-Host "  cd C:\" -ForegroundColor White
    Write-Host "  git clone https://github.com/microsoft/vcpkg.git" -ForegroundColor White
    Write-Host "  .\vcpkg\bootstrap-vcpkg.bat" -ForegroundColor White
    exit 1
}
Write-Host "  vcpkg found: $VcpkgPath" -ForegroundColor Green

# Check for Visual Studio
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    Write-Host "  Visual Studio found: $vsPath" -ForegroundColor Green
} else {
    Write-Host "WARNING: Could not detect Visual Studio" -ForegroundColor Yellow
}

# Install vcpkg packages
if (!$SkipVcpkg) {
    Write-Host ""
    Write-Host "[2/6] Installing vcpkg packages (this may take a while first time)..." -ForegroundColor Yellow

    $packages = "openssl", "curl", "sqlite3", "libsodium", "cryptopp", "c-ares", "zlib", "freeimage", "ffmpeg", "libmediainfo", "libuv"
    & "$VcpkgPath\vcpkg.exe" install --triplet x64-windows $packages

    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: vcpkg install failed" -ForegroundColor Red
        exit 1
    }
    Write-Host "  Packages installed" -ForegroundColor Green
} else {
    Write-Host "[2/6] Skipping vcpkg (--SkipVcpkg)" -ForegroundColor Gray
}

# Clone SDK if needed
Write-Host ""
Write-Host "[3/6] Checking MEGA SDK..." -ForegroundColor Yellow
$sdkPath = "$ProjectRoot\third_party\sdk"

if (!(Test-Path $sdkPath)) {
    Write-Host "  Cloning MEGA SDK..." -ForegroundColor White
    New-Item -ItemType Directory -Path "$ProjectRoot\third_party" -Force | Out-Null
    git clone --depth 1 https://github.com/meganz/sdk.git $sdkPath
}
Write-Host "  SDK ready: $sdkPath" -ForegroundColor Green

# Build SDK
if (!$SkipSdk) {
    Write-Host ""
    Write-Host "[4/6] Building MEGA SDK..." -ForegroundColor Yellow

    $sdkBuild = "$sdkPath\build_sdk"
    if (!(Test-Path $sdkBuild)) { New-Item -ItemType Directory -Path $sdkBuild | Out-Null }

    Push-Location $sdkBuild
    cmake .. -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_TOOLCHAIN_FILE="$VcpkgPath/scripts/buildsystems/vcpkg.cmake" `
        -DVCPKG_OVERLAY_PORTS="../cmake/vcpkg_overlay_ports" `
        -DVCPKG_OVERLAY_TRIPLETS="../cmake/vcpkg_overlay_triplets" `
        -DCMAKE_BUILD_TYPE=Release `
        -DENABLE_SYNC=ON -DENABLE_CHAT=OFF `
        -DENABLE_LOG_PERFORMANCE=OFF `
        -DENABLE_SDKLIB_EXAMPLES=OFF -DENABLE_SDKLIB_TESTS=OFF `
        -DUSE_OPENSSL=ON -DUSE_CURL=ON -DUSE_SODIUM=ON `
        -DUSE_CRYPTOPP=ON -DUSE_SQLITE=ON `
        -DUSE_FREEIMAGE=ON -DUSE_FFMPEG=ON `
        -DUSE_MEDIAINFO=ON -DUSE_LIBUV=ON -DUSE_PDFIUM=OFF

    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        Write-Host "ERROR: SDK cmake configuration failed" -ForegroundColor Red
        exit 1
    }

    cmake --build . --config Release --parallel

    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        Write-Host "ERROR: SDK build failed" -ForegroundColor Red
        exit 1
    }
    Pop-Location
    Write-Host "  SDK built successfully" -ForegroundColor Green
} else {
    Write-Host "[4/6] Skipping SDK build (--SkipSdk)" -ForegroundColor Gray
}

# Build Qt GUI
Write-Host ""
Write-Host "[5/6] Building MegaCustomGUI..." -ForegroundColor Yellow

$guiPath = "$ProjectRoot\qt-gui"
$guiBuild = "$guiPath\build-win64"

Push-Location $guiPath
cmake -B build-win64 -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH="$QtPath" `
    -DCMAKE_TOOLCHAIN_FILE="$VcpkgPath/scripts/buildsystems/vcpkg.cmake" `
    -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Host "ERROR: GUI cmake configuration failed" -ForegroundColor Red
    exit 1
}

cmake --build build-win64 --config Release --parallel

if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Host "ERROR: GUI build failed" -ForegroundColor Red
    exit 1
}
Pop-Location
Write-Host "  GUI built successfully" -ForegroundColor Green

# Create portable distribution
Write-Host ""
Write-Host "[6/6] Creating portable distribution..." -ForegroundColor Yellow

$deployDir = "$guiPath\MegaCustomGUI-Portable"
if (Test-Path $deployDir) { Remove-Item -Recurse -Force $deployDir }
New-Item -ItemType Directory -Path $deployDir | Out-Null

# Copy executable
Copy-Item "$guiBuild\Release\MegaCustomGUI.exe" $deployDir\

# Run windeployqt
& "$QtPath\bin\windeployqt.exe" --release --no-translations "$deployDir\MegaCustomGUI.exe"

# Copy vcpkg DLLs
$vcpkgBin = "$VcpkgPath\installed\x64-windows\bin"
Get-ChildItem "$vcpkgBin\*.dll" | Copy-Item -Destination $deployDir\

# Copy ffmpeg tools
$ffmpegTools = "$VcpkgPath\installed\x64-windows\tools\ffmpeg"
if (Test-Path $ffmpegTools) {
    Copy-Item "$ffmpegTools\ffmpeg.exe" $deployDir\ -ErrorAction SilentlyContinue
    Copy-Item "$ffmpegTools\ffprobe.exe" $deployDir\ -ErrorAction SilentlyContinue
}

# Copy resources
New-Item -ItemType Directory -Path "$deployDir\resources\styles" -Force | Out-Null
New-Item -ItemType Directory -Path "$deployDir\resources\icons" -Force | Out-Null
Copy-Item "$guiPath\resources\styles\*.qss" "$deployDir\resources\styles\" -ErrorAction SilentlyContinue
Copy-Item "$guiPath\resources\icons\*.ico" "$deployDir\resources\icons\" -ErrorAction SilentlyContinue

# Create portable marker
"MegaCustomGUI Portable Mode`n`nSettings stored in this folder." | Out-File "$deployDir\portable.marker" -Encoding UTF8

# Create ZIP
$zipPath = "$guiPath\MegaCustomGUI-Portable-Win64.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath }
Compress-Archive -Path $deployDir -DestinationPath $zipPath

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  BUILD COMPLETE!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Portable folder: $deployDir" -ForegroundColor White
Write-Host "ZIP archive:     $zipPath" -ForegroundColor White
Write-Host ""
Write-Host "To run: Double-click MegaCustomGUI.exe in the portable folder" -ForegroundColor Cyan
