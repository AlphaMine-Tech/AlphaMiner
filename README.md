# AlphaMiner — Qubic CPU Miner

Private mining software for [AlphaPool](https://qubic.alphapool.tech). Mines Qubic UPoW (Useful Proof of Work) via the Qatum protocol.

## Requirements
- **CPU:** AVX2 minimum, AVX512 recommended
- **OS:** Linux, Windows

## Build (Linux)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

With AVX512 (recommended for EPYC/Xeon):
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_AVX512=1
make -j$(nproc)
```

## Usage

```
AlphaMiner <Wallet> <Worker> [Threads] [Pool IP] [Pool Port]
```

- **Wallet** — Your 60-character Qubic address
- **Worker** — Worker name (e.g. `rig01`)
- **Threads** — CPU threads (default: all cores)
- **Pool IP** — Defaults to AlphaPool (`131.106.76.202`)
- **Pool Port** — Defaults to `7777`

### Examples

Mine with all cores (auto-detect):
```bash
./AlphaMiner EQVUBBETJJUYCHNWEPZJMHUATJZAIVQHSYGLQFTEJFPOYAIXDDVKBMAGTFPF rig01
```

Mine with 32 threads:
```bash
./AlphaMiner EQVUBBETJJUYCHNWEPZJMHUATJZAIVQHSYGLQFTEJFPOYAIXDDVKBMAGTFPF rig01 32
```

## Based on

Forked from [hackerby888/Qiner](https://github.com/hackerby888/Qiner) (Qubic reference miner).
