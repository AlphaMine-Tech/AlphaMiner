#!/usr/bin/env bash
# HiveOS stats script for AlphaMiner
# Reads from log and outputs khs/stats in HiveOS JSON format

LOG_FILE="${MINER_LOG_BASENAME:-alphaminer}.log"
LOG_PATH="$MINER_LOG_DIR/$LOG_FILE"

# Parse last hashrate line: "[AlphaMiner]  ... | 1234 it/s | N solutions | ..."
LAST_LINE=$(grep "it/s" "$LOG_PATH" 2>/dev/null | tail -1)

if [[ -z $LAST_LINE ]]; then
    echo '{"hs":[],"hs_units":"its","ar":[0,0],"uptime":0}'
    exit 0
fi

ITS=$(echo "$LAST_LINE" | grep -oP '\d+ it/s' | grep -oP '^\d+')
SOLUTIONS=$(echo "$LAST_LINE" | grep -oP '\d+ solutions' | grep -oP '^\d+')
[[ -z $ITS ]] && ITS=0
[[ -z $SOLUTIONS ]] && SOLUTIONS=0

# Uptime in seconds
UPTIME=$(( $(date +%s) - $(stat -c %Y "$LOG_PATH" 2>/dev/null || echo $(date +%s)) ))
[[ $UPTIME -lt 0 ]] && UPTIME=0

cat <<JSON
{"hs":[$ITS],"hs_units":"its","ar":[$SOLUTIONS,0],"uptime":$UPTIME,"ver":"0.3.0"}
JSON
