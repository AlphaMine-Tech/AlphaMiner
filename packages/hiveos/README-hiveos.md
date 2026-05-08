# AlphaMiner GPU — HiveOS Custom Miner

## Flight Sheet (Custom miner)

- **Miner name:** AlphaMiner-GPU
- **Installation URL:** `https://github.com/AlphaMine-Tech/AlphaMiner/releases/download/v0.4.0/alphaminer-hiveos-gpu-v0.4.0.tar.gz`
- **Hash algorithm:** `qubic`
- **Wallet and worker template:** `%WAL%`
- **Pool URL:** `qubic.alphapool.tech:7777`
- **Pass:** `%WORKER_NAME%`
- **Extra config arguments:** optional (example: `-g 4096 -t 0`)

## Behavior

- Auto-detects all NVIDIA GPUs.
- Launches one AlphaMiner process per GPU (`worker-g0`, `worker-g1`, ...).
- Aggregates `it/s` into Hive stats.
