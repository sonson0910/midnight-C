# Midnight C++ SDK Context

This document is a handoff note for future Codex/ChatGPT sessions working on
this repository. It explains what this SDK currently uses, how it communicates
with Midnight, which paths are production-critical, and which mistakes should
not be reintroduced.

Do not paste or commit mnemonic phrases, wallet seeds, proof keys, or `.secrets`
contents into this file. Public addresses and public transaction hashes are safe
to reference when they help reproduce read-only queries.

## Current Goal

The repository is a C++ SDK for Midnight with future IoT use in mind. The C++
API is the public API. Canonical Midnight ledger transaction construction is
delegated to a native FFI library built from Midnight's official Rust
ledger/toolkit crates. The production path must not call a CLI process to build
transactions.

The desired production pipeline is:

```text
wallet/source state -> ledger sync/cache -> prepare/prove/build -> submit -> confirm/finality -> persist artifacts
```

The C++ SDK owns:

- service configuration
- validation
- indexer GraphQL queries
- node JSON-RPC submission
- proof-server health/proxy calls
- ledger FFI loading
- artifact persistence
- confirmation/finality polling
- structured SDK errors

The Rust FFI owns:

- canonical wallet address derivation
- canonical transaction construction
- ledger context replay
- proof/build integration through Midnight ledger/toolkit crates
- serialized Midnight transaction bytes and transaction hash output

## Important Repository Paths

Core C++ production API:

- `include/midnight/production/midnight_client.hpp`
- `src/production/midnight_client.cpp`
- `include/midnight/production/errors.hpp`
- `include/midnight/production/validation.hpp`
- `include/midnight/production/artifact_manager.hpp`
- `src/production/artifact_manager.cpp`
- `include/midnight/production/state_backend.hpp`
- `src/production/state_backend.cpp`

Ledger FFI boundary:

- `include/midnight/ledger/ledger_ffi.h`
- `include/midnight/ledger/ledger_backend.hpp`
- `include/midnight/ledger/transaction_types.hpp`
- `src/ledger/ledger_backend.cpp`
- `midnight-research/midnight-node/util/ledger-ffi/src/lib.rs`
- `midnight-research/midnight-node/util/ledger-ffi/Cargo.toml`

Network communication:

- `include/midnight/network/indexer_client.hpp`
- `src/network/indexer_client.cpp`
- `include/midnight/network/midnight_node_rpc.hpp`
- `src/network/midnight_node_rpc.cpp`
- `include/midnight/network/substrate_rpc.hpp`
- `src/network/substrate_rpc.cpp`
- `include/midnight/network/transaction_confirmation.hpp`
- `src/network/transaction_confirmation.cpp`

Examples/tests/live helper:

- `examples/preprod_live_flow.cpp`
- `tools/midnight-preprod-live.sh`
- `tests/ledger_backend_test.cpp`
- `tests/production_live_submit_test.cpp`
- `tests/indexer_client_live_test.cpp`

Existing reference docs:

- `docs/MIDNIGHT_API_REFERENCE.md`
- `docs/midnight_graphql_queries.md`
- `docs/MIDNIGHT_UTXO_PROTOCOL.md`

## Build And Packaging

The main CMake option for the native ledger backend is:

```bash
MIDNIGHT_BUILD_LEDGER_FFI=ON
```

When enabled and Rust/Cargo is available, CMake builds the native backend from:

```text
midnight-research/midnight-node/util/ledger-ffi
```

The expected native library names are:

- macOS: `libmidnight_ledger_ffi.dylib`
- Linux: `libmidnight_ledger_ffi.so`
- Windows: `midnight_ledger_ffi.dll`

The package installs:

- headers
- `midnight-core`
- optional native ledger FFI library
- CMake package config
- pkg-config file

Typical local verify build:

```bash
cmake -S . -B build_codex_verify_full
cmake --build build_codex_verify_full
ctest --test-dir build_codex_verify_full --output-on-failure
git diff --check
git -C midnight-research/midnight-node diff --check
```

If only rebuilding the Rust FFI:

```bash
cargo build \
  --manifest-path midnight-research/midnight-node/Cargo.toml \
  --package midnight-ledger-ffi \
  --release \
  --target-dir build_codex_verify_full/midnight-ledger-ffi
```

## Service Endpoints And Networks

The SDK supports named networks such as `preview`, `preprod`, and `mainnet`.
Preview has been the active live-test network in recent work.

Default service shape:

```text
node RPC:       https://rpc.<network>.midnight.network
source WSS:     wss://rpc.<network>.midnight.network
indexer HTTP:   https://indexer.<network>.midnight.network/api/v4/graphql
proof server:   http://127.0.0.1:6300
```

Indexer URLs must post to exactly:

```text
/api/v4/graphql
```

Do not accidentally generate:

```text
/api/v4/graphql/
/api/v4/v4/graphql
```

## C++ Public API

Use `midnight::production::MidnightClient` as the high-level production entry
point.

Key config:

```cpp
midnight::production::ClientConfig config;
config.network_name = "preview";
config.node_url = "https://rpc.preview.midnight.network";
config.indexer_url = "https://indexer.preview.midnight.network/api/v4/graphql";
config.proof_server_url = "http://127.0.0.1:6300";
config.ledger_ffi_library = "/absolute/path/to/libmidnight_ledger_ffi.dylib";
config.state_backend_mode = midnight::production::StateBackendMode::LocalCache;
```

Environment based config reads:

- `MIDNIGHT_NETWORK`
- `MIDNIGHT_NODE_URL`
- `MIDNIGHT_INDEXER_URL`
- `MIDNIGHT_PROOF_SERVER_URL`
- `MIDNIGHT_LEDGER_FFI_LIBRARY`
- `MIDNIGHT_NODE_TIMEOUT_MS`
- `MIDNIGHT_PROOF_TIMEOUT_MS`
- `MIDNIGHT_STATE_MODE` (`local-cache`, `snapshot`, or `managed`)
- `MIDNIGHT_STATE_SERVICE_URL`
- `MIDNIGHT_STATE_SERVICE_API_KEY`
- `MIDNIGHT_STATE_SERVICE_TIMEOUT_MS`

High-level methods:

- `health_check()`
- `derive_wallet_addresses(seed_or_mnemonic, network)`
- `query_transaction(tx_hash)`
- `query_block(height)`
- `query_block(block_hash)`
- `query_wallet_state(address)`
- `query_wallet_state(address, from_block, to_block)`
- `query_wallet_state_from_transaction(address, tx_hash)`
- `query_cached_wallet_summary(params)`
- `query_contract_state(contract_hex)`
- `sync_ledger_state(params)`
- `state_status(source)`
- `refresh_state(params)`
- `ensure_state_fresh(params)`
- `export_state_snapshot(source, snapshot_dir)`
- `import_state_snapshot(snapshot_dir, destination)`
- `build_transfer_night(params)`
- `build_register_dust(params)`
- `build_deregister_dust(params)`
- `build_simple_contract_deploy(params)`
- `build_simple_contract_call(params)`
- `build_custom_contract_transaction(params)`
- `transfer_night(params)`
- `register_dust(params)`
- `deregister_dust(params)`
- `deploy_simple_contract(params)`
- `call_simple_contract(params)`
- `submit_custom_contract_intents(params)`
- `wait_for_inclusion(tx_hash)`
- `wait_for_finality(tx_hash)`

Pipeline methods with `PipelineOptions` build, submit, confirm, and persist
artifacts in one call.

## State Backend Architecture

The SDK now exposes state/cache as a first-class production boundary instead of
making applications scrape `sync-ledger-state.log`.

Public types:

- `StateBackendMode::LocalCache`
- `StateBackendMode::Snapshot`
- `StateBackendMode::Managed`
- `StateStatus`
- `StateRefreshOptions`
- `StateRefreshResult`
- `SnapshotResult`
- `IStateBackend`
- `LocalStateBackend`
- `ManagedStateBackend`

Local mode:

- checks the configured Redb source fetch cache and wallet ledger-state DB
- calls `ILedgerBackend::sync_ledger_state()` when refresh is requested
- does not silently cold-sync inside transaction build APIs
- is the default for production C++ SDK consumers

Snapshot mode:

- uses the same local cache files
- adds `export_state_snapshot()` and `import_state_snapshot()` so a machine
  that has completed source sync can seed another machine
- is useful for CI, edge deployments, IoT gateways, and customer installs where
  cold sync from public RPC would be too slow or unreliable

Managed mode:

- calls a future/state-service HTTP API instead of doing sync work in-process
- expected service endpoints are:
  - `POST /v1/state/status`
  - `POST /v1/state/refresh`
- auth is `Authorization: Bearer <MIDNIGHT_STATE_SERVICE_API_KEY>` when set
- this is the preferred business/operations direction if Venera runs a cache
  state service for product customers

`MidnightClient::state_status(source)` tries to enrich status with current node
tip. If the backend reports a local checkpoint, C++ computes `lag_blocks` and
marks the state stale when lag exceeds the requested threshold. The current
local file backend can prove cache presence but cannot infer a canonical
checkpoint directly from Redb internals; checkpoint visibility still comes from
the patched toolkit logs or a managed service response.

## Ledger FFI Contract

The C ABI consumed by C++:

```c
int midnight_ledger_backend_info(char **response_json);
int midnight_ledger_build_transaction(const char *request_json, char **response_json);
int midnight_ledger_derive_wallet_addresses(const char *request_json, char **response_json);
int midnight_ledger_validate(const char *request_json, char **response_json);
int midnight_ledger_inspect_transaction(const char *request_json, char **response_json);
void midnight_ledger_free_string(char *value);
```

The active local version/capability set is recorded in
`midnight-versions.json` and is also returned by
`midnight_ledger_backend_info`. The current FFI ABI is `2` and follows the
local `midnight-research/midnight-node` source tree. This can be newer than the
public docs, so do not downgrade versions just because docs lag a release
candidate.

`midnight_ledger_build_transaction` receives:

```json
{
  "operation": "transfer_night",
  "params": {}
}
```

Supported operations:

- `transfer_night`
- `register_dust`
- `deregister_dust`
- `deploy_simple_contract`
- `call_simple_contract`
- `custom_contract_transaction`
- `sync_ledger_state`
- `wallet_summary`

Build response maps to `midnight::ledger::BuildResult`:

- `success`
- `transaction_hex`
- `transaction_hash`
- `contract_address`
- `output_file`
- `intent_path`
- `private_state_path`
- `zswap_state_path`
- `onchain_state_path`
- `result_path`
- `raw_output`
- `command`
- `log`
- `error`

`wallet_summary` is a non-spending ledger-cache read path. It uses the same
canonical source/cache replay as transaction building and returns its JSON
summary in `BuildResult::raw_output`. The C++ public wrapper is
`MidnightClient::query_cached_wallet_summary(params)`.

Current wallet summary fields:

- `spendable_night_balance`
- `registered_night_balance`
- `shielded_night_balance`
- `generated_dust_balance`
- `generated_dust_capacity`
- `utxo_count`
- `dust_utxo_count`
- `shielded_coin_count`
- `latest_source_height`
- `utxos`
- `shielded_coins`
- `dust`
- `dust_error`

For deploy transactions the FFI now attempts to extract the canonical
untagged 32-byte contract address from the serialized ledger transaction using
the Midnight toolkit `contract_address` implementation. If extraction fails,
`contract_address` remains empty and the caller must query the indexer after
inclusion.

Wallet derivation response maps to
`midnight::ledger::WalletAddressDerivationResult`:

- `shielded`
- `unshielded`
- `dust`
- `dust_public`
- `coin_public`
- `coin_public_tagged`
- `verifying_key`
- `user_address`

Validation and transaction inspection responses map to:

- `midnight::ledger::ValidationResult`
- `midnight::ledger::TransactionInspectionResult`

The production C++ entry point exposes these through:

- `MidnightClient::ledger_backend_info()`
- `MidnightClient::validate_ledger_value(kind, value, network)`
- `MidnightClient::inspect_transaction_hex(tx_hex, ledger_version)`
- `unshielded_user_address_untagged`

The FFI implementation uses these Midnight Rust components:

- `midnight-node-toolkit`
- `midnight-node-ledger-helpers`
- toolkit `TxGenerator`
- toolkit fetcher/source cache
- toolkit `cli_parsers`
- ledger fork-aware context cache

This is a native library boundary, not a shell/CLI bridge.

## SourceConfig And Ledger Sync

`midnight::ledger::SourceConfig` controls how ledger source transactions are
loaded:

```cpp
struct SourceConfig {
    SourceMode source_mode = SourceMode::Auto;
    std::string src_url;                 // usually wss://rpc.<network>.midnight.network
    std::vector<std::string> src_files;  // optional source files
    bool fetch_only_cached = false;
    bool require_ledger_state_cache = true;
    bool ignore_block_context = false;
    bool dust_warp = false;
    std::string fetch_cache = "redb:midnight_cache/fetch_cache.db";
    std::string ledger_state_db = "midnight_cache/ledger_cache_db";
    uint32_t fetch_concurrency = 4;
    std::optional<uint32_t> fetch_compute_concurrency;
    uint32_t fetch_retry_count = 3;
    uint64_t fetch_retry_delay_ms = 5000;
    std::optional<uint64_t> from_block;
    std::optional<uint64_t> to_block;
    std::vector<std::string> transaction_hashes;
};
```

Important behavior:

- Full/cached ledger sync is required before building real transactions unless
  the source range is intentionally bounded for a special test.
- Transaction build APIs normalize `SourceMode::Auto` to `LocalCache` for
  unbounded RPC sources. This is intentional: production SDK calls should not
  silently cold-sync. Use `SourceMode::ColdSync` or
  `MIDNIGHT_LIVE_ALLOW_COLD_SYNC=1` only when explicitly doing a full RPC
  source fetch.
- After a successful cold sync, run
  `tools/midnight-preprod-live.sh enable-local-mode`. This persists
  `MIDNIGHT_LIVE_SOURCE_MODE=local-cache`,
  `MIDNIGHT_LIVE_FETCH_ONLY_CACHED=1`, and
  `MIDNIGHT_LIVE_ALLOW_COLD_SYNC=0` so later register/transfer calls use the
  local canonical cache.
- Application code should call `MidnightClient::state_status()` or
  `ensure_state_fresh()` before build/submit flows. This gives a typed answer
  (`ready`, `stale`, `lag_blocks`, cache paths, byte counts) instead of forcing
  product code to parse shell output.
- To move an already-synced state to another machine, use
  `export_state_snapshot()` / `import_state_snapshot()` in C++ or the helper
  script's `export-cache` / `import-cache` commands.
- A single faucet block can prove that balance query works, but it is not a
  complete ledger context for production transaction building.
- `ignore_block_context=1` and bounded faucet-only source are debugging tools,
  not the production path.
- Preview WSS may drop background source fetch tasks. The FFI now logs heartbeat
  progress and retries transient source errors.
- The Redb fetch cache file can keep the same file size while still being
  updated internally. Use `tools/midnight-preprod-live.sh sync-status` and check
  the cache `modified` timestamp before assuming the sync is stuck.
- The bundled toolkit fetcher was patched locally to checkpoint
  `highest_verified_block` after each contiguous verified fetch range, not only
  after the whole cold sync completes. Without that, public RPC timeouts could
  make retries reprocess the same cached work for hours.

If cold sync drops with a message like:

```text
The background task closed connection closed; restart required
worker thread panicked
```

retry with lower concurrency:

```bash
MIDNIGHT_LIVE_FETCH_CONCURRENCY=1 MIDNIGHT_LIVE_FETCH_RETRY_COUNT=10 \
  tools/midnight-preprod-live.sh sync-ledger-state
```

## Wallet Balance And DUST Registration

The production balance display should prefer the local ledger cache after it is
synced:

```bash
MIDNIGHT_LIVE_BALANCE_SOURCE=local-cache \
  tools/midnight-preprod-live.sh balance
```

This calls `MidnightClient::query_cached_wallet_summary()` and reports the
wallet state from the same ledger context used for real transaction building.
It is the correct path after the wallet has spent, registered DUST, deployed a
contract, or otherwise moved beyond its first faucet transaction.

`MIDNIGHT_LIVE_BALANCE_SOURCE=funding-tx` and
`MIDNIGHT_LIVE_BALANCE_SOURCE=funding-block` are debugging paths only. They are
useful for quickly proving the indexer can see a faucet transaction, but they
are not current wallet-balance APIs. After DUST registration or any spend, the
old faucet output is spent, so querying only the faucet tx can correctly show
zero available NIGHT even though the wallet still owns a replacement UTXO.

DUST registration does not hard-lock NIGHT. A DUST registration transaction
spends the old NIGHT UTXO and creates a new NIGHT UTXO for the wallet with
DUST-generation metadata. That registered UTXO is still spendable. In wallet
summary output:

- `spendable_night_balance` is the spendable unshielded NIGHT total.
- `registered_night_balance` is the subset of spendable NIGHT currently
  registered for DUST generation. Do not add it on top of spendable NIGHT.
- `generated_dust_balance` is generated DUST available for fees.
- `generated_dust_capacity` is the generation capacity reported by the ledger
  helper.

This is why Lace-like wallet behavior can transfer NIGHT while DUST generation
is active: the registered NIGHT UTXO is still normal spendable ledger state.

## Transaction Pipeline

Production transaction path:

```text
C++ params
  -> FfiLedgerBackend JSON request
  -> Rust ledger/toolkit TxGenerator
  -> proof server if needed
  -> canonical Midnight tx bytes
  -> C++ MidnightNodeRPC::submit_transaction
  -> unsigned RuntimeCall::Midnight(send_mn_transaction)
  -> author_submitExtrinsic
  -> IndexerClient confirmation
  -> ArtifactManager persistence
```

Do not hand-roll production transaction bytes in C++. Legacy local UTXO/CBOR
code is not the production serializer. Production bytes must come from the
official ledger serializer through FFI.

## Node JSON-RPC Rules

`MidnightNodeRPC` and `SubstrateRPC` are for node JSON-RPC only:

- chain/block/header/body reads
- node health/methods
- `author_submitExtrinsic`
- `midnight_contractState` for contract hex only

Wallet NIGHT/DUST balances must not use `midnight_contractState`.

`MidnightNodeRPC::submit_transaction(tx_hex)` expects Midnight ledger
transaction bytes, not an already SCALE-encoded extrinsic. It wraps bytes in:

```text
RuntimeCall::Midnight(send_mn_transaction)
```

and submits through:

```text
author_submitExtrinsic
```

`submit_extrinsic_hex()` is the separate method for an already encoded
extrinsic.

`midnight_contractState` must be called with positional params:

```json
["<contract_hex_without_0x>"]
```

or:

```json
["<contract_hex_without_0x>", "<block_hash>"]
```

Never pass:

```json
{ "contract_address": "..." }
```

Never pass `mn_addr_*`, `mn_dust_*`, or `mn_shield-addr_*` into
`midnight_contractState`.

## Indexer GraphQL Rules

`IndexerClient` is the canonical path for:

- read-only indexer wallet balance
- UTXO lookup
- transaction lookup
- block lookup
- DUST registration state
- contract actions/state from indexer
- nullifier lookups

For current wallet/build state after local cache sync, prefer
`query_cached_wallet_summary()`. Indexer balance queries are still valid for
read-only network inspection, but they should not replace the ledger cache used
for transaction building.

Important v4 GraphQL query forms:

```graphql
transactions(offset: { hash: $hash })
block(offset: { height: $height })
block(offset: { hash: $hash })
```

Wallet outputs use:

```graphql
unshieldedCreatedOutputs
unshieldedSpentOutputs
```

`UnshieldedUtxo.value` is a string/u128-safe value at the API boundary. The SDK
keeps public convenience balances as strings where possible. Any conversion to
`uint64_t` must check overflow and fail clearly if the u128 does not fit.

Hash handling:

- accept tx/block hashes with or without `0x`
- normalize only for request compatibility
- store returned hashes exactly as the indexer returns them

Token handling:

- all-zero 32-byte token hex means NIGHT
- keep raw token hex available internally
- do not replace values needed for transaction building with display string
  `"NIGHT"`

## Address And Type Validation

Production validation lives in:

```text
include/midnight/production/validation.hpp
```

Rules:

- unshielded NIGHT address: `mn_addr_<network>...`
- DUST address: `mn_dust_<network>...`
- shielded address: `mn_shield-addr_<network>...`
- contract address: 32-byte hex, with or without `0x`
- token type: 32-byte hex
- NIGHT token type: 32 zero bytes
- tx hash: 32-byte hex
- amount/fee: decimal u128 string
- RNG seed: 32-byte hex
- wallet seed: accepted as supported hex seed or BIP39 mnemonic by FFI parser

## Error Model

Public production errors use `midnight::production::ErrorCode` and `SdkError`.

Important codes:

- `InvalidAddress`
- `InvalidContractAddress`
- `InvalidTokenType`
- `InvalidAmount`
- `InvalidHash`
- `IndexerSchemaError`
- `LedgerBackendError`
- `LedgerBuildError`
- `ProofServerError`
- `NodeRpcError`
- `NodeRejectedTx`
- `IndexerError`
- `SubmissionRejected`
- `ConfirmationTimeout`
- `FinalityTimeout`
- `ArtifactError`
- `UnsupportedOperation`

Avoid adding new public APIs that return only ad hoc error strings.

## Artifacts

`ArtifactManager` persists build/submit outputs under:

```text
midnight-artifacts/<network>/<wallet_id>/<operation>/<timestamp>/
```

The artifact root is configurable with:

```text
MIDNIGHT_LIVE_ARTIFACT_DIR
```

Important artifacts may include:

- metadata
- transaction hex
- transaction hash
- raw ledger output
- intent path
- private state path
- zswap state path
- onchain state path
- result path

Cleanup is conservative. Important transaction artifacts are not deleted by
default.

## Live Helper Script

Use:

```bash
tools/midnight-preprod-live.sh <command>
```

Commands:

- `generate-wallet`
- `print-faucet`
- `proof-health`
- `balance`
- `sync-status`
- `watch-checkpoint [seconds|once]`
- `sync-ledger-state`
- `enable-local-mode`
- `local-mode-status`
- `export-cache [file]`
- `import-cache <file>`
- `register-dust`
- `transfer-night`
- `both`

Although the script name contains `preprod`, it supports `MIDNIGHT_NETWORK`,
including `preview`.

Preview setup:

```bash
export MIDNIGHT_NETWORK=preview
tools/midnight-preprod-live.sh generate-wallet
tools/midnight-preprod-live.sh print-faucet
tools/midnight-preprod-live.sh proof-health
tools/midnight-preprod-live.sh balance
tools/midnight-preprod-live.sh sync-ledger-state
tools/midnight-preprod-live.sh enable-local-mode
tools/midnight-preprod-live.sh local-mode-status
tools/midnight-preprod-live.sh register-dust
tools/midnight-preprod-live.sh transfer-night
```

Balance defaults to the local ledger cache. For old faucet/indexer debugging
only, override the source explicitly:

```bash
MIDNIGHT_LIVE_BALANCE_SOURCE=funding-tx tools/midnight-preprod-live.sh balance
MIDNIGHT_LIVE_BALANCE_SOURCE=funding-block tools/midnight-preprod-live.sh balance
MIDNIGHT_LIVE_BALANCE_SOURCE=indexer-scan tools/midnight-preprod-live.sh balance
```

The script writes local secrets under:

```text
.secrets/midnight-<network>/live.env
```

That file must stay ignored and must not be copied into docs or commits.

Useful live env vars:

- `MIDNIGHT_NETWORK`
- `MIDNIGHT_LEDGER_FFI_LIBRARY`
- `MIDNIGHT_LIVE_NODE_URL`
- `MIDNIGHT_LIVE_SOURCE_URL`
- `MIDNIGHT_LIVE_INDEXER_URL`
- `MIDNIGHT_LIVE_PROOF_SERVER_URL`
- `MIDNIGHT_LIVE_SOURCE_SEED`
- `MIDNIGHT_LIVE_NIGHT_ADDRESS`
- `MIDNIGHT_LIVE_DESTINATION_ADDRESS`
- `MIDNIGHT_LIVE_DUST_ADDRESS`
- `MIDNIGHT_LIVE_SHIELDED_ADDRESS`
- `MIDNIGHT_LIVE_AMOUNT`
- `MIDNIGHT_LIVE_WALLET_ID`
- `MIDNIGHT_LIVE_FETCH_CACHE`
- `MIDNIGHT_LIVE_LEDGER_STATE_DB`
- `MIDNIGHT_LIVE_DUST_WARP`
- `MIDNIGHT_LIVE_BALANCE_SOURCE`
- `MIDNIGHT_LIVE_BALANCE_TIMEOUT_MS`
- `MIDNIGHT_LIVE_FETCH_CONCURRENCY`
- `MIDNIGHT_LIVE_FETCH_COMPUTE_CONCURRENCY`
- `MIDNIGHT_LIVE_FETCH_RETRY_COUNT`
- `MIDNIGHT_LIVE_FETCH_RETRY_DELAY_MS`
- `MIDNIGHT_LIVE_FETCH_ONLY_CACHED`
- `MIDNIGHT_LIVE_ALLOW_COLD_SYNC`
- `MIDNIGHT_LIVE_ARTIFACT_DIR`
- `MIDNIGHT_LIVE_CONFIRMATION_TIMEOUT_MS`
- `MIDNIGHT_LIVE_CONFIRMATION_POLL_MS`
- `MIDNIGHT_LIVE_SYNC_TIMEOUT_MS`

## Gated Live Tests

Live submit tests never run by default. Enable them only when a funded wallet,
proof server, network URLs, and ledger FFI library are configured:

```bash
export MIDNIGHT_RUN_LIVE_SUBMIT_TESTS=1
export MIDNIGHT_NETWORK=preview
export MIDNIGHT_LEDGER_FFI_LIBRARY=/absolute/path/to/libmidnight_ledger_ffi.dylib
export MIDNIGHT_LIVE_NODE_URL=https://rpc.preview.midnight.network
export MIDNIGHT_LIVE_SOURCE_URL=wss://rpc.preview.midnight.network
export MIDNIGHT_LIVE_INDEXER_URL=https://indexer.preview.midnight.network/api/v4/graphql
export MIDNIGHT_LIVE_PROOF_SERVER_URL=http://127.0.0.1:6300
export MIDNIGHT_LIVE_SOURCE_SEED=<secret>
export MIDNIGHT_LIVE_DESTINATION_ADDRESS=mn_addr_preview...
export MIDNIGHT_LIVE_AMOUNT=1000000
ctest --test-dir build_codex_verify_full --output-on-failure
```

Non-spending tests should remain the default for CI.

## Current Public Preview Test Context

The following public data has been used for read-only verification on preview:

```text
network: preview
public NIGHT address:
  mn_addr_preview17v9604rd5vw00sryvjxfg688fvv9fryz309j9vgjdd7qrh4wdvcqxqfgmj
funding tx hash:
  0xb203e61666b25e2e015978215f134bae8ac7318f23ba5b5f353031aefde008cf
funding block:
  721281
observed faucet amount:
  1000000000
DUST register tx hash:
  f72afbb2cf04ef81a178397e2e5259f96cca35b465b190fcaa903f82fda22a2d
DUST register extrinsic hash:
  0xcbd8e7ad0b0b246189b23c2da47ecdd4069a8135234bb9c3f32caf0e7699878b
DUST register block height:
  773422
DUST register block hash:
  fdd8e90050414308cce698c3e04efaa58a93cf5417d66e9c08c3d0a57358f540
latest local ledger cache height used by balance:
  773431
latest local-cache balance output:
  night_balance=1000000000
  spendable_night_balance=1000000000
  registered_night_balance=1000000000
  shielded_night_balance=0
  dust_balance=2617332199999999999
  dust_capacity=5000000000000000000
  utxo_count=1
  dust_utxo_count=1
  shielded_coin_count=0
  balance_source=local-ledger-cache
```

Do not add the matching seed or mnemonic here.

## Known Pitfalls

1. Do not query wallet NIGHT balance through node contract state.
   Use `IndexerClient` UTXO paths.

2. Do not pass wallet Bech32 addresses to `midnight_contractState`.
   Contract state requires raw 32-byte contract hex.

3. Do not hand-roll production ledger transaction serialization in C++.
   Use `FfiLedgerBackend` and official ledger/toolkit crates.

4. Do not treat one faucet funding block as a complete build context.
   It can make balance query fast, but production build/prove needs valid ledger
   state replay/cache.

5. Do not use `MIDNIGHT_LIVE_FUNDING_TX_HASH` as the current wallet balance
   source after any spend/register operation. The old faucet UTXO may be spent,
   so that debug path can show zero while the local ledger cache shows the
   replacement spendable UTXO.

6. Do not treat `registeredForDustGeneration=true` as locked NIGHT.
   Registered NIGHT remains spendable; `registered_night_balance` is a subset
   of `spendable_night_balance`.

7. Do not run live submit tests by default.
   They are gated by `MIDNIGHT_RUN_LIVE_SUBMIT_TESTS=1`.

8. Do not commit `.secrets`, `midnight_cache`, or live artifacts unless the
   project explicitly changes policy.

9. `*.ps1` files are exploratory/test probes. The user previously said C++
   changes are the priority and `.ps1` files can be ignored unless explicitly
   requested.

10. Existing `docs/ARCHITECTURE.md` and parts of older docs may contain early
   scaffold/legacy wording. Prefer this context file plus current headers/source
   when deciding production Midnight behavior.

## Quick Commands For Future Sessions

Check proof server:

```bash
/Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh proof-health
```

Check live sync progress:

```bash
/Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh sync-status
```

Check current wallet balance from canonical local cache:

```bash
/usr/bin/env MIDNIGHT_NETWORK=preview \
  /Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh balance
```

Watch checkpoint movement in real time:

```bash
/Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh watch-checkpoint 10
```

`sync-ledger-state` auto-resumes transient preview RPC fetch failures by default.
Use `MIDNIGHT_LIVE_SYNC_MAX_RESTARTS` to control the outer helper restart loop
and `MIDNIGHT_LIVE_FETCH_RETRY_COUNT` to control the Rust FFI inner retry loop.

Start/continue preview ledger sync:

```bash
/usr/bin/env MIDNIGHT_NETWORK=preview \
  /Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh sync-ledger-state
```

Enable local canonical mode after sync succeeds:

```bash
/Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh enable-local-mode
/Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh local-mode-status
```

Retry preview sync with lower concurrency:

```bash
MIDNIGHT_LIVE_FETCH_CONCURRENCY=1 MIDNIGHT_LIVE_FETCH_RETRY_COUNT=10 \
  /Users/sonson/Documents/code/venera/midnight/midnight-C/tools/midnight-preprod-live.sh sync-ledger-state
```

Build and test:

```bash
cmake -S /Users/sonson/Documents/code/venera/midnight/midnight-C \
  -B /Users/sonson/Documents/code/venera/midnight/midnight-C/build_codex_verify_full
cmake --build /Users/sonson/Documents/code/venera/midnight/midnight-C/build_codex_verify_full
ctest --test-dir /Users/sonson/Documents/code/venera/midnight/midnight-C/build_codex_verify_full --output-on-failure
```

## What Counts As Production-Ready Evidence

A future session should not claim the SDK is fully end-to-end production
verified unless it has evidence for all of:

- C++ build passes
- Rust FFI build passes
- unit/integration tests pass
- indexer read-only preview/preprod queries pass
- proof server health passes
- ledger cache sync completes or a valid cache is imported
- gated live build/submit confirms inclusion/finality
- artifacts are written and inspected
- no secrets are committed

If any item is missing, state the exact remaining gap instead of saying
"100% production ready".

## Latest Local Verification

Last checked in this workspace on 2026-05-18:

- Rust FFI release build passed:
  `cargo build --manifest-path midnight-research/midnight-node/Cargo.toml --package midnight-ledger-ffi --release --target-dir build_codex_verify_full/midnight-ledger-ffi`
- C++ live helper target passed:
  `cmake --build build_codex_verify_full --target preprod_live_flow -j 4`
- `bash -n tools/midnight-preprod-live.sh` passed.
- `git diff --check` passed.
- Preview local-cache balance succeeded after DUST registration:
  `night_balance=1000000000`,
  `registered_night_balance=1000000000`,
  `dust_balance=2617332199999999999`,
  `ledger_cache_height=773431`.

The previous 2026-05-17 full pass also had:

- `cmake -S . -B build_codex_verify_full` passed.
- `cmake --build build_codex_verify_full` passed with `-Wall -Wextra -Wpedantic`
  enabled for `midnight-core`.
- `ctest --test-dir build_codex_verify_full --output-on-failure` passed.
- `git -C midnight-research/midnight-node diff --check` passed.

Current proof-server availability was not rechecked during the 2026-05-18 docs
handoff update. Run `tools/midnight-preprod-live.sh proof-health` before any
new live build/submit flow.
