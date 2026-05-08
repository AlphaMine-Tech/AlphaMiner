#!/usr/bin/env bash
set -euo pipefail

# Usage: ./scripts/build-gpu-linux.sh [build-dir]
BUILD_DIR="${1:-build-gpu}"

if ! command -v nvcc >/dev/null 2>&1; then
  echo "[!] nvcc not found. Install CUDA toolkit on this host first." >&2
  exit 1
fi

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON .
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "[+] Built: $BUILD_DIR/AlphaMiner"
"$BUILD_DIR/AlphaMiner" 2>/dev/null || true
