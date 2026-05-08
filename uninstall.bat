@echo off
setlocal

:: AlphaMiner Uninstall Script for Windows
:: =======================================

echo AlphaMiner Uninstall
echo ====================
echo.

:: Remove executables
if exist "AlphaMiner.exe" (
    del "AlphaMiner.exe"
    echo ✅ Removed AlphaMiner.exe
)

if exist "AlphaMiner-CPU.exe" (
    del "AlphaMiner-CPU.exe"
    echo ✅ Removed AlphaMiner-CPU.exe
)

if exist "AlphaMiner-GPU.exe" (
    del "AlphaMiner-GPU.exe"
    echo ✅ Removed AlphaMiner-GPU.exe
)

:: Remove build directories
if exist "build" (
    rmdir /s /q "build"
    echo ✅ Removed build directory
)

if exist "build-gpu" (
    rmdir /s /q "build-gpu"
    echo ✅ Removed build-gpu directory
)

:: Remove config files
if exist "config.json" (
    del "config.json"
    echo ✅ Removed config.json
)

:: Remove log files
if exist "alphaminer.log" (
    del "alphaminer.log"
    echo ✅ Removed alphaminer.log
)

:: Remove log files with rotation
if exist "alphaminer.log.1" (
    del "alphaminer.log.1"
    echo ✅ Removed alphaminer.log.1
)

if exist "alphaminer.log.2" (
    del "alphaminer.log.2"
    echo ✅ Removed alphaminer.log.2
)

if exist "alphaminer.log.3" (
    del "alphaminer.log.3"
    echo ✅ Removed alphaminer.log.3
)

if exist "alphaminer.log.4" (
    del "alphaminer.log.4"
    echo ✅ Removed alphaminer.log.4
)

:: Remove shortcuts (if any)
if exist "%USERPROFILE%\Desktop\AlphaMiner.lnk" (
    del "%USERPROFILE%\Desktop\AlphaMiner.lnk"
    echo ✅ Removed desktop shortcut
)

:: Remove from PATH (if added)
echo.
echo Note: This script does not remove AlphaMiner from your PATH.
echo If you added AlphaMiner to PATH manually, please remove it manually.

echo.
echo ✅ Uninstall completed successfully!
echo.
echo Thank you for using AlphaMiner!
pause