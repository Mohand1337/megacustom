@echo off
REM MegaCustomGUI Windows Build - Double-click to run
REM
REM Prerequisites:
REM   1. Visual Studio 2022 (Desktop C++ workload)
REM   2. Qt 6.6+ for MSVC: https://www.qt.io/download-qt-installer
REM   3. vcpkg: git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
REM            then run: C:\vcpkg\bootstrap-vcpkg.bat
REM
REM Edit the paths below if your installations are different:

set QT_PATH=C:\Qt\6.6.0\msvc2019_64
set VCPKG_PATH=C:\vcpkg

echo.
echo ========================================
echo   MegaCustomGUI Windows Build
echo ========================================
echo.
echo Qt Path:    %QT_PATH%
echo vcpkg Path: %VCPKG_PATH%
echo.

powershell -ExecutionPolicy Bypass -File "%~dp0build-windows-local.ps1" -QtPath "%QT_PATH%" -VcpkgPath "%VCPKG_PATH%"

echo.
pause
