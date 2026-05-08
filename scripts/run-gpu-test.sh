#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <WALLET> <WORKER> [GPU_THREADS] [CPU_THREADS] [POOL_HOST] [POOL_PORT]" >&2
  exit 1
fi

WALLET="$1"
WORKER="$2"
GPU_THREADS="${3:-128}"
CPU_THREADS="${4:-0}"
POOL_HOST="${5:-qubic.alphapool.tech}"
POOL_PORT="${6:-7777}"

BIN="${BIN:-./build-gpu/AlphaMiner}"

if [[ ! -x "$BIN" ]]; then
  echo "[!] Binary not found/executable: $BIN" >&2
  echo "    Build first: ./scripts/build-gpu-linux.sh" >&2
  exit 1
fi

echo "[+] Launching GPU test"
echo "    Wallet: ${WALLET:0:12}..."
echo "    Worker: $WORKER"
echo "    Pool:   $POOL_HOST:$POOL_PORT"
echo "    Mode:   CPU threads=$CPU_THREADS, GPU threads=$GPU_THREADS"

exec "$BIN" "$WALLET" "$WORKER" -t "$CPU_THREADS" -g "$GPU_THREADS" -p "$POOL_HOST:$POOL_PORT"
