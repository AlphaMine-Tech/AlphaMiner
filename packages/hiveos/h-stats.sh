#!/usr/bin/env bash
set -euo pipefail

LOG_DIR="${MINER_LOG_DIR:-/var/log/miner}"
VER="1.0"

total_its=0
total_sol=0

shopt -s nullglob
for f in "$LOG_DIR"/alphaminer-gpu*.log; do
  line=$(grep "it/s" "$f" 2>/dev/null | tail -1 || true)
  [[ -z "$line" ]] && continue
  its=$(echo "$line" | grep -oE '[0-9]+ it/s' | tail -1 | awk '{print $1}' || true)
  sol=$(echo "$line" | grep -oE '[0-9]+ solutions' | tail -1 | awk '{print $1}' || true)
  [[ -z "$its" ]] && its=0
  [[ -z "$sol" ]] && sol=0
  total_its=$((total_its + its))
  total_sol=$((total_sol + sol))
done

uptime=0
if [[ -f "$LOG_DIR/alphaminer-gpu0.log" ]]; then
  uptime=$(( $(date +%s) - $(stat -c %Y "$LOG_DIR/alphaminer-gpu0.log" 2>/dev/null || date +%s) ))
  [[ $uptime -lt 0 ]] && uptime=0
fi

echo "{\"hs\":[${total_its}],\"hs_units\":\"its\",\"ar\":[${total_sol},0],\"uptime\":${uptime},\"ver\":\"${VER}\"}"
