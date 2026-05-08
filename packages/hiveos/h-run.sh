#!/usr/bin/env bash
set -euo pipefail

[[ -z "${MINER_FORK:-}" ]] && MINER_FORK="alphaminer"

WALLET="${CUSTOM_TEMPLATE:-${CUSTOM_USER:-${WAL:-}}}"
WORKER_BASE="${CUSTOM_PASS:-${WORKER_NAME:-$(hostname)}}"
POOL_RAW="${CUSTOM_URL:-qubic.alphapool.tech:7777}"
CPU_THREADS="${CPU_THREADS:-0}"
GPU_THREADS="${GPU_THREADS:-4096}"
EXTRA_ARGS="${CUSTOM_USER_CONFIG:-}"

if [[ -z "$WALLET" ]]; then
  echo "[AlphaMiner] ERROR: Wallet is empty (CUSTOM_TEMPLATE/CUSTOM_USER/WAL)."
  exit 1
fi

POOL_HOST="$POOL_RAW"
POOL_PORT="7777"
if [[ "$POOL_RAW" == *:* ]]; then
  POOL_HOST="${POOL_RAW%:*}"
  POOL_PORT="${POOL_RAW##*:}"
fi
if [[ -n "${CUSTOM_PORT:-}" ]]; then
  POOL_PORT="$CUSTOM_PORT"
fi

BIN="$MINER_DIR/AlphaMiner"
if [[ ! -x "$BIN" ]]; then
  echo "[AlphaMiner] ERROR: Binary not found/executable: $BIN"
  exit 1
fi

GPU_INDEXES=$(nvidia-smi --query-gpu=index --format=csv,noheader 2>/dev/null | tr -d ' ' || true)
if [[ -z "$GPU_INDEXES" ]]; then
  echo "[AlphaMiner] ERROR: no NVIDIA GPUs detected"
  exit 1
fi

mkdir -p "$MINER_LOG_DIR"
PID_FILE="$MINER_LOG_DIR/alphaminer-gpu.pids"
: > "$PID_FILE"

cleanup() {
  if [[ -f "$PID_FILE" ]]; then
    while read -r pid; do
      [[ -n "$pid" ]] && kill "$pid" 2>/dev/null || true
    done < "$PID_FILE"
  fi
}
trap cleanup EXIT INT TERM

echo "[AlphaMiner] Starting multi-GPU"
echo "[AlphaMiner] Pool: $POOL_HOST:$POOL_PORT"
echo "[AlphaMiner] Wallet: ${WALLET:0:12}..."
echo "[AlphaMiner] Worker base: $WORKER_BASE"
echo "[AlphaMiner] Threads: cpu=$CPU_THREADS gpu=$GPU_THREADS"

while read -r idx; do
  [[ -z "$idx" ]] && continue
  worker="${WORKER_BASE}-g${idx}"
  logf="$MINER_LOG_DIR/alphaminer-gpu${idx}.log"
  echo "[AlphaMiner] Launching GPU $idx as worker $worker"
  CUDA_VISIBLE_DEVICES="$idx" "$BIN" "$WALLET" "$worker" -t "$CPU_THREADS" -g "$GPU_THREADS" -p "$POOL_HOST:$POOL_PORT" $EXTRA_ARGS >> "$logf" 2>&1 &
  echo "$!" >> "$PID_FILE"
done <<< "$GPU_INDEXES"

wait -n || true
echo "[AlphaMiner] A GPU process exited; stopping all workers"
cleanup
wait || true
exit 1
