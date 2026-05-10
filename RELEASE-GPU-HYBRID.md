# AlphaMiner Release Plan (GPU + Hybrid, Windows/Ubuntu/HiveOS)

## Artifact matrix

- `alphaminer-windows-x86_64-cpu-v<ver>.zip`
- `alphaminer-windows-x86_64-gpu-hybrid-v<ver>.zip` *(build on Windows w/ CUDA)*
- `alphaminer-ubuntu-x86_64-cpu-v<ver>.tar.gz`
- `alphaminer-ubuntu-x86_64-gpu-hybrid-v<ver>.tar.gz` *(build on Ubuntu w/ nvcc)*
- `alphaminer-hiveos-gpu-hybrid-v<ver>.tar.gz` *(from Ubuntu CUDA build)*

## Linux build host command

```bash
./packages/release-matrix.sh 1.0.2
```

This produces CPU artifacts always and CUDA artifacts when `nvcc` is installed.

## Windows CUDA build (required for GPU/Hybrid Windows)

On Windows (VS2022 + CUDA 12.x):

```powershell
cmake -S . -B build-gpu -G "Visual Studio 17 2022" -A x64 -DENABLE_CUDA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-gpu --config Release
```

Package:

- `build-gpu/Release/AlphaMiner.exe`
- `packages/windows/run.bat`
- `README-Windows.md`

Zip folder as: `alphaminer-windows-x86_64-gpu-hybrid-v<ver>.zip`

## Runtime modes

Launchers support:
- `cpu`
- `gpu`
- `hybrid`

Windows: edit `MODE` in `run.bat`.
Linux: `./run.sh <wallet> <worker> [mode] ...`
HiveOS: `h-run.sh` defaults to GPU+hybrid settings via `-t` and `-g`.
