# AlphaMiner Release Strategy

AlphaMiner is proprietary. The safe distribution model is:

1. **Keep the source repository private.**
2. **Build release artifacts from the private repo on trusted infrastructure.**
3. **Publish only binaries + manifests** to a separate public distribution location.

## Recommended Layout

### Private source repo
Use this repository for:
- CUDA / CPU source
- internal build scripts
- private issue tracking
- performance tuning work
- HiveOS packaging templates

### Public binary repo or release mirror
Use a separate public location for:
- release archives (`.tar.gz`, `.zip`)
- `SHA256SUMS`
- changelog / release notes
- HiveOS install URL targets
- miner manifests only

This avoids exposing the proprietary codebase while still giving miners stable download URLs.

## Why not make the source repo public?
Because the current repository contains the proprietary implementation itself. A public GitHub repository would expose the CUDA kernel, tuning, and implementation details.

## Practical Options

### Option A — Private source repo + separate public GitHub releases repo
Best default.

- Keep `AlphaMine-Tech/AlphaMiner` private
- Create a second public repo such as `AlphaMine-Tech/alphaminer-releases`
- Upload only built artifacts + manifests there
- Point HiveOS custom miner URLs at that public repo's release assets

### Option B — Private source repo + object storage / CDN
Good if you want stable direct-download URLs without another GitHub repo.

- Build from private repo
- Upload artifacts to S3 / Cloudflare R2 / Bunny / similar
- Publish checksums + changelog in a small public page/repo

### Option C — Private source repo + GitHub Packages / container registry
Useful for internal distribution, but less convenient for HiveOS custom miner installs.

## HiveOS Recommendation
For HiveOS custom miner distribution, the easiest model is:
- build `alphaminer-hiveos-vX.Y.Z.tar.gz` from the private repo
- publish that archive publicly
- keep `h-manifest.conf`, `h-run.sh`, and `h-stats.sh` aligned with the shipped binary interface
- never require HiveOS to pull source code

## Release Flow

1. Update version in the private repo
2. Build artifacts with `packages/build-release.sh`
3. Generate checksums / release manifest
4. Copy artifacts into the public binary mirror
5. Publish release notes
6. Update HiveOS install URL if versioned URLs are used

## Guardrails

- Do **not** publish source tarballs from the proprietary repo
- Do **not** enable GitHub auto-generated source archives if the repo becomes publicly reachable
- Treat performance numbers in README/release notes as claims that should be backed by reproducible internal test notes
- Keep credentials out of `git remote -v` output / docs / screenshots
