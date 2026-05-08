# AlphaMiner GPU Test Quickstart (recover/gpu-5958b12)

This branch restores the known working CUDA runtime path from commit `5958b12`.

## 1) Build on a CUDA-capable Linux host

```bash
git checkout recover/gpu-5958b12
./scripts/build-gpu-linux.sh
```

## 2) Run first-flight GPU test

```bash
./scripts/run-gpu-test.sh <WALLET> <WORKER> 128 0 qubic.alphapool.tech 7777
```

- `128` = GPU threads per batch (start here)
- `0` = CPU threads disabled (GPU-only test)

## 3) What to watch in first 15 minutes

- `Connected to pool`
- `Subscribed to pool`
- periodic hashrate / iteration logs
- no crash/restart loop
- share submit activity and acceptance in pool logs

## 4) Optional hybrid mode

```bash
./scripts/run-gpu-test.sh <WALLET> <WORKER> 128 4 qubic.alphapool.tech 7777
```

## 5) Package source for transfer

```bash
git archive --format=tar.gz -o dist/alphaminer-recover-gpu-5958b12.tar.gz recover/gpu-5958b12
```
