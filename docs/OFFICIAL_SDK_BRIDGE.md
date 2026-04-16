# Official SDK Address Bridge

This project includes a bridge that generates Midnight addresses using official Midnight SDK artifacts.

## Why

If you want address generation to stay aligned with official runtime behavior, this bridge avoids custom format drift.

## Prerequisites

1. Install official SDK dependencies once:

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
```

Return value:

- `0`: success
- `1`: failure (`out_error` is populated)

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
