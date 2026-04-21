# Official SDK Address Bridge

This project includes a bridge that generates Midnight addresses using official Midnight SDK artifacts.

## Why

If you want address generation to stay aligned with official runtime behavior, this bridge avoids custom format drift.

## Prerequisites

1. Ensure official SDK dependencies are installed in a `node_modules` directory that contains `@midnight-ntwrk/*` packages.

Bridge scripts resolve SDK dependencies in this order:

- `MIDNIGHT_SDK_NODE_MODULES` (if set)
- `<repo>/node_modules`
- `<repo>/midnight-research/node_modules` (legacy fallback)

Legacy setup command (still supported):

```powershell
cd midnight-research
npm install
```

1. Run commands from repository root (`night_fund`) so the default bridge script path resolves.

## Script-only usage

```powershell
node tools/official-wallet-address.mjs --network preprod
```

With fixed seed:

```powershell
node tools/official-wallet-address.mjs --network preprod --seed 42e3a4db89b5334292d977afdafecf4e29fb73da9ac1618d590dd5ee50fad40c
```

Expected JSON output includes:

- `success`
- `network`
- `seedHex`
- `derivationPath`
- `unshieldedAddress`
- `shieldedAddress`
- `dustAddress`

Dust registration flow (requires funded NIGHT UTXO and a reachable proof server):

```powershell
node tools/official-wallet-address.mjs --network preprod --register-dust --proof-server http://127.0.0.1:6300 --sync-timeout 120
```

Additional dust registration fields:

- `dustRegistrationRequested`
- `dustRegistrationAttempted`
- `dustRegistrationSuccess`
- `dustRegistrationMessage`
- `dustRegistrationTxId`

Transfer flow (real onchain submit):

```powershell
node tools/official-transfer-night.mjs --network preprod --seed <64-hex-seed> --to <mn_addr...> --amount <units> --proof-server http://127.0.0.1:6300 --sync-timeout 120
```

Transfer output fields include:

- `success`
- `network`
- `sourceAddress`
- `destinationAddress`
- `amount`
- `txId` (on success)
- `error` (on failure)

Wallet state snapshot flow (no transfer submit):

```powershell
node tools/official-transfer-night.mjs --network preprod --seed <64-hex-seed> --state-only --sync-timeout 120 --no-auto-register-dust
```

State snapshot output fields include:

- `success`
- `network`
- `sourceAddress`
- `synced`
- `syncError`
- `unshieldedBalance`
- `pendingUnshieldedBalance`
- `dustBalance`
- `availableUtxoCount`
- `pendingUtxoCount`

## C/C++ usage

Header:

- `include/midnight/blockchain/official_sdk_bridge.hpp`

Primary C++ API (full wallet addresses):

```cpp
midnight::blockchain::OfficialAddressResult generate_official_wallet_addresses(
    const std::string& network = "preprod",
    const std::string& seed_hex = "",
    uint32_t account = 0,
    uint32_t address_index = 0,
    bool register_dust = false,
    const std::string& proof_server = "",
    uint32_t sync_timeout_seconds = 120,
    const std::string& bridge_script = "tools/official-wallet-address.mjs");
```

Compatibility C++ API (unshielded only):

```cpp
midnight::blockchain::OfficialAddressResult generate_official_unshielded_address(
    const std::string& network = "preprod",
    const std::string& seed_hex = "",
    uint32_t account = 0,
    uint32_t address_index = 0,
    const std::string& bridge_script = "tools/official-wallet-address.mjs");
```

Transfer C++ API:

```cpp
midnight::blockchain::OfficialTransferResult transfer_official_night(
    const std::string& seed_hex,
    const std::string& to_address,
    const std::string& amount,
    const std::string& network = "preprod",
    const std::string& proof_server = "",
    uint32_t sync_timeout_seconds = 120,
    const std::string& bridge_script = "tools/official-transfer-night.mjs");
```

Wallet state C++ API:

```cpp
midnight::blockchain::OfficialWalletStateResult query_official_wallet_state(
    const std::string& seed_hex,
    const std::string& network = "preprod",
    const std::string& proof_server = "",
    const std::string& node_url = "",
    const std::string& indexer_url = "",
    const std::string& indexer_ws_url = "",
    uint32_t sync_timeout_seconds = 120,
    const std::string& bridge_script = "tools/official-transfer-night.mjs");
```

Indexer sync diagnostics C++ API:

```cpp
midnight::blockchain::OfficialIndexerSyncResult query_official_indexer_sync_status(
    const std::string& seed_hex,
    const std::string& network = "preprod",
    const std::string& proof_server = "",
    const std::string& node_url = "",
    const std::string& indexer_url = "",
    const std::string& indexer_ws_url = "",
    uint32_t sync_timeout_seconds = 120,
    const std::string& bridge_script = "tools/official-transfer-night.mjs");
```

Compact deploy C++ API:

```cpp
midnight::blockchain::OfficialContractDeployResult deploy_official_compact_contract(
    const std::string& contract = "FaucetAMM",
    const std::string& network = "undeployed",
    const std::string& seed_hex = "",
    uint64_t fee_bps = 10,
    const std::string& initial_nonce_hex = "",
    const std::string& proof_server = "",
    const std::string& node_url = "",
    const std::string& indexer_url = "",
    const std::string& indexer_ws_url = "",
    uint32_t sync_timeout_seconds = 120,
    uint32_t dust_wait_seconds = 120,
    uint32_t deploy_timeout_seconds = 300,
    uint32_t dust_utxo_limit = 1,
    const std::string& contract_module = "",
    const std::string& contract_export = "",
    const std::string& private_state_store = "",
    const std::string& private_state_id = "",
    const std::string& constructor_args_json = "",
    const std::string& constructor_args_file = "",
    const std::string& zk_config_path = "",
    const std::string& bridge_script = "tools/official-deploy-contract.mjs");
```

C ABI entry point:

```cpp
int midnight_generate_official_unshielded_address(
    const char* network,
    const char* seed_hex,
    char* out_address,
    size_t out_address_capacity,
    char* out_seed_hex,
    size_t out_seed_hex_capacity,
    char* out_error,
    size_t out_error_capacity);

int midnight_transfer_official_night(
    const char* network,
    const char* seed_hex,
    const char* to_address,
    const char* amount,
    const char* proof_server,
    uint32_t sync_timeout_seconds,
    char* out_txid,
    size_t out_txid_capacity,
    char* out_error,
    size_t out_error_capacity);

int midnight_query_official_indexer_sync_status(
    const char* network,
    const char* seed_hex,
    const char* proof_server,
    const char* node_url,
    const char* indexer_url,
    const char* indexer_ws_url,
    uint32_t sync_timeout_seconds,
    int* out_facade_synced,
    int* out_all_connected,
    char* out_source_address,
    size_t out_source_address_capacity,
    char* out_sync_error,
    size_t out_sync_error_capacity,
    char* out_diagnostics_json,
    size_t out_diagnostics_json_capacity,
    char* out_error,
    size_t out_error_capacity);

int midnight_query_official_wallet_state(
    const char* network,
    const char* seed_hex,
    const char* proof_server,
    const char* node_url,
    const char* indexer_url,
    const char* indexer_ws_url,
    uint32_t sync_timeout_seconds,
    int* out_synced,
    uint64_t* out_available_utxo_count,
    uint64_t* out_registered_utxo_count,
    char* out_source_address,
    size_t out_source_address_capacity,
    char* out_state_json,
    size_t out_state_json_capacity,
    char* out_error,
    size_t out_error_capacity);

int midnight_deploy_official_compact_contract(
    const char* contract,
    const char* network,
    const char* seed_hex,
    uint64_t fee_bps,
    const char* proof_server,
    const char* node_url,
    const char* indexer_url,
    const char* indexer_ws_url,
    uint32_t sync_timeout_seconds,
    uint32_t dust_wait_seconds,
    uint32_t deploy_timeout_seconds,
    char* out_contract_address,
    size_t out_contract_address_capacity,
    char* out_txid,
    size_t out_txid_capacity,
    char* out_deploy_json,
    size_t out_deploy_json_capacity,
    char* out_error,
    size_t out_error_capacity);
```

Return value:

- `0`: success
- `1`: failure (`out_error` is populated)

Optional script overrides for C ABI callers and tests:

- `MIDNIGHT_OFFICIAL_WALLET_ADDRESS_SCRIPT`
- `MIDNIGHT_OFFICIAL_TRANSFER_SCRIPT`
- `MIDNIGHT_OFFICIAL_DEPLOY_SCRIPT`

## wallet-generator integration

`wallet-generator` now has an official mode:

```powershell
.\build_verify\bin\Release\wallet-generator.exe --official-sdk --network preprod
```

With dust registration:

```powershell
.\build_verify\bin\Release\wallet-generator.exe --official-sdk --network preprod --register-dust --proof-server http://127.0.0.1:6300 --sync-timeout 120
```

Supported flags:

- `--official-sdk`
- `--network preprod|preview|mainnet|undeployed`
- `--seed-hex <64-hex>`
- `--register-dust`
- `--proof-server <url>`
- `--sync-timeout <seconds>`
- `--official-transfer-to <mn_addr...>`
- `--official-transfer-amount <units>`

Combined transfer example:

```powershell
.\build_verify\bin\Release\wallet-generator.exe --official-sdk --network preprod --seed-hex <64hex> --official-transfer-to <mn_addr...> --official-transfer-amount 1 --proof-server http://127.0.0.1:6300 --sync-timeout 120
```

## Example binary

Build and run:

```powershell
cmake --build build_verify --config Debug --target official_sdk_bridge_example
.\build_verify\bin\Debug\official_sdk_bridge_example.exe preprod
```

The binary prints a valid unshielded address and the seed used.

Transfer through C++ bridge example mode:

```powershell
.\build_verify\bin\Debug\official_sdk_bridge_example.exe transfer preprod <seed_hex> <to_address> <amount> http://127.0.0.1:6300 120
```

State snapshot through C++ bridge example mode:

```powershell
.\build_verify\bin\Debug\official_sdk_bridge_example.exe state preprod <seed_hex> http://127.0.0.1:6300 120
```

Indexer diagnostics through C++ bridge example mode:

```powershell
.\build_verify\bin\Debug\official_sdk_bridge_example.exe indexer preprod <seed_hex> http://127.0.0.1:6300 120
```

Compact deploy through C++ bridge example mode:

```powershell
.\build_verify\bin\Debug\official_sdk_bridge_example.exe deploy undeployed FaucetAMM <seed_hex> http://127.0.0.1:6300
```
