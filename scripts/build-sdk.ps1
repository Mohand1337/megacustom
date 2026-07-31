$cmake = "C:\vcpkg\downloads\tools\cmake-3.31.10-windows\cmake-3.31.10-windows-x86_64\bin\cmake.exe"
$vcpkg = "C:\vcpkg"

# Ensure libcrypto++ alias exists
Copy-Item "$vcpkg\installed\x64-windows\lib\pkgconfig\cryptopp.pc" "$vcpkg\installed\x64-windows\lib\pkgconfig\libcrypto++.pc" -Force -ErrorAction SilentlyContinue

# Configure
cd C:\Users\Administrator\Desktop\megacustom\third_party\sdk\build_sdk
$generatorArgs = @()
if (!(Test-Path ".\CMakeCache.txt" -PathType Leaf)) {
    $generatorArgs = @("-G", "Visual Studio 17 2022", "-A", "x64")
}
& $cmake .. @generatorArgs -DCMAKE_TOOLCHAIN_FILE="$vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_BUILD_TYPE=Release -DENABLE_SYNC=ON -DENABLE_CHAT=OFF -DENABLE_LOG_PERFORMANCE=OFF -DENABLE_SDKLIB_EXAMPLES=OFF -DENABLE_SDKLIB_TESTS=OFF -DUSE_OPENSSL=ON -DUSE_CURL=ON -DUSE_SODIUM=ON -DUSE_CRYPTOPP=ON -DUSE_SQLITE=ON -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_MEDIAINFO=OFF -DUSE_LIBUV=ON -DUSE_PDFIUM=OFF
if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed!" -ForegroundColor Red; exit 1 }

# Build both static libraries consumed directly by qt-gui/CMakeLists.txt.
& $cmake --build . --config Release --target SDKlib ccronexpr --parallel
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed!" -ForegroundColor Red; exit 1 }

$sdkArtifact = "Release\SDKlib.lib"
if (!(Test-Path $sdkArtifact -PathType Leaf)) {
    Write-Host "Required SDK artifact is missing: $sdkArtifact" -ForegroundColor Red
    exit 1
}
$ccronexprCandidates = @(
    "third_party\ccronexpr\ccronexpr.dir\Release\ccronexpr.lib",
    "third_party\ccronexpr\Release\ccronexpr.lib"
)
$ccronexprArtifact = $ccronexprCandidates |
    Where-Object { Test-Path $_ -PathType Leaf } |
    Select-Object -First 1
if (!$ccronexprArtifact) {
    Write-Host "Required ccronexpr artifact is missing. Checked: $($ccronexprCandidates -join ', ')" -ForegroundColor Red
    exit 1
}

Write-Host "SDK built successfully!" -ForegroundColor Green
