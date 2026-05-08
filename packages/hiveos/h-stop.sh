#!/usr/bin/env bash
set -euo pipefail
PID_FILE="${MINER_LOG_DIR:-/var/log/miner}/alphaminer-gpu.pids"

if [[ -f "$PID_FILE" ]]; then
  while read -r pid; do
    [[ -n "$pid" ]] && kill "$pid" 2>/dev/null || true
  done < "$PID_FILE"
  rm -f "$PID_FILE"
fi
pkill -f "/AlphaMiner " 2>/dev/null || true
exit 0
