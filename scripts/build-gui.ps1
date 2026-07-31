$cmake = "C:\vcpkg\downloads\tools\cmake-3.31.10-windows\cmake-3.31.10-windows-x86_64\bin\cmake.exe"
$vcpkg = "C:\vcpkg"
$qt = "C:\Qt\6.6.0\msvc2019_64"
$project = "C:\Users\Administrator\Desktop\megacustom"

# Configure
cd "$project\qt-gui"
$generatorArgs = @()
if (!(Test-Path ".\build-win64\CMakeCache.txt" -PathType Leaf)) {
    $generatorArgs = @("-G", "Visual Studio 17 2022", "-A", "x64")
}
& $cmake -B build-win64 @generatorArgs -DCMAKE_PREFIX_PATH="$qt" -DCMAKE_TOOLCHAIN_FILE="$vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { Write-Host "Configure failed!" -ForegroundColor Red; exit 1 }

# Build
& $cmake --build build-win64 --config Release --parallel
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed!" -ForegroundColor Red; exit 1 }

Write-Host "GUI built successfully!" -ForegroundColor Green
Write-Host "Exe at: $project\qt-gui\build-win64\Release\MegaCustomGUI.exe"
