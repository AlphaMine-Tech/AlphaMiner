#!/usr/bin/env bash
# HiveOS launch script for AlphaMiner
# Called by HiveOS with environment variables set from the flight sheet

[[ -z $MINER_FORK ]] && MINER_FORK="alphaminer"

# Resolve wallet and worker from HiveOS environment
WALLET="${CUSTOM_TEMPLATE:-$CUSTOM_USER_CONFIG}"
[[ -z $WALLET ]] && WALLET="$CUSTOM_USER"
WORKER="${CUSTOM_PASS:-$(hostname)}"

# Thread count: use CPU_THREADS if set, else all cores
THREADS="${CPU_THREADS:-$(nproc)}"

# Pool endpoint — default to alphapool.tech
POOL_HOST="${CUSTOM_URL:-qubic.alphapool.tech}"
POOL_PORT="${CUSTOM_PORT:-7777}"

# Build command
MINER_CMD="$MINER_DIR/AlphaMiner $WALLET $WORKER $THREADS $POOL_HOST $POOL_PORT"

echo "Starting AlphaMiner v0.3.0"
echo "  Pool:    $POOL_HOST:$POOL_PORT"
echo "  Wallet:  ${WALLET:0:15}...${WALLET: -7}"
echo "  Worker:  $WORKER"
echo "  Threads: $THREADS"

exec $MINER_CMD
