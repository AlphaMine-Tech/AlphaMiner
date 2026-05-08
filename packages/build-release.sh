#!/usr/bin/env bash
# Build AlphaMiner release artifacts
# Run from repo root on Linux x86_64.

set -euo pipefail
VERSION="0.4.0"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$REPO_ROOT/dist"
mkdir -p "$DIST"

echo "=== Building AlphaMiner v$VERSION ==="

# CPU linux artifact (legacy)
cmake -B "$REPO_ROOT/build-linux" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -static-libgcc -static-libstdc++" \
  "$REPO_ROOT"
cmake --build "$REPO_ROOT/build-linux" -j"$(nproc)"
CPU_BIN="$REPO_ROOT/build-linux/AlphaMiner"
strip "$CPU_BIN" || true

CPU_DIR="$DIST/alphaminer-linux-x86_64-v$VERSION"
rm -rf "$CPU_DIR"
mkdir -p "$CPU_DIR"
cp "$CPU_BIN" "$CPU_DIR/AlphaMiner"
cp "$REPO_ROOT/packages/linux/run.sh" "$CPU_DIR/"
chmod +x "$CPU_DIR/AlphaMiner" "$CPU_DIR/run.sh"
tar -czf "$DIST/alphaminer-linux-x86_64-v$VERSION.tar.gz" -C "$DIST" "alphaminer-linux-x86_64-v$VERSION"

# GPU HiveOS artifact (requires prebuilt GPU binary)
GPU_BIN="$REPO_ROOT/build-gpu/AlphaMiner"
if [[ ! -x "$GPU_BIN" ]]; then
  echo "[!] Missing $GPU_BIN"
  echo "    Build GPU binary first (e.g. CUDA toolchain) then rerun."
  exit 1
fi

HIVE_DIR="$DIST/alphaminer"
rm -rf "$HIVE_DIR"
mkdir -p "$HIVE_DIR"
cp "$GPU_BIN" "$HIVE_DIR/AlphaMiner"
cp "$REPO_ROOT/packages/hiveos/h-manifest.conf" "$HIVE_DIR/"
cp "$REPO_ROOT/packages/hiveos/h-run.sh" "$HIVE_DIR/"
cp "$REPO_ROOT/packages/hiveos/h-stop.sh" "$HIVE_DIR/"
cp "$REPO_ROOT/packages/hiveos/h-stats.sh" "$HIVE_DIR/"
chmod +x "$HIVE_DIR/AlphaMiner" "$HIVE_DIR/h-run.sh" "$HIVE_DIR/h-stop.sh" "$HIVE_DIR/h-stats.sh"
tar -czf "$DIST/alphaminer-hiveos-gpu-v$VERSION.tar.gz" -C "$DIST" "alphaminer"

# Windows (optional)
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  cmake -B "$REPO_ROOT/build-windows" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    "$REPO_ROOT"
  cmake --build "$REPO_ROOT/build-windows" -j"$(nproc)"
  WIN_DIR="$DIST/alphaminer-windows-x86_64-v$VERSION"
  rm -rf "$WIN_DIR"
  mkdir -p "$WIN_DIR"
  cp "$REPO_ROOT/build-windows/AlphaMiner.exe" "$WIN_DIR/"
  cp "$REPO_ROOT/packages/windows/run.bat" "$WIN_DIR/"
  (cd "$DIST" && zip -qr "alphaminer-windows-x86_64-v$VERSION.zip" "alphaminer-windows-x86_64-v$VERSION")
else
  echo "[!] mingw not found — skipping Windows build"
fi

echo "=== Release artifacts ==="
ls -lh "$DIST"/alphaminer-*"$VERSION"* 2>/dev/null || true
