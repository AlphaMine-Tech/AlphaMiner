# AlphaMiner for Windows

## Overview
AlphaMiner is a high-performance Qubic UPoW (Useful Proof of Work) miner for Windows, supporting both CPU and GPU mining modes.

## Features
- **CPU Mining**: Multi-threaded CPU mining with AVX2/AVX512 optimization
- **GPU Mining**: CUDA-accelerated GPU mining for NVIDIA GPUs
- **Hybrid Mode**: Auto-detect best available mining mode
- **Dev Fee**: 1.5% dev fee with automatic cycle management
- **Stratum Protocol**: Full compatibility with Qubic stratum pools
- **Windows Native**: Optimized for Windows performance

## Requirements
- Windows 10 or later (64-bit)
- CPU with AVX2 support (AVX512 recommended)
- For GPU mining: NVIDIA GPU with CUDA 12.x support
- Visual Studio 2022 (Community free edition)
- CUDA Toolkit 12.x (for GPU mining)

## Installation

### Quick Install (Recommended)
1. Download the latest AlphaMiner Windows package
2. Extract to a directory (e.g., `C:\AlphaMiner`)
3. Run `install-gpu.bat` to setup GPU support (optional)
4. Run `run.bat` to start mining

### Manual Install
1. Install Visual Studio 2022 Community Edition
2. Install CUDA Toolkit 12.x (for GPU mining)
3. Build from source using CMake
4. Copy executables to your preferred location

## Building from Source

### Prerequisites
```bash
# Install Visual Studio 2022 Community
# Install CUDA Toolkit 12.x
# Install CMake
```

### Build Commands
```cmd
# Clone repository
git clone <repository-url>
cd AlphaMiner

# Build CPU version
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_WINDOWS=ON -DENABLE_CUDA=OFF
cmake --build . --config Release

# Build GPU version
cd ..
mkdir build-gpu
cd build-gpu
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_WINDOWS=ON -DENABLE_CUDA=ON
cmake --build . --config Release
```

## Usage

### Basic Usage
```cmd
# CPU Mining (all cores)
AlphaMiner.exe <wallet> <worker>

# CPU Mining (specific threads)
AlphaMiner.exe <wallet> <worker> 8

# GPU Mining
AlphaMiner-GPU.exe <wallet> <worker>

# With custom pool
AlphaMiner.exe <wallet> <worker> 8 qubic.alphapool.tech 7777
```

### Command Line Parameters
- **wallet**: Your 60-character Qubic address
- **worker**: Worker name (e.g., `rig01`, `windows-pc`)
- **threads**: Number of CPU threads (0 = auto-detect)
- **host**: Pool hostname (default: `qubic.alphapool.tech`)
- **port**: Pool port (default: `7777`)

### Configuration File
Edit `config.json` to customize:
```json
{
  "pool": {
    "host": "qubic.alphapool.tech",
    "port": 7777
  },
  "mining": {
    "threads": 0,
    "enable_gpu": true,
    "gpu_device": 0
  },
  "logging": {
    "level": "info",
    "file": "alphaminer.log"
  }
}
```

## Executables
- **AlphaMiner.exe**: Auto-detect best mode (GPU if available, else CPU)
- **AlphaMiner-CPU.exe**: CPU-only mode
- **AlphaMiner-GPU.exe**: GPU-only mode

## GPU Mining Setup
1. Install NVIDIA drivers
2. Install CUDA Toolkit 12.x
3. Run `install-gpu.bat`
4. Verify GPU with `nvidia-smi`
5. Run `AlphaMiner-GPU.exe`

## Troubleshooting

### Common Issues
- **"CUDA not found"**: Install CUDA Toolkit 12.x
- **"No GPU detected"**: Check NVIDIA drivers and CUDA installation
- **"Connection failed"**: Check pool address and port
- **"Low performance"**: Verify CPU/GPU optimization settings

### Logs
Check `alphaminer.log` for detailed information:
```
[INFO] Connected to pool
[INFO] Subscribed to pool
[INFO] Mining started
[INFO] Hashrate: 1.2 MH/s
[INFO] Solution found
```

## Performance Optimization

### CPU Optimization
- Use AVX512 if available (`-DENABLE_AVX512=ON`)
- Match thread count to CPU cores
- Use `-march=native` for best performance

### GPU Optimization
- Use Pascal architecture or newer
- Optimize CUDA architecture flags
- Monitor GPU utilization with `nvidia-smi`

## Security
- Only download from official sources
- Keep your wallet address private
- Use secure network connections
- Monitor for unusual activity

## Support
- Check the official documentation
- Review troubleshooting guides
- Join community discussions
- Report issues to maintainers

## License
This software is for private use only. Please respect the license terms.

---

*AlphaMiner - High-Performance Qubic Miner for Windows*