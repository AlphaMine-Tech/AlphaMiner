#!/usr/bin/env bash
set -euo pipefail
# Usage: ./run.sh <wallet> <worker> [mode] [cpu_threads] [gpu_threads] [pool_host] [pool_port]

WALLET="${1:?Usage: ./run.sh <wallet> <worker> [mode] [cpu_threads] [gpu_threads] [pool_host] [pool_port]}"
WORKER="${2:?Usage: ./run.sh <wallet> <worker> [mode] [cpu_threads] [gpu_threads] [pool_host] [pool_port]}"
MODE="${3:-hybrid}"              # cpu | gpu | hybrid
CPU_THREADS="${4:-0}"
GPU_THREADS="${5:-4096}"
POOL_HOST="${6:-qubic.alphapool.tech}"
POOL_PORT="${7:-7777}"

case "$MODE" in
  cpu) GPU_THREADS=0 ;;
  gpu) CPU_THREADS=0 ;;
  hybrid) ;;
  *) echo "Invalid mode: $MODE (cpu|gpu|hybrid)"; exit 1 ;;
esac

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/AlphaMiner" "$WALLET" "$WORKER" -t "$CPU_THREADS" -g "$GPU_THREADS" -p "$POOL_HOST:$POOL_PORT"
