# MegaCustomGUI Quick Rebuild Script
# Pull the latest fast-forward update, rebuild changed files, and update the portable folder.
# First-time setup: Run build-windows-local.ps1 first.
#
# Usage:
#   .\scripts\win-rebuild.ps1
#   .\scripts\win-rebuild.ps1 -PortableDir "C:\Users\Administrator\Desktop\MegaCustomGUI-Portable"

param(
    [string]$QtPath = "C:\Qt\6.6.0\msvc2019_64",
    [string]$VcpkgPath = "C:\vcpkg",
    [string]$PortableDir = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$GuiPath = "$ProjectRoot\qt-gui"
$BuildDir = "$GuiPath\build-win64"
$SdkBuildDir = "$ProjectRoot\third_party\sdk\build_sdk"

# Pull latest changes
Write-Host "Pulling latest changes..." -ForegroundColor Yellow
git -C $ProjectRoot pull --ff-only
if ($LASTEXITCODE -ne 0) {
    Write-Host "Pull failed; the build was not started." -ForegroundColor Red
    exit 1
}
Write-Host ""

$requiredSdkArtifacts = @(
    "$SdkBuildDir\Release\SDKlib.lib",
    "$SdkBuildDir\third_party\ccronexpr\Release\ccronexpr.lib"
)
foreach ($artifact in $requiredSdkArtifacts) {
    if (!(Test-Path $artifact -PathType Leaf)) {
        Write-Host "Required SDK artifact is missing: $artifact" -ForegroundColor Red
        Write-Host "Run .\scripts\build-windows-local.ps1 once, then retry." -ForegroundColor Yellow
        exit 1
    }
}

# Incremental build (only recompiles changed files)
Write-Host "Building..." -ForegroundColor Yellow
cmake --build $BuildDir --config Release --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
Write-Host "Build succeeded." -ForegroundColor Green

# Copy exe to portable folder
if ($PortableDir -and (Test-Path $PortableDir)) {
    Copy-Item "$BuildDir\Release\MegaCustomGUI.exe" "$PortableDir\" -Force
    Write-Host "Copied to $PortableDir" -ForegroundColor Green
} elseif (Test-Path "$GuiPath\MegaCustomGUI-Portable") {
    Copy-Item "$BuildDir\Release\MegaCustomGUI.exe" "$GuiPath\MegaCustomGUI-Portable\" -Force
    Write-Host "Copied to $GuiPath\MegaCustomGUI-Portable" -ForegroundColor Green
} else {
    Write-Host "Exe at: $BuildDir\Release\MegaCustomGUI.exe" -ForegroundColor Cyan
    Write-Host "No portable folder found. Use -PortableDir to specify one." -ForegroundColor Yellow
}
