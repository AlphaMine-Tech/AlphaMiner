# AlphaMiner — HiveOS Custom Miner

## Flight Sheet Setup

| Field | Value |
|---|---|
| Coin | QUBIC (or custom) |
| Pool | Custom |
| Pool URL | `qubic.alphapool.tech:7777` |
| Wallet | Your 60-character Qubic address |
| Miner | Custom |
| Installation URL | `https://github.com/AlphaMine-Tech/AlphaMiner/releases/download/v0.3.0/alphaminer-hiveos-v0.3.0.tar.gz` |
| Hash algorithm | `aigarth` |
| Pass/Worker | `%WORKER_NAME%` (HiveOS auto-fills rig name) |

## Custom Miner Archive Structure

The release tarball must unpack to:
```
alphaminer/
  AlphaMiner          ← main binary (linux x86_64, statically linked)
  h-manifest.conf
  h-run.sh
  h-stats.sh
```

## Notes

- AlphaMiner uses CPU cores (not GPU). Set Threads in Extra Config if needed.
- Dev fee: 1.5% — 15 seconds per 1000 seconds of mining directed to AlphaMine Tech.
- Pool: `qubic.alphapool.tech:7777` (Qatum stratum)
- Solutions per epoch shown in HiveOS stats as accepted shares.
