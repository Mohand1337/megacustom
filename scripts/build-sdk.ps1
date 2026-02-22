$cmake = "C:\vcpkg\downloads\tools\cmake-3.31.10-windows\cmake-3.31.10-windows-x86_64\bin\cmake.exe"
$vcpkg = "C:\vcpkg"

# Ensure libcrypto++ alias exists
Copy-Item "$vcpkg\installed\x64-windows\lib\pkgconfig\cryptopp.pc" "$vcpkg\installed\x64-windows\lib\pkgconfig\libcrypto++.pc" -Force -ErrorAction SilentlyContinue

# Configure
cd C:\Users\Administrator\Desktop\megacustom\third_party\sdk\build_sdk
& $cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE="$vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_BUILD_TYPE=Release -DENABLE_SYNC=ON -DENABLE_CHAT=OFF -DENABLE_LOG_PERFORMANCE=OFF -DENABLE_SDKLIB_EXAMPLES=OFF -DENABLE_SDKLIB_TESTS=OFF -DUSE_OPENSSL=ON -DUSE_CURL=ON -DUSE_SODIUM=ON -DUSE_CRYPTOPP=ON -DUSE_SQLITE=ON -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_MEDIAINFO=OFF -DUSE_LIBUV=ON -DUSE_PDFIUM=OFF
if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed!" -ForegroundColor Red; exit 1 }

# Build
& $cmake --build . --config Release --target SDKlib --parallel
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed!" -ForegroundColor Red; exit 1 }

Write-Host "SDK built successfully!" -ForegroundColor Green
