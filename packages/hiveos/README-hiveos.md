# AlphaMiner — HiveOS Custom Miner

This package is intended for **binary-only distribution**. HiveOS rigs should download a release archive, not source code.

## Recommended Distribution Model

- Build AlphaMiner from the private source repo
- Publish `alphaminer-hiveos-vX.Y.Z.tar.gz` as a public binary asset
- Point HiveOS at the public release asset URL
- Keep the proprietary source repository private

## Flight Sheet Setup

| Field | Value |
|---|---|
| Coin | QUBIC (or custom) |
| Pool | Custom |
| Pool URL | `qubic.alphapool.tech:7777` |
| Wallet | Your 60-character Qubic address |
| Miner | Custom |
| Installation URL | `https://github.com/AlphaMine-Tech/alphaminer-releases/releases/download/v0.3.0/alphaminer-hiveos-v0.3.0.tar.gz` |
| Hash algorithm | `aigarth` |
| Pass/Worker | `%WORKER_NAME%` |

## Runtime Model

The HiveOS wrapper is designed for the GPU-capable AlphaMiner CLI:

```bash
./AlphaMiner <wallet> <worker> -t <cpu_threads> -g <gpu_nonces> -p qubic.alphapool.tech:7777
```

### Defaults

- CPU threads: `0` (GPU-only by default)
- GPU nonces per batch: `2048`
- Pool: `qubic.alphapool.tech:7777`

## Optional Extra Config Variables

You can pass these through your custom miner config/environment:

- `CPU_THREADS=4` — enable hybrid mode with 4 CPU threads
- `GPU_NONCES=2048` — tune GPU nonce batch size
- `CUSTOM_URL=qubic.alphapool.tech`
- `CUSTOM_PORT=7777`
- `EXTRA_CONFIG="..."` — appended verbatim for future flags

## Custom Miner Archive Structure

The release tarball must unpack to:

```text
alphaminer/
  AlphaMiner
  h-manifest.conf
  h-run.sh
  h-stats.sh
```

## Notes

- Do not publish source code for HiveOS distribution.
- If the CLI flags change, update `h-run.sh` and this document together.
- `h-stats.sh` expects AlphaMiner log lines containing `it/s` and `solutions`.
