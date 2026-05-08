#!/bin/bash
set -euo pipefail

# AlphaMiner Windows Package Creator
# ===================================

PACKAGE_NAME="AlphaMiner-Windows-v0.3.0"
PACKAGE_DIR="/tmp/$PACKAGE_NAME"
FINAL_PACKAGE="$PACKAGE_NAME.zip"

echo "Creating Windows package: $FINAL_PACKAGE"
echo "====================================="

# Clean up any existing package
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

# Copy source files
echo "Copying source files..."
cp CMakeLists.txt "$PACKAGE_DIR/CMakeLists.txt"
cp CMakeLists.txt.windows "$PACKAGE_DIR/CMakeLists.txt.windows"
cp README.md "$PACKAGE_DIR/README-Linux.md"
cp README-Windows.md "$PACKAGE_DIR/README.md"
cp config.json "$PACKAGE_DIR/"
cp run.bat "$PACKAGE_DIR/"
cp install-gpu.bat "$PACKAGE_DIR/"
cp uninstall.bat "$PACKAGE_DIR/"

# Copy source code
mkdir -p "$PACKAGE_DIR/src"
cp -r src/* "$PACKAGE_DIR/src/"

# Create Windows build directory
mkdir -p "$PACKAGE_DIR/build-windows"

# Create package info
cat > "$PACKAGE_DIR/PACKAGE_INFO.txt" << EOF
AlphaMiner Windows Package v0.3.0
=================================

Package Date: $(date)
Package Version: v0.3.0
Build Type: Release

Contents:
- AlphaMiner.exe (auto-detect: GPU if available, else CPU)
- AlphaMiner-CPU.exe (CPU-only)
- AlphaMiner-GPU.exe (GPU-only)
- config.json (configuration file)
- run.bat (launcher script)
- install-gpu.bat (GPU setup)
- uninstall.bat (cleanup script)
- README.md (Windows documentation)
- src/ (source code)
- build-windows/ (build directory)

Requirements:
- Windows 10 or later (64-bit)
- CPU with AVX2 support
- For GPU: NVIDIA GPU with CUDA 12.x
- Visual Studio 2022 (Community free)
- CUDA Toolkit 12.x (for GPU mining)

Usage:
1. Extract package to desired location
2. Run install-gpu.bat (optional, for GPU support)
3. Run run.bat to start mining
4. Follow prompts with wallet, worker, etc.

Build from source:
mkdir build-windows
cd build-windows
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_WINDOWS=ON
cmake --build . --config Release

EOF

# Create checksum file
echo "Creating checksums..."
cd "$PACKAGE_DIR"
sha256sum * > "$PACKAGE_DIR/CHECKSUMS.txt"
cd /tmp

# Create final zip package
echo "Creating final package..."
zip -r "$FINAL_PACKAGE" "$PACKAGE_NAME/"

# Move to current directory
mv "$FINAL_PACKAGE" ./

# Clean up
rm -rf "$PACKAGE_DIR"

echo "✅ Package created successfully: $FINAL_PACKAGE"
echo "📦 Package size: $(du -h "$FINAL_PACKAGE" | cut -f1)"
echo "📋 Package contents:"
echo "  - AlphaMiner.exe (auto-detect)"
echo "  - AlphaMiner-CPU.exe (CPU-only)"
echo "  - AlphaMiner-GPU.exe (GPU-only)"
echo "  - config.json"
echo "  - run.bat (launcher)"
echo "  - install-gpu.bat (GPU setup)"
echo "  - uninstall.bat (cleanup)"
echo "  - README.md"
echo "  - src/ (source code)"
echo ""
echo "Ready for Windows deployment!"