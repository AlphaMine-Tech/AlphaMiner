#!/usr/bin/env bash
set -euo pipefail

# Build/package matrix for AlphaMiner
# Produces:
# - Ubuntu x86_64 (CPU)
# - Ubuntu x86_64 (CUDA: GPU + Hybrid) [requires nvcc]
# - HiveOS (CUDA: GPU + Hybrid) [requires CUDA build]
# - Windows x86_64 (CPU static, via MinGW)
# - Windows x86_64 (CUDA: GPU + Hybrid) [requires native Windows CUDA build]

VERSION="${1:-1.0.2}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/dist"
mkdir -p "$DIST"

build_cpu_linux() {
  cmake -S "$ROOT" -B "$ROOT/build-linux-cpu" -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=OFF
  cmake --build "$ROOT/build-linux-cpu" -j"$(nproc)"
  local out="$DIST/alphaminer-ubuntu-x86_64-cpu-v$VERSION"
  rm -rf "$out" && mkdir -p "$out"
  cp "$ROOT/build-linux-cpu/AlphaMiner" "$out/AlphaMiner"
  cp "$ROOT/packages/linux/run.sh" "$out/run.sh"
  chmod +x "$out/AlphaMiner" "$out/run.sh"
  tar -czf "$DIST/alphaminer-ubuntu-x86_64-cpu-v$VERSION.tar.gz" -C "$DIST" "$(basename "$out")"
}

build_cuda_linux() {
  if ! command -v nvcc >/dev/null 2>&1; then
    echo "[!] nvcc not found: skipping Ubuntu/HiveOS CUDA artifacts"
    return 0
  fi
  cmake -S "$ROOT" -B "$ROOT/build-linux-cuda" -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON
  cmake --build "$ROOT/build-linux-cuda" -j"$(nproc)"

  local ubuntu="$DIST/alphaminer-ubuntu-x86_64-gpu-hybrid-v$VERSION"
  rm -rf "$ubuntu" && mkdir -p "$ubuntu"
  cp "$ROOT/build-linux-cuda/AlphaMiner" "$ubuntu/AlphaMiner"
  cp "$ROOT/packages/linux/run.sh" "$ubuntu/run.sh"
  chmod +x "$ubuntu/AlphaMiner" "$ubuntu/run.sh"
  tar -czf "$DIST/alphaminer-ubuntu-x86_64-gpu-hybrid-v$VERSION.tar.gz" -C "$DIST" "$(basename "$ubuntu")"

  local hive="$DIST/alphaminer"
  rm -rf "$hive" && mkdir -p "$hive"
  cp "$ROOT/build-linux-cuda/AlphaMiner" "$hive/AlphaMiner"
  cp "$ROOT/packages/hiveos/h-"* "$hive/"
  chmod +x "$hive/AlphaMiner" "$hive/h-run.sh" "$hive/h-stop.sh" "$hive/h-stats.sh"
  tar -czf "$DIST/alphaminer-hiveos-gpu-hybrid-v$VERSION.tar.gz" -C "$DIST" "alphaminer"
}

build_windows_cpu() {
  if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo "[!] mingw not found: skipping Windows CPU artifact"
    return 0
  fi
  cmake -S "$ROOT" -B "$ROOT/build-windows-cpu" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DENABLE_CUDA=OFF \
    -DCMAKE_EXE_LINKER_FLAGS='-static -static-libgcc -static-libstdc++'
  cmake --build "$ROOT/build-windows-cpu" -j"$(nproc)"
  local out="$DIST/alphaminer-windows-x86_64-cpu-v$VERSION"
  rm -rf "$out" && mkdir -p "$out"
  cp "$ROOT/build-windows-cpu/AlphaMiner.exe" "$out/AlphaMiner.exe"
  cp "$ROOT/packages/windows/run.bat" "$out/run.bat"
  cp "$ROOT/README-Windows.md" "$out/README.md"
  (cd "$DIST" && zip -qr "alphaminer-windows-x86_64-cpu-v$VERSION.zip" "$(basename "$out")")
}

print_windows_cuda_note() {
  cat <<EOF
[!] Windows GPU/Hybrid artifact must be built on Windows with CUDA Toolkit + VS2022.
    Suggested artifact name:
    - alphaminer-windows-x86_64-gpu-hybrid-v$VERSION.zip
EOF
}

build_cpu_linux
build_cuda_linux
build_windows_cpu
print_windows_cuda_note

echo
ls -lh "$DIST"/alphaminer-*"v$VERSION"* 2>/dev/null || true
