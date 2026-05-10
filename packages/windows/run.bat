@echo off
setlocal
chcp 65001 >nul

REM AlphaMiner launcher (Windows)
REM Modes: cpu | gpu | hybrid

set MODE=hybrid
set WALLET=YOUR_60_CHAR_QUBIC_WALLET_HERE
set WORKER=rig01
set CPU_THREADS=0
set GPU_THREADS=4096
set POOL_HOST=qubic.alphapool.tech
set POOL_PORT=7777

if /i "%MODE%"=="cpu" (
  set GPU_THREADS=0
)
if /i "%MODE%"=="gpu" (
  set CPU_THREADS=0
)

echo Starting AlphaMiner mode=%MODE%
echo Wallet=%WALLET%
echo Worker=%WORKER%
echo Pool=%POOL_HOST%:%POOL_PORT%
echo CPU threads=%CPU_THREADS%  GPU threads=%GPU_THREADS%
echo.

AlphaMiner.exe %WALLET% %WORKER% -t %CPU_THREADS% -g %GPU_THREADS% -p %POOL_HOST%:%POOL_PORT%
pause
