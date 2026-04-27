#!/usr/bin/env bash
# AlphaMiner v0.3.0 — Linux launcher
# Usage: ./run.sh <Qubic_Wallet> <WorkerName> [Threads] [PoolHost] [PoolPort]

WALLET="${1:?Usage: ./run.sh <wallet> <worker> [threads] [pool_host] [pool_port]}"
WORKER="${2:?Usage: ./run.sh <wallet> <worker> [threads] [pool_host] [pool_port]}"
THREADS="${3:-$(nproc)}"
POOL_HOST="${4:-qubic.alphapool.tech}"
POOL_PORT="${5:-7777}"

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$DIR/AlphaMiner" "$WALLET" "$WORKER" "$THREADS" "$POOL_HOST" "$POOL_PORT"
