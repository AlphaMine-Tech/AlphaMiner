@echo off
setlocal

:: AlphaMiner Launcher Script for Windows
:: =======================================

set EXE_NAME=AlphaMiner.exe
set CONFIG_FILE=config.json
set LOG_FILE=alphaminer.log

echo Starting AlphaMiner...
echo ======================
echo Executable: %EXE_NAME%
echo Config: %CONFIG_FILE%
echo Log: %LOG_FILE%
echo.

:: Check if executable exists
if not exist "%EXE_NAME%" (
    echo Error: %EXE_NAME% not found!
    echo Please build the miner first or download the package.
    pause
    exit /b 1
)

:: Check config file
if not exist "%CONFIG_FILE%" (
    echo Warning: %CONFIG_FILE% not found, using defaults.
    echo.
)

:: Run the miner
echo Starting AlphaMiner...
echo ======================
echo Wallet: %1
echo Worker: %2
echo Threads: %3
echo Pool: %4
echo Port: %5
echo.

"%EXE_NAME%" %1 %2 %3 %4 %5

echo.
echo AlphaMiner stopped.
pause