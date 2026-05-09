# AlphaMiner Windows Test — Right Now (30-minute gate)

Branch: `release/alphaminer-gpu-rc1`
Pool: `qubic.alphapool.tech:7777`

## 1) Prep (Windows)
1. Install **Visual Studio 2022** (Desktop development with C++)
2. Install **CMake**
3. Install **NVIDIA Driver** + **CUDA Toolkit 12.x**
4. Open **x64 Native Tools Command Prompt for VS 2022**

## 2) Build CPU binary
```cmd
cd C:\AlphaMiner
cmake -S . -B build-cpu -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=OFF
cmake --build build-cpu --config Release
```
CPU exe path:
- `build-cpu\Release\AlphaMiner.exe` (MSVC multi-config)
- or `build-cpu\AlphaMiner.exe` (single-config generators)

## 3) Build GPU binary
```cmd
cd C:\AlphaMiner
cmake -S . -B build-gpu -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON
cmake --build build-gpu --config Release
```
GPU exe path:
- `build-gpu\Release\AlphaMiner.exe` (MSVC multi-config)
- or `build-gpu\AlphaMiner.exe` (single-config generators)

## 4) 30-minute CPU run
```cmd
cd C:\AlphaMiner\build-cpu\Release
AlphaMiner.exe <WALLET> win-cpu 0 qubic.alphapool.tech 7777
```

## 5) 30-minute GPU run
```cmd
cd C:\AlphaMiner\build-gpu\Release
AlphaMiner.exe <WALLET> win-gpu 0 qubic.alphapool.tech 7777
```

## 6) Pass criteria
- Connects and subscribes successfully
- No crash/restart loop for 30 minutes
- Share/solution activity appears in miner output and pool side
- GPU run has no CUDA runtime errors

## 7) Quick evidence capture
Run each and save output:
```cmd
AlphaMiner.exe <WALLET> win-cpu 0 qubic.alphapool.tech 7777 > cpu-test.log 2>&1
AlphaMiner.exe <WALLET> win-gpu 0 qubic.alphapool.tech 7777 > gpu-test.log 2>&1
```

## 8) If GPU build fails on CUDA arch
Retry with explicit architecture flag:
```cmd
cmake -S . -B build-gpu -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=89
cmake --build build-gpu --config Release
```
Use `89` for Ada, `86` for Ampere, `75` for Turing if needed.
