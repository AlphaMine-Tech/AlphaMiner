@echo off
REM AlphaMiner v0.3.0 — Windows launcher
REM Edit the values below, then double-click run.bat

SET WALLET=YOUR_60_CHAR_QUBIC_WALLET_HERE
SET WORKER=rig01
SET THREADS=0
SET POOL_HOST=qubic.alphapool.tech
SET POOL_PORT=7777

REM 0 threads = use all available CPU cores
AlphaMiner.exe %WALLET% %WORKER% %THREADS% %POOL_HOST% %POOL_PORT%
pause
