#!/usr/bin/env bash
# Build AlphaMiner release tarballs for all platforms
# Run from the AlphaMiner repo root on a Linux x86_64 host
# Requires: cmake, g++ (with AVX2 support), upx (optional, for size reduction)
# For Windows cross-compile: x86_64-w64-mingw32-g++ must be installed

set -e
VERSION="0.3.0"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$REPO_ROOT/dist"

mkdir -p "$DIST"

echo "=== Building AlphaMiner v$VERSION ==="

# --- Linux x86_64 ---
echo "[1/2] Building Linux x86_64..."
cmake -B "$REPO_ROOT/build-linux" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -static-libgcc -static-libstdc++" \
    "$REPO_ROOT"
cmake --build "$REPO_ROOT/build-linux" -j$(nproc)

LINUX_BINARY="$REPO_ROOT/build-linux/AlphaMiner"
strip "$LINUX_BINARY"
# Optional: UPX compress (reduces binary ~60%)
if command -v upx &>/dev/null; then
    upx --best "$LINUX_BINARY"
fi

# Linux standard tarball
LINUX_DIR="$DIST/alphaminer-linux-x86_64-v$VERSION"
mkdir -p "$LINUX_DIR"
cp "$LINUX_BINARY" "$LINUX_DIR/AlphaMiner"
cp "$REPO_ROOT/packages/linux/run.sh" "$LINUX_DIR/"
chmod +x "$LINUX_DIR/run.sh" "$LINUX_DIR/AlphaMiner"
tar -czf "$DIST/alphaminer-linux-x86_64-v$VERSION.tar.gz" -C "$DIST" "alphaminer-linux-x86_64-v$VERSION"
echo "  -> $DIST/alphaminer-linux-x86_64-v$VERSION.tar.gz"

# HiveOS tarball (different directory name required by HiveOS)
HIVE_DIR="$DIST/alphaminer"
mkdir -p "$HIVE_DIR"
cp "$LINUX_BINARY" "$HIVE_DIR/AlphaMiner"
cp "$REPO_ROOT/packages/hiveos/h-manifest.conf" "$HIVE_DIR/"
cp "$REPO_ROOT/packages/hiveos/h-run.sh" "$HIVE_DIR/"
cp "$REPO_ROOT/packages/hiveos/h-stats.sh" "$HIVE_DIR/"
chmod +x "$HIVE_DIR/AlphaMiner" "$HIVE_DIR/h-run.sh" "$HIVE_DIR/h-stats.sh"
tar -czf "$DIST/alphaminer-hiveos-v$VERSION.tar.gz" -C "$DIST" "alphaminer"
echo "  -> $DIST/alphaminer-hiveos-v$VERSION.tar.gz"

# --- Windows x86_64 (cross-compile) ---
echo "[2/2] Building Windows x86_64..."
if ! command -v x86_64-w64-mingw32-g++ &>/dev/null; then
    echo "  [!] mingw64 not found — skipping Windows build"
    echo "      Install: sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64"
else
    cmake -B "$REPO_ROOT/build-windows" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
        -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
        "$REPO_ROOT"
    cmake --build "$REPO_ROOT/build-windows" -j$(nproc)

    WIN_DIR="$DIST/alphaminer-windows-x86_64-v$VERSION"
    mkdir -p "$WIN_DIR"
    cp "$REPO_ROOT/build-windows/AlphaMiner.exe" "$WIN_DIR/"
    cp "$REPO_ROOT/packages/windows/run.bat" "$WIN_DIR/"
    cd "$DIST" && zip -r "alphaminer-windows-x86_64-v$VERSION.zip" "alphaminer-windows-x86_64-v$VERSION"
    cd "$REPO_ROOT"
    echo "  -> $DIST/alphaminer-windows-x86_64-v$VERSION.zip"
fi

echo ""
echo "=== Release artifacts ==="
ls -lh "$DIST"/*.tar.gz "$DIST"/*.zip 2>/dev/null
