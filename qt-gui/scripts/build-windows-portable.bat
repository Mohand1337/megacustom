@echo off
setlocal EnableDelayedExpansion

REM ============================================================================
REM MegaCustomGUI - Windows Portable Build Script
REM ============================================================================
REM
REM Prerequisites:
REM   1. Visual Studio 2022 (Community or higher) with C++ workload
REM   2. Qt 6.6+ for MSVC 2022 x64 (from qt.io or aqtinstall)
REM   3. VCPKG with required packages installed
REM   4. CMake 3.21+
REM
REM VCPKG packages needed:
REM   vcpkg install openssl:x64-windows curl:x64-windows sqlite3:x64-windows
REM   vcpkg install libsodium:x64-windows cryptopp:x64-windows icu:x64-windows
REM   vcpkg install c-ares:x64-windows zlib:x64-windows
REM
REM Usage:
REM   build-windows-portable.bat [Qt path] [VCPKG root]
REM
REM Example:
REM   build-windows-portable.bat C:\Qt\6.6.0\msvc2022_64 C:\vcpkg
REM ============================================================================

echo.
echo ============================================
echo  MegaCustomGUI Portable Windows Build
echo ============================================
echo.

REM Configuration - modify these paths for your system
if "%~1"=="" (
    set QT_DIR=C:\Qt\6.6.0\msvc2022_64
) else (
    set QT_DIR=%~1
)

if "%~2"=="" (
    set VCPKG_ROOT=C:\vcpkg
) else (
    set VCPKG_ROOT=%~2
)

set BUILD_DIR=build-win64
set DEPLOY_DIR=MegaCustomGUI-Portable
set PROJECT_ROOT=%~dp0..

REM Validate paths
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo ERROR: Qt not found at %QT_DIR%
    echo Please install Qt 6.6+ for MSVC 2022 x64
    echo Or specify the Qt path as the first argument
    exit /b 1
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo ERROR: VCPKG not found at %VCPKG_ROOT%
    echo Please install VCPKG or specify the path as the second argument
    exit /b 1
)

echo Qt Directory: %QT_DIR%
echo VCPKG Root: %VCPKG_ROOT%
echo Build Directory: %BUILD_DIR%
echo Deploy Directory: %DEPLOY_DIR%
echo.

REM ============================================================================
REM Step 1: Build MEGA SDK (if not already built)
REM ============================================================================
echo [1/6] Checking MEGA SDK...

set SDK_DIR=%PROJECT_ROOT%\..\third_party\sdk
set SDK_BUILD_DIR=%SDK_DIR%\build_sdk

if not exist "%SDK_BUILD_DIR%\Release\SDKlib.lib" (
    echo Building MEGA SDK...

    if not exist "%SDK_BUILD_DIR%" mkdir "%SDK_BUILD_DIR%"
    pushd "%SDK_BUILD_DIR%"

    cmake .. -G "Visual Studio 17 2022" -A x64 ^
        -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DENABLE_SYNC=ON ^
        -DENABLE_CHAT=OFF ^
        -DUSE_OPENSSL=ON

    if errorlevel 1 (
        echo ERROR: SDK CMake configuration failed
        popd
        exit /b 1
    )

    cmake --build . --config Release --parallel

    if errorlevel 1 (
        echo ERROR: SDK build failed
        popd
        exit /b 1
    )

    popd
    echo MEGA SDK built successfully.
) else (
    echo MEGA SDK already built.
)
echo.

REM ============================================================================
REM Step 2: Configure Qt GUI
REM ============================================================================
echo [2/6] Configuring MegaCustomGUI...

pushd "%PROJECT_ROOT%"

if exist "%BUILD_DIR%" (
    echo Cleaning previous build...
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"

cmake -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH=%QT_DIR% ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo ERROR: CMake configuration failed
    popd
    exit /b 1
)
echo.

REM ============================================================================
REM Step 3: Build
REM ============================================================================
echo [3/6] Building MegaCustomGUI...

cmake --build "%BUILD_DIR%" --config Release --parallel

if errorlevel 1 (
    echo ERROR: Build failed
    popd
    exit /b 1
)
echo.

REM ============================================================================
REM Step 4: Create deployment directory
REM ============================================================================
echo [4/6] Creating deployment directory...

if exist "%DEPLOY_DIR%" (
    rmdir /s /q "%DEPLOY_DIR%"
)
mkdir "%DEPLOY_DIR%"

REM Copy executables
copy "%BUILD_DIR%\Release\MegaCustomGUI.exe" "%DEPLOY_DIR%\" >nul
copy "%BUILD_DIR%\Release\mega_ops.exe" "%DEPLOY_DIR%\" >nul 2>&1

echo Executables copied.
echo.

REM ============================================================================
REM Step 5: Deploy Qt libraries
REM ============================================================================
echo [5/6] Deploying Qt libraries...

"%QT_DIR%\bin\windeployqt.exe" ^
    --release ^
    --no-translations ^
    --no-system-d3d-compiler ^
    --no-opengl-sw ^
    --no-compiler-runtime ^
    "%DEPLOY_DIR%\MegaCustomGUI.exe"

if errorlevel 1 (
    echo WARNING: windeployqt had issues, continuing...
)

REM Copy VCPKG DLLs
echo Copying VCPKG dependencies...
set VCPKG_BIN=%VCPKG_ROOT%\installed\x64-windows\bin

for %%f in (
    libssl-3-x64.dll
    libcrypto-3-x64.dll
    libcurl.dll
    sqlite3.dll
    sodium.dll
    zlib1.dll
) do (
    if exist "%VCPKG_BIN%\%%f" (
        copy "%VCPKG_BIN%\%%f" "%DEPLOY_DIR%\" >nul
        echo   Copied %%f
    )
)

REM Copy ICU DLLs
for %%f in (
    icuuc74.dll
    icuin74.dll
    icudt74.dll
) do (
    if exist "%VCPKG_BIN%\%%f" (
        copy "%VCPKG_BIN%\%%f" "%DEPLOY_DIR%\" >nul
        echo   Copied %%f
    )
)

REM Copy VC++ Runtime (if not using static linking)
echo Copying VC++ Runtime...
set VC_REDIST=%VCToolsRedistDir%x64\Microsoft.VC143.CRT
if exist "%VC_REDIST%" (
    copy "%VC_REDIST%\vcruntime140.dll" "%DEPLOY_DIR%\" >nul 2>&1
    copy "%VC_REDIST%\vcruntime140_1.dll" "%DEPLOY_DIR%\" >nul 2>&1
    copy "%VC_REDIST%\msvcp140.dll" "%DEPLOY_DIR%\" >nul 2>&1
    copy "%VC_REDIST%\msvcp140_1.dll" "%DEPLOY_DIR%\" >nul 2>&1
    copy "%VC_REDIST%\concrt140.dll" "%DEPLOY_DIR%\" >nul 2>&1
    echo   Copied VC++ Runtime
) else (
    echo   WARNING: VC++ Runtime not found, app may require VC++ Redistributable
)

echo.

REM ============================================================================
REM Step 6: Copy resources and create portable marker
REM ============================================================================
echo [6/6] Copying resources...

REM Copy stylesheets
if not exist "%DEPLOY_DIR%\resources\styles" mkdir "%DEPLOY_DIR%\resources\styles"
copy "%PROJECT_ROOT%\resources\styles\*.qss" "%DEPLOY_DIR%\resources\styles\" >nul 2>&1

REM Copy icons (SVGs are in the QRC, but copy any external ones)
if not exist "%DEPLOY_DIR%\resources\icons" mkdir "%DEPLOY_DIR%\resources\icons"
copy "%PROJECT_ROOT%\resources\icons\*.svg" "%DEPLOY_DIR%\resources\icons\" >nul 2>&1
copy "%PROJECT_ROOT%\resources\icons\*.ico" "%DEPLOY_DIR%\resources\icons\" >nul 2>&1

REM Create portable marker file
echo MegaCustomGUI Portable Mode> "%DEPLOY_DIR%\portable.marker"
echo.
echo This file enables portable mode.>> "%DEPLOY_DIR%\portable.marker"
echo Settings will be stored in this folder instead of AppData.>> "%DEPLOY_DIR%\portable.marker"
echo Delete this file to use standard Windows settings location.>> "%DEPLOY_DIR%\portable.marker"

REM Create README
(
echo MegaCustomGUI Portable
echo ======================
echo.
echo This is a portable version of MegaCustomGUI.
echo.
echo To run: Double-click MegaCustomGUI.exe
echo.
echo Portable mode is enabled - all settings are stored in this folder.
echo To use standard Windows settings ^(AppData^), delete the portable.marker file.
echo.
echo System Requirements:
echo - Windows 10 or later ^(64-bit^)
echo - No installation required
echo.
echo Files:
echo - MegaCustomGUI.exe  : Main application
echo - mega_ops.exe       : Command-line tool ^(optional^)
echo - portable.marker    : Enables portable mode
echo - resources/         : Themes and icons
echo.
) > "%DEPLOY_DIR%\README.txt"

echo.

REM ============================================================================
REM Create ZIP archive
REM ============================================================================
echo Creating ZIP archive...

set ZIP_NAME=MegaCustomGUI-Portable-Win64.zip
if exist "%ZIP_NAME%" del "%ZIP_NAME%"

powershell -Command "Compress-Archive -Path '%DEPLOY_DIR%' -DestinationPath '%ZIP_NAME%' -Force"

if errorlevel 1 (
    echo WARNING: Could not create ZIP archive
) else (
    echo Created: %ZIP_NAME%
)

popd

echo.
echo ============================================
echo  Build Complete!
echo ============================================
echo.
echo Portable distribution: %DEPLOY_DIR%\
echo ZIP archive: %ZIP_NAME%
echo.
echo To test: Run %DEPLOY_DIR%\MegaCustomGUI.exe
echo.

endlocal
