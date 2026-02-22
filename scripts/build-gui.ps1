$cmake = "C:\vcpkg\downloads\tools\cmake-3.31.10-windows\cmake-3.31.10-windows-x86_64\bin\cmake.exe"
$vcpkg = "C:\vcpkg"
$qt = "C:\Qt\6.6.0\msvc2019_64"
$project = "C:\Users\Administrator\Desktop\megacustom"

# Configure
cd "$project\qt-gui"
& $cmake -B build-win64 -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="$qt" -DCMAKE_TOOLCHAIN_FILE="$vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed!" -ForegroundColor Red; exit 1 }

# Build
& $cmake --build build-win64 --config Release --parallel
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed!" -ForegroundColor Red; exit 1 }

Write-Host "GUI built successfully!" -ForegroundColor Green
Write-Host "Exe at: $project\qt-gui\build-win64\Release\MegaCustomGUI.exe"
