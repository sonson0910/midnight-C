# Midnight Research Patch Inventory

This document records the local changes inside:

```text
midnight-research/midnight-node
```

That tree is not treated as a pristine upstream checkout in this workspace. It
contains local compatibility patches that make the C++ SDK's native
`libmidnight_ledger_ffi` backend build, inspect, prove, serialize, and submit
Midnight transactions with the same ledger/toolkit code path used by the
Midnight Rust sources.

Do not paste or commit wallet seeds, mnemonics, proof keys, or `.secrets`
contents into this file.

## Why This Exists

The C++ SDK is the public API, but production Midnight transaction bytes must be
created by the official Midnight ledger/toolkit implementation. The local Rust
FFI is the boundary:

```text
C++ SDK
  -> native Rust FFI in midnight-research/midnight-node/util/ledger-ffi
  -> Midnight ledger/toolkit crates
  -> serialized transaction bytes and transaction hash
  -> C++ node RPC submit and indexer confirmation
```

The SDK must not hand-roll production transaction serialization in C++, and it
must not shell out to a CLI process for the production transaction path.

## Modified Files

Current nested repository status is expected to include these local changes:

```text
Cargo.lock
Cargo.toml
CXX_SDK_PATCHES.md
util/ledger-ffi/
util/toolkit/src/fetcher.rs
util/toolkit/src/fetcher/fetch_storage/redb_backend.rs
util/toolkit/src/fetcher/wallet_state_cache.rs
util/toolkit/src/serde_def/transactions.rs
util/toolkit/src/tx_generator/builder/mod.rs
util/toolkit/src/tx_generator/builder/builders/common/batch_single_tx.rs
util/toolkit/src/tx_generator/builder/builders/common/claim_rewards.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_call.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_custom.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_deploy.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_maintenance.rs
util/toolkit/src/tx_generator/builder/builders/common/deregister_dust_address.rs
util/toolkit/src/tx_generator/builder/builders/common/register_dust_address.rs
util/toolkit/src/tx_generator/builder/builders/common/single_tx.rs
```

## Patch Purposes

### `util/ledger-ffi/`

Native Rust `cdylib` consumed by the C++ SDK through
`include/midnight/ledger/ledger_ffi.h`.

Important exported C ABI symbols:

```c
int midnight_ledger_backend_info(char **response_json);
int midnight_ledger_build_transaction(const char *request_json, char **response_json);
int midnight_ledger_derive_wallet_addresses(const char *request_json, char **response_json);
int midnight_ledger_validate(const char *request_json, char **response_json);
int midnight_ledger_inspect_transaction(const char *request_json, char **response_json);
void midnight_ledger_free_string(char *value);
```

Supported operation names accepted by `midnight_ledger_build_transaction`:

```text
transfer_night
register_dust
deregister_dust
deploy_simple_contract
call_simple_contract
custom_contract_transaction
sync_ledger_state
wallet_summary
```

The FFI owns canonical wallet derivation, ledger source replay, proof/build
integration, serialized transaction hex, transaction hash output, transaction
inspection, and contract address extraction.

### `Cargo.toml` and `Cargo.lock`

Register the local `midnight-ledger-ffi` package and lock its dependency graph
against the checked-out Midnight ledger/toolkit sources. Do not remove this
package when syncing with upstream unless the C++ build is migrated to an
equivalent upstream FFI.

### `CXX_SDK_PATCHES.md`

Small marker file inside the nested `midnight-node` tree. It points future
sessions back to this root SDK handoff document so the local patches are not
mistaken for an untouched upstream checkout.

### Toolkit Transaction Builders

The common transaction builders were patched to preserve canonical block
context in serialized transactions.

Patched builder files:

```text
util/toolkit/src/tx_generator/builder/builders/common/batch_single_tx.rs
util/toolkit/src/tx_generator/builder/builders/common/claim_rewards.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_call.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_custom.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_deploy.rs
util/toolkit/src/tx_generator/builder/builders/common/contract_maintenance.rs
util/toolkit/src/tx_generator/builder/builders/common/deregister_dust_address.rs
util/toolkit/src/tx_generator/builder/builders/common/register_dust_address.rs
util/toolkit/src/tx_generator/builder/builders/common/single_tx.rs
```

Critical rule:

```text
Do not reintroduce TransactionWithContext::new(tx, None) for production
transaction builders.
```

`None` creates a default ledger block context using wall-clock time and a random
parent hash. That can build and prove a transaction locally but produce a
non-canonical `parentBlockHash`. The C++ SDK detects this before submit as:

```text
error_code=ledger_state_mismatch
```

Production builders must pass the current canonical context, currently through:

```text
Some(context.latest_block_context())
```

### `util/toolkit/src/tx_generator/builder/mod.rs`

Adds/exposes helper plumbing used by the builder patches so every production
transaction builder can access the latest canonical block context from the
ledger source replay.

### Fetcher And Cache Files

Patched files:

```text
util/toolkit/src/fetcher.rs
util/toolkit/src/fetcher/fetch_storage/redb_backend.rs
util/toolkit/src/fetcher/wallet_state_cache.rs
```

Purpose:

- emit source-sync progress and heartbeat messages that the C++ helper can
  surface through `sync-status`
- make public Preview WSS timeouts diagnosable instead of looking like a hang
- checkpoint `highest_verified_block` after contiguous verified fetch ranges,
  not only after the entire cold sync finishes
- support resumable local-cache mode after a long cold sync
- support wallet ledger-state cache inspection and rebuild workflows

This is why a cold sync can resume from the Redb source cache instead of
throwing away a multi-hour, multi-gigabyte fetch.

### `util/toolkit/src/serde_def/transactions.rs`

Adds the serialization/deserialization shape needed by the FFI and C++ SDK to
inspect transaction artifacts, including canonical block context fields.

The C++ SDK uses this inspection before submit to reject transactions whose
`parentBlockHash` is not known by the live node.

## Static Contract Assets

The live smart-contract smoke flow uses existing Midnight static assets from:

```text
midnight-research/midnight-node/static/contracts/simple-merkle-tree
```

Those assets are used as reference inputs for `deploy_simple_contract` and
`call_simple_contract`. They are not the long-term production contract authoring
path. Real product contracts should compile Compact artifacts and submit them
through `custom_contract_transaction`.

## Known Live Verification

Observed on Preview on 2026-05-18:

```text
transfer-night tx:
  30024643c66f6c0745c5cca3b37d02c509543882a9a5d77f0f0f15bc63565ede
transfer-night inclusion block:
  780705

contract smoke address:
  9fc5ed8c13df07e672bbf940539a49fabc9847735dbb589d6b5e0b55b8494839
deploy tx:
  d6ef864d0402e764b1b3f457459d24bcf116cb93d45a441f725c6146330bc5b1
call tx:
  eeb97d3f7a0ebb2d5ef392dfc98354921e2a8cc761f3b2bb9fd8001641c7bfac
```

The live contract smoke flow covered:

```text
build deploy -> submit -> confirm -> extract contract address
query state
refresh local cache
build call -> submit -> confirm
query state again
persist artifacts under midnight-artifacts/preview/preview-local
```

## Verification Commands

From the C++ SDK repository root:

```bash
cargo build \
  --manifest-path midnight-research/midnight-node/Cargo.toml \
  --package midnight-ledger-ffi \
  --release \
  --target-dir build_codex_verify_full/midnight-ledger-ffi

git -C midnight-research/midnight-node diff --check
```

Full C++ verification:

```bash
cmake -S . -B build_codex_verify_full
cmake --build build_codex_verify_full
ctest --test-dir build_codex_verify_full --output-on-failure
git diff --check
```

Live flows require local secrets, funded Preview wallet state, a compatible
proof server, and a canonical local cache. They are intentionally gated and are
not default CI jobs.

## Handoff Rule

Future chats should read this file before changing the Rust FFI or any
`midnight-research/midnight-node/util/toolkit` transaction builder. The highest
risk regression is losing canonical transaction context and going back to
locally valid but node-rejected transaction artifacts.
