@echo off
setlocal

:: AlphaMiner GPU Setup Script for Windows
:: =======================================

echo AlphaMiner GPU Setup
echo ====================
echo.

:: Check if CUDA is installed
echo Checking for CUDA Toolkit...
if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v*" (
    echo ✅ CUDA Toolkit found
    for /d %%i in ("C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v*") do (
        set CUDA_PATH=%%i
        echo CUDA Path: !CUDA_PATH!
    )
) else (
    echo ❌ CUDA Toolkit not found
    echo.
    echo Please download and install CUDA Toolkit 12.x from:
    echo https://developer.nvidia.com/cuda-toolkit-archive
    echo.
    echo Installation steps:
    echo 1. Download CUDA Toolkit 12.x
    echo 2. Run installer with Express installation
    echo 3. Reboot your system
    echo 4. Run this script again
    echo.
    pause
    exit /b 1
)

:: Check for NVIDIA GPU
echo.
echo Checking for NVIDIA GPU...
nvidia-smi >nul 2>&1
if %errorlevel% equ 0 (
    echo ✅ NVIDIA GPU found
    nvidia-smi --query-gpu=name --format=csv,noheader | find /v "Name"
) else (
    echo ❌ No NVIDIA GPU found
    echo GPU mining requires NVIDIA GPU with CUDA support
    pause
    exit /b 1
)

:: Create build directories
echo.
echo Setting up build environment...
if not exist "build" mkdir build
if not exist "build-gpu" mkdir build-gpu

:: Build CPU version
echo.
echo Building CPU version...
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_WINDOWS=ON
cmake --build . --config Release
cd ..

:: Build GPU version
echo.
echo Building GPU version...
cd build-gpu
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_WINDOWS=ON -DENABLE_CUDA=ON
cmake --build . --config Release
cd ..

:: Copy GPU executable
echo.
echo Copying GPU executable...
if exist "build-gpu\Release\%EXE_NAME%" (
    copy "build-gpu\Release\%EXE_NAME%" "AlphaMiner-GPU.exe"
    echo ✅ GPU executable created: AlphaMiner-GPU.exe
) else (
    echo ❌ GPU build failed
    pause
    exit /b 1
)

:: Copy CPU executable
echo.
echo Copying CPU executable...
if exist "build\Release\%EXE_NAME%" (
    copy "build\Release\%EXE_NAME%" "AlphaMiner-CPU.exe"
    echo ✅ CPU executable created: AlphaMiner-CPU.exe
) else (
    echo ❌ CPU build failed
    pause
    exit /b 1
)

echo.
echo ✅ Setup completed successfully!
echo.
echo Available executables:
echo - AlphaMiner-CPU.exe (CPU-only mode)
echo - AlphaMiner-GPU.exe (GPU mode)
echo - AlphaMiner.exe (auto-detect, default)
echo.
echo Usage:
echo - CPU: AlphaMiner-CPU.exe ^<wallet^> ^<worker^> [threads] [host] [port]
echo - GPU: AlphaMiner-GPU.exe ^<wallet^> ^<worker^> [threads] [host] [port]
echo.
echo Example: AlphaMiner-CPU.exe EQVU... rig01 4 qubic.alphapool.tech 7777
echo.