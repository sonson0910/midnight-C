# Machine Handoff

This document lists the files and services needed to build and operate the
Midnight C++ SDK from another machine.

Do not commit wallet seeds, mnemonics, private keys, proof keys, `.secrets`,
`midnight_cache`, or `midnight-artifacts`.

## Required Source

Clone the C++ SDK repository:

```bash
git clone https://github.com/Venera-labs/midnight_C.git
cd midnight_C
```

For full native ledger FFI builds, also provide the patched Midnight reference
tree:

```text
midnight-research/midnight-node
```

The exact local patch inventory is documented in
`docs/MIDNIGHT_RESEARCH_PATCHES.md`.

The other reference trees are useful for source comparison and future language
SDK work, but the C++ production build currently needs `midnight-node` for
`util/ledger-ffi`:

```text
midnight-research/midnight-ledger
midnight-research/midnight-indexer
midnight-research/midnight-zk
```

## Required Build Dependencies

Linux:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  libssl-dev libsodium-dev nlohmann-json3-dev libfmt-dev libjsoncpp-dev
```

macOS:

```bash
brew install cmake ninja openssl libsodium nlohmann-json fmt jsoncpp rustup
```

Windows CI uses MSYS2 MINGW64:

```bash
pacman -S --needed \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-openssl \
  mingw-w64-x86_64-libsodium \
  mingw-w64-x86_64-pkgconf
```

Rust/Cargo is required only when building `libmidnight_ledger_ffi` locally.

## Build Commands

Protocol-only smoke build:

```bash
cmake -S . -B build-quality \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DENABLE_BLOCKCHAIN=OFF \
  -DENABLE_GRPC=OFF \
  -DMIDNIGHT_BUILD_LEDGER_FFI=OFF \
  -DMIDNIGHT_ENABLE_INSTALL=OFF
```

Full SDK build without rebuilding Rust FFI:

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON \
  -DBUILD_EXAMPLES=ON \
  -DENABLE_BLOCKCHAIN=ON \
  -DENABLE_WEBSOCKET=ON \
  -DENABLE_GRPC=OFF \
  -DMIDNIGHT_BUILD_LEDGER_FFI=OFF
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Full SDK build with native Rust ledger FFI:

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON \
  -DBUILD_EXAMPLES=ON \
  -DENABLE_BLOCKCHAIN=ON \
  -DENABLE_WEBSOCKET=ON \
  -DENABLE_GRPC=OFF \
  -DMIDNIGHT_BUILD_LEDGER_FFI=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Build only the Rust FFI:

```bash
cargo build \
  --manifest-path midnight-research/midnight-node/Cargo.toml \
  --package midnight-ledger-ffi \
  --release \
  --target-dir build/midnight-ledger-ffi
```

## Runtime Resources To Move

For simple build/test on another machine, move only the source repositories.

For live Preview transaction work without repeating a cold sync, also copy:

```text
midnight_cache/live_submit_fetch_cache.db
midnight_cache/live_submit_ledger_state_db
.secrets/midnight-preview/live.env
```

The `.secrets` file contains wallet/private operational data and must be moved
out-of-band, never through git.

Optional but useful:

```text
midnight-artifacts/
```

These artifacts preserve transaction intents, private state files, zswap state,
transaction hashes, and contract flow outputs.

The proof server is external. For Preview, run a version compatible with
`midnight-versions.json`, for example:

```bash
docker run -p 6300:6300 midnightnetwork/proof-server:latest \
  -- midnight-proof-server --network preview
```

Then verify:

```bash
tools/midnight-preprod-live.sh proof-health
```

## Research Repositories

When GitHub credentials are available, publish the local research trees as
separate repositories. Suggested names under `Venera-labs`:

```text
midnight-research-node
midnight-research-ledger
midnight-research-indexer
midnight-research-zk
```

The C++ SDK should keep `midnight-research/` ignored locally. Do not re-add it
as a root git submodule unless `.gitmodules` is configured intentionally.

Publish all four repositories after authenticating GitHub CLI:

```bash
gh auth login
GITHUB_OWNER=Venera-labs GITHUB_VISIBILITY=private \
  tools/publish-midnight-research.sh
```

The publish script refuses to push dirty research repositories so uncommitted
FFI/toolkit patches cannot be lost accidentally.

## Live Readiness Checklist

Before live submit on a new machine:

```bash
tools/midnight-preprod-live.sh proof-health
tools/midnight-preprod-live.sh local-mode-status
tools/midnight-preprod-live.sh balance
```

If local cache is stale:

```bash
tools/midnight-preprod-live.sh refresh-local-cache
```

If local ledger state is inconsistent but the source fetch cache is present:

```bash
tools/midnight-preprod-live.sh reset-ledger-state-cache
tools/midnight-preprod-live.sh sync-ledger-state
tools/midnight-preprod-live.sh enable-local-mode
```
