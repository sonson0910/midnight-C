# Midnight SDK - IoT & Blockchain C++ Infrastructure Library

**Midnight** is a comprehensive C++20 library built to support IoT infrastructure integrated with the Midnight blockchain.

## Key Features

### 1. **Cross-Platform Protocol Support**

- **MQTT**: Lightweight communication for IoT devices
- **CoAP**: CoAP protocol for resource-constrained devices
- **HTTP/HTTPS**: Standard RESTful communication
- **WebSocket**: Real-time bidirectional connections

### 2. **Midnight Blockchain Support**

- Midnight chain management
- Cryptocurrency wallet management with HD key derivation
- Transaction building and signing
- UTXO and multi-asset management
- Distributed integration and on-chain operations

### 3. **Session & Configuration Management**

- Session management for IoT devices
- Flexible configuration
- Integrated logging

## Project Structure

```text
midnight/
├── CMakeLists.txt           # Main CMake configuration
├── include/midnight/
│   ├── core/               # Core components
│   ├── protocols/          # Protocols (MQTT, CoAP, HTTP, WebSocket)
│   ├── blockchain/         # Midnight blockchain SDK
│   └── session/            # Session management
├── src/                    # Source implementations
├── examples/               # Usage examples (MQTT, Blockchain, HTTP)
├── tests/                  # Tests
├── docs/                   # Documentation
├── README.md               # This document
└── MIDNIGHT_BLOCKCHAIN.md  # Blockchain guide
```

## Requirements

- C++20-compatible compiler (gcc 11+, clang 12+, MSVC 2019+)
- CMake 3.20+
- Optional dependencies:
  - nlohmann_json (JSON support)
  - fmt (output formatting)

## Build Project

```bash
# Create build directory
mkdir build && cd build

# Configure CMake
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_MQTT=ON \
      -DENABLE_COAP=ON \
      -DENABLE_HTTP=ON \
      -DENABLE_WEBSOCKET=ON \
      -DENABLE_BLOCKCHAIN=ON \
      -DBUILD_TESTS=ON \
      -DBUILD_EXAMPLES=ON ..

# Build
cmake --build . --config Release

# Install (optional)
cmake --install .
```

## Run Examples

```bash
# Build examples
cmake --build . --target midnight-examples

# MQTT example
./bin/mqtt_example

# Blockchain example
./bin/blockchain_example

# HTTP example
./bin/http_example

# Official SDK bridge example (address/transfer/state/indexer/deploy)
./bin/official_sdk_bridge_example --help

# Connectivity and compatibility check with Midnight Preprod
# Returns exit code 0 when READY for production
./bin/http_connectivity_test

# Deploy FaucetAMM to undeployed network (via official JS bridge)
# Requirement: dependencies installed in midnight-research and wallet has NIGHT + DUST
MIDNIGHT_DEPLOY_SEED_HEX=<seed_hex_32_bytes> \
    ./bin/compact_contract_deploy_undeployed_example

# Deploy a custom contract (outside FaucetAMM) via bridge
# ContractDeployer constructor_args configuration:
#   use_official_sdk_bridge=true
#   contract=<ContractName>
#   contract_module=<path or package specifier to JS contract export module>
#   contract_export=<export name in module>
#   zk_config_path=<path to managed assets for the contract>
#   private_state_id=<private state id>
#   constructor_args_json=<constructor args JSON, supports typed bigint/bytes>

# Run C++ example for direct custom deploy
./bin/compact_contract_deploy_custom_example \
    --contract FaucetAMM \
    --contract-module @midnight-ntwrk/counter-contract \
    --contract-export FaucetAMM \
    --zk-config-path midnight-research/node_modules/@midnight-ntwrk/counter-contract/dist/managed/FaucetAMM
```

## Official SDK Bridge (Recommended for Real-World Flows)

### Preparation

```bash
# Install dependencies for the official JS bridge
cd midnight-research && npm install

# Return to project root
cd ..

# Required when using wallet alias (seed is encrypted at rest)
export MIDNIGHT_WALLET_STORE_PASSPHRASE="your-strong-passphrase-16+-chars"

# Optional: customize wallet alias storage directory
# export MIDNIGHT_WALLET_STORE_DIR=/path/to/wallet-store
```

### Quick usage with C++ binary

```bash
# 1) Register alias once (seed will be encrypted when stored)
./bin/official_sdk_bridge_example wallet-add mywallet <seed_hex> preprod

# 2) Transfer NIGHT using alias (without passing seed on command line)
./bin/official_sdk_bridge_example transfer-wallet preprod mywallet <to_address> <amount>

# 3) Query wallet snapshot using alias
./bin/official_sdk_bridge_example state-wallet preprod mywallet

# 4) Query indexer diagnostics using alias
./bin/official_sdk_bridge_example indexer-wallet preprod mywallet

# 5) Deploy contract using alias
./bin/official_sdk_bridge_example deploy-wallet undeployed mywallet FaucetAMM
```

### Call JS bridge directly (supports --seed-file)

```bash
# Deploy (recommended: use --seed-file instead of --seed)
node tools/official-deploy-contract.mjs \
  --network undeployed \
  --contract FaucetAMM \
  --seed-file /path/to/seed.txt

# Transfer
node tools/official-transfer-night.mjs \
  --network preprod \
  --seed-file /path/to/seed.txt \
  --to <to_address> \
  --amount <amount>
```

## C++ Examples (Wallet Alias + Transfer/Deploy)

### 1) Register alias + transfer NIGHT

```cpp
#include "midnight/blockchain/official_sdk_bridge.hpp"
#include <iostream>

int main() {
    std::string error;
    const bool saved = midnight::blockchain::register_wallet_seed_hex(
        "mywallet",
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "preprod",
        "",
        &error);

    if (!saved) {
        std::cerr << "Wallet alias registration failed: " << error << std::endl;
        return 1;
    }

    const auto tx = midnight::blockchain::transfer_official_night_with_wallet(
        "mywallet",
        "mn_addr_destination_here",
        "1",
        "preprod");

    if (!tx.success) {
        std::cerr << "NIGHT transfer failed: " << tx.error << std::endl;
        return 1;
    }

    std::cout << "txid=" << tx.txid << std::endl;
    return 0;
}
```

### 2) Deploy contract with wallet alias

```cpp
#include "midnight/blockchain/official_sdk_bridge.hpp"
#include <iostream>

int main() {
    const auto deploy = midnight::blockchain::deploy_official_compact_contract_with_wallet(
        "mywallet",
        "FaucetAMM",
        "undeployed",
        10);

    if (!deploy.success) {
        std::cerr << "Deployment failed: " << deploy.error << std::endl;
        return 1;
    }

    std::cout << "contract=" << deploy.contract_address << " txid=" << deploy.txid << std::endl;
    return 0;
}
```

### 3) Query state + indexer diagnostics with wallet alias

```cpp
#include "midnight/blockchain/official_sdk_bridge.hpp"
#include <iostream>

int main() {
    const auto state = midnight::blockchain::query_official_wallet_state_with_wallet(
        "mywallet",
        "preprod");

    if (!state.success) {
        std::cerr << "State query failed: " << state.error << std::endl;
        return 1;
    }

    std::cout << "address=" << state.source_address
              << " balance=" << state.unshielded_balance << std::endl;

    const auto idx = midnight::blockchain::query_official_indexer_sync_status_with_wallet(
        "mywallet",
        "preprod");

    if (!idx.success) {
        std::cerr << "Indexer diagnostics query failed: " << idx.error << std::endl;
        return 1;
    }

    std::cout << "facade_synced=" << (idx.facade_is_synced ? "true" : "false")
              << " all_connected=" << (idx.all_connected ? "true" : "false") << std::endl;
    return 0;
}
```

Security notes:

- Do not use `--seed` and `--seed-file` together.
- For alias flow, `MIDNIGHT_WALLET_STORE_PASSPHRASE` is required (>= 16 chars).
- Prefer `transfer-wallet` / `deploy-wallet` to avoid exposing seed in process args.

## Run Tests

```bash
# Build tests
cmake --build . --target midnight-tests

# Run tests
ctest --output-on-failure
```

## Quick Start - Blockchain

### 1. Initialize Blockchain

```cpp
#include "midnight/blockchain/midnight_adapter.hpp"

// Configure protocol parameters
midnight::blockchain::ProtocolParams params;
params.min_fee_a = 44;
params.min_fee_b = 155381;

// Create blockchain manager
midnight::blockchain::MidnightBlockchain blockchain;
blockchain.initialize("preprod", params);
blockchain.connect("https://rpc.preprod.midnight.network");
```

### 2. Create Wallet

```cpp
#include "midnight/blockchain/wallet.hpp"

midnight::blockchain::Wallet wallet;
wallet.create_from_mnemonic("mnemonic words here...", "");

std::string address = wallet.get_address();
```

### 3. Build Transaction

```cpp
std::vector<midnight::blockchain::UTXO> utxos;
// Fill UTXO data...

auto result = blockchain.build_transaction(
    utxos,
    {{"recipient_address", 2000000}},
    address  // change address
);
```

### 4. Sign & Submit

```cpp
auto signed = blockchain.sign_transaction(
    result.result,
    private_key_hex
);

auto submitted = blockchain.submit_transaction(signed.result);
if (submitted.success) {
    std::cout << "TX ID: " << submitted.result << std::endl;
}
```

## Development Goals

- ✅ Project structure setup
- ✅ Core components (Config, Logger, SessionManager)
- ✅ Protocol clients (MQTT, CoAP, HTTP, WebSocket)
- ✅ Blockchain components (Transaction, Wallet, MidnightBlockchain)
- ⏳ Complete protocol implementations with third-party libraries
- ⏳ Add encryption and security features
- ⏳ Support multi-sig transactions
- ⏳ On-chain smart contract interactions

## Documentation

- [MIDNIGHT_BLOCKCHAIN.md](MIDNIGHT_BLOCKCHAIN.md) - Blockchain API details
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - System architecture
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) - Getting started guide

## License

Apache License 2.0

## Contact

**Project**: Midnight SDK - Blockchain-integrated IoT infrastructure

**Repository**: [https://github.com/sonson0910/midnight-C](https://github.com/sonson0910/midnight-C)
