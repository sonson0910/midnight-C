# Midnight C++ SDK Specification

This document is the cross-language SDK contract derived from the current C++
implementation. Treat the C++ SDK plus this spec as the source of truth for
other language bindings.

Do not copy wallet seeds, mnemonics, proof keys, or `.secrets` contents into
this document. Public addresses, public transaction hashes, public block
numbers, and public contract addresses may be used as fixtures.

## Version Policy

The SDK pins behavior to `midnight-versions.json`.

Current local production baseline:

```json
{
  "ledger": "8.1.0-rc.1",
  "zswap": "8.1.0-rc.1",
  "onchain_runtime": "3.1.0",
  "indexer_schema": "v4",
  "ffi_abi": 2
}
```

The local `midnight-research/midnight-node` tree is the compatibility source of
truth when public docs lag a release candidate. All SDKs must expose a way to
report their pinned Midnight component versions and must fail clearly when a
proof server or ledger backend is incompatible.

## Network Endpoints

Named networks use this endpoint shape by default:

```text
node RPC:     https://rpc.<network>.midnight.network
source WSS:   wss://rpc.<network>.midnight.network
indexer GQL:  https://indexer.<network>.midnight.network/api/v4/graphql
proof server: http://127.0.0.1:6300
```

Indexer clients must post to exactly `/api/v4/graphql`. Never generate
`/api/v4/graphql/` or `/api/v4/v4/graphql`.

## Public SDK Responsibilities

Every language SDK should keep the same boundary:

- SDK language layer: config, validation, indexer GraphQL, node JSON-RPC,
  WebSocket submit fallback, proof-server health checks, artifact persistence,
  confirmation/finality polling, and typed errors.
- Native ledger backend: canonical wallet derivation, transaction build/prove,
  serialization, transaction hash, transaction inspection, and contract address
  extraction.
- External services: proof server, node RPC, indexer, and optional managed
  state/cache service.

Production transaction bytes must come from the official ledger backend. Do not
hand-roll production Midnight transaction serialization in application SDK code.

## Format Rules

Addresses:

- Unshielded NIGHT wallet: `mn_addr_<network>...`
- DUST wallet: `mn_dust_<network>...`
- Shielded wallet: `mn_shield-addr_<network>...`
- Contract address: raw 32-byte hex, accepted with or without `0x`

Hashes and tokens:

- Transaction hash: 32-byte hex, accepted with or without `0x`
- Block hash: 32-byte hex, accepted with or without `0x`
- Token type: 32-byte hex
- NIGHT token type: 32 zero bytes

Amounts:

- API-boundary amount and fee values are decimal u128 strings.
- Values may include leading zeroes.
- Negative signs, plus signs, decimals, exponent notation, empty strings, and
  values greater than `2^128 - 1` are invalid.
- Convenience `uint64_t` APIs may exist, but they must check overflow and fail
  clearly for larger u128 values.

## Indexer GraphQL v4

Required query forms:

```graphql
transactions(offset: { hash: $hash })
block(offset: { height: $height })
block(offset: { hash: $hash })
```

Wallet outputs:

```graphql
unshieldedCreatedOutputs
unshieldedSpentOutputs
```

`UnshieldedUtxo.value` is a string at the API boundary. SDKs must keep raw
token hex internally; converting all NIGHT token values to the display string
`"NIGHT"` is not allowed on any path that can later build transactions.

## Node JSON-RPC

Node clients are for chain and submission operations only:

- `chain_getHeader`
- `chain_getBlock`
- `chain_getBlockHash`
- `chain_getFinalizedHead`
- `author_submitExtrinsic`
- `midnight_contractState`

Wallet balances must not use `midnight_contractState`.

`midnight_contractState` takes positional parameters:

```json
["<contract_hex_without_0x>"]
```

or:

```json
["<contract_hex_without_0x>", "<block_hash>"]
```

Do not pass wallet Bech32 addresses or object params such as
`{"contract_address": "..."}`.

Submission behavior:

- Submit `RuntimeCall::Midnight(send_mn_transaction)` through
  `author_submitExtrinsic`.
- Try HTTP JSON-RPC first.
- If the HTTP edge rejects a large body with transport-level `403`/`413` style
  failure, retry the same JSON-RPC method over WebSocket.
- Do not retry over WebSocket for application-level JSON-RPC errors such as
  `Invalid Transaction`.

## Ledger FFI Contract

Required C ABI:

```c
int midnight_ledger_backend_info(char **response_json);
int midnight_ledger_build_transaction(const char *request_json, char **response_json);
int midnight_ledger_derive_wallet_addresses(const char *request_json, char **response_json);
int midnight_ledger_validate(const char *request_json, char **response_json);
int midnight_ledger_inspect_transaction(const char *request_json, char **response_json);
void midnight_ledger_free_string(char *value);
```

`midnight_ledger_build_transaction` receives:

```json
{
  "operation": "transfer_night",
  "params": {}
}
```

Required operations:

- `transfer_night`
- `register_dust`
- `deregister_dust`
- `deploy_simple_contract`
- `call_simple_contract`
- `custom_contract_transaction`
- `sync_ledger_state`
- `wallet_summary`

The response must carry typed fields equivalent to C++
`midnight::ledger::BuildResult`: `success`, `transaction_hex`,
`transaction_hash`, `contract_address`, artifact paths, `raw_output`,
`command`, `log`, and `error`.

## Transaction Pipeline

The high-level SDK pipeline is:

```text
prepare state -> prove/build -> persist build artifacts -> submit -> wait inclusion -> wait finality -> persist result artifacts
```

Required public operations:

- NIGHT transfer
- DUST register
- DUST deregister
- simple contract deploy/call smoke helpers
- custom Compact contract transaction submission from generated intents

Production custom contracts should compile through the ledger-compatible Compact
toolchain and submit generated intents with `custom_contract_transaction`.
Simple contract helpers are smoke-test helpers, not the recommended product
contract authoring path.

## State And Cache

SDKs should expose state/cache as a first-class boundary:

- local cache mode
- snapshot import/export mode
- optional managed state service mode
- state readiness
- lag in blocks versus node tip when available
- refresh tail operation
- conservative repair/reset actions

Transaction build APIs must not silently start an hour-long cold sync. If a
cache is missing or stale, return a typed state error or require an explicit
refresh/sync call.

For local Preview operation, a developer-friendly prepare command should:

1. check proof-server health and version,
2. inspect local-cache readiness,
3. refresh the local tail when cache lag exceeds the configured threshold,
4. rebuild only derived wallet ledger-state cache when it is inconsistent,
5. print final readiness and wallet balance.

## Error Model

Public SDKs must use a typed error model equivalent to C++
`midnight::production::ErrorCode`:

- `none`
- `invalid_address`
- `invalid_contract_address`
- `invalid_token_type`
- `invalid_amount`
- `invalid_hash`
- `indexer_schema_error`
- `ledger_backend_error`
- `ledger_build_error`
- `ledger_state_mismatch`
- `proof_server_error`
- `node_rpc_error`
- `node_rejected_tx`
- `indexer_error`
- `submission_rejected`
- `confirmation_timeout`
- `finality_timeout`
- `artifact_error`
- `unsupported_operation`

Known Midnight node custom errors used by current troubleshooting fixtures:

- `170`: `InvalidDustSpendProof`
- `171`: `OutOfDustValidityWindow`

Do not expose only unstructured strings from public APIs.

## Artifacts

Artifact layout:

```text
midnight-artifacts/<network>/<wallet_id>/<operation>/<id-or-timestamp>/
```

Important files may include:

- `metadata.json`
- `transaction.hex`
- `transaction.hash`
- `ledger-output.json`
- `submit-result.json`
- `confirmation.json`
- `intent.json`
- `private-state.json`
- `zswap-state.json`
- `onchain-state.json`
- `result.json`

Cleanup must be conservative. SDKs must not delete transaction-critical
artifacts by default.

## Golden Fixtures

Shared test vectors live under `golden-fixtures/`:

- `vectors/u128.json`
- `vectors/formats.json`
- `vectors/errors.json`
- `vectors/artifact-layout.json`
- `vectors/indexer-preview.json`
- `live/preview-smoke.json`

Every language SDK should run the same vectors for:

- u128 decimal validation,
- address/hash/token validation,
- proof-server version compatibility fields,
- artifact path layout,
- public error code strings,
- live-gated Preview smoke evidence.

Live Preview tests are gated and must not run by default in CI.

## Live Evidence Required

A release candidate can claim end-to-end production readiness only when there is
current evidence for:

- SDK build,
- native ledger backend build,
- non-spending tests,
- proof-server version check,
- indexer read-only block/tx/UTXO queries,
- local/snapshot/managed state readiness,
- gated live NIGHT transfer inclusion/finality,
- gated live DUST register/deregister where applicable,
- gated live contract deploy/call inclusion/finality,
- artifact persistence and context verification.

If any item is missing, report the exact remaining gap.
