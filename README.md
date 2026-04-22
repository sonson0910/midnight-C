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

## Quick Start

### Install

```bash
# Clone the repository
git clone https://github.com/Venera-labs/midnight_C.git
cd midnight_C

# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_BLOCKCHAIN=ON ..
cmake --build . --config Release
```

### Create a Wallet

```cpp
#include "midnight/blockchain/wallet.hpp"
#include <iostream>

int main() {
    // Create a wallet from mnemonic
    midnight::blockchain::Wallet wallet;
    wallet.create_from_mnemonic("your mnemonic words here...", "");

    // Get wallet address
    std::string address = wallet.get_address();
    std::cout << "Address: " << address << std::endl;

    // Connect to Midnight network and query balance
    midnight::blockchain::MidnightBlockchain blockchain;
    blockchain.initialize("preprod", {});
    blockchain.connect("https://rpc.preprod.midnight.network");

    return 0;
}
```

### Transfer NIGHT (via wallet alias)

```cpp
#include "midnight/blockchain/official_sdk_bridge.hpp"
#include <iostream>

int main() {
    // Register wallet alias once
    midnight::blockchain::register_wallet_seed_hex(
        "mywallet", "<seed_hex_32_bytes>", "preprod");

    // Transfer NIGHT using alias
    const auto tx = midnight::blockchain::transfer_official_night_with_wallet(
        "mywallet", "<to_address>", "1", "preprod");

    if (tx.success)
        std::cout << "txid=" << tx.txid << std::endl;

    return 0;
}
```

## Module Quick Start (Bài 2–6)

### Bài 2 — Network & RPC

Switch between networks, query the chain, submit and confirm transactions.

```cpp
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/network/network_config.hpp"

// Pick a network: DEVNET | TESTNET | MAINNET
auto endpoint = midnight::network::NetworkConfig::get_rpc_endpoint(
    midnight::network::NetworkConfig::Network::TESTNET);

midnight::network::MidnightNodeRPC rpc(endpoint, /*timeout_ms=*/5000);

if (rpc.is_ready()) {
    auto [height, hash] = rpc.get_chain_tip();
    std::cout << "tip: " << height << " " << hash << "\n";

    // Broadcast a signed tx (CBOR hex)
    std::string tx_id = rpc.submit_transaction(tx_hex);

    // Block until confirmed (max 10 blocks, ~60 s)
    bool ok = rpc.wait_for_confirmation(tx_id, 10);
    std::cout << (ok ? "confirmed" : "timeout") << "\n";
}
```

Auto-retry behaviour is configured via `midnight::network::RetryConfig` (exponential backoff, up to 3 retries by default).

---

### Bài 3 — IoT Protocols (MQTT · CoAP · HTTP · WebSocket)

All four protocol clients live in `midnight::protocols`.

```cpp
// ── MQTT (lightweight IoT messaging) ──────────────────────────
#include "midnight/protocols/mqtt/mqtt_client.hpp"
midnight::protocols::mqtt::MqttClient mqtt;
mqtt.connect("mqtt://broker.example.com", 1883);
mqtt.publish("midnight/sensor/data", R"({"temp":22.5})", /*qos=*/1);
mqtt.subscribe("midnight/cmd/#", [](const std::string& topic,
                                    const std::string& payload) {
    // handle incoming command
});

// ── CoAP (low-power sensors) ──────────────────────────────────
#include "midnight/protocols/coap/coap_client.hpp"
midnight::protocols::coap::CoapClient coap;
std::string body = coap.send_request(
    "coap://sensor.local/state",
    midnight::protocols::coap::RequestMethod::GET);

// ── HTTP (standard REST / indexer) ───────────────────────────
#include "midnight/protocols/http/http_client.hpp"
midnight::protocols::http::HttpClient http;
auto resp = http.get("https://indexer.preprod.midnight.network/api/v3/graphql");

// ── WebSocket (real-time block subscription) ─────────────────
#include "midnight/protocols/websocket/ws_client.hpp"
midnight::protocols::websocket::WebSocketClient ws;
ws.set_message_handler([](const std::string& msg) {
    std::cout << "frame: " << msg << "\n";
});
ws.connect("wss://rpc.preprod.midnight.network");
ws.send(R"({"jsonrpc":"2.0","method":"chain_subscribeNewHead",
            "params":[],"id":1})");
```

> **Note**: The `midnight::protocols` API surface is fully defined; underlying I/O integration (paho-mqtt, libcoap, etc.) is plugged in separately.

---

### Bài 4 — ZK Proofs

Generate proofs via the local Proof Server, isolate private witnesses, and embed the proof in a transaction.

```cpp
#include "midnight/zk/proof_server_client.hpp"
#include "midnight/zk/proof_server_resilient_client.hpp"
#include "midnight/zk/proof_types.hpp"

// --- Base client (proof server at localhost:6300 by default) ---
midnight::zk::ProofServerClient::Config cfg;   // host=localhost, port=6300
auto base = std::make_shared<midnight::zk::ProofServerClient>(cfg);
base->connect();

// --- Resilient wrapper: circuit breaker + exponential backoff ---
midnight::zk::ResilienceConfig res;
res.max_retries        = 3;
res.failure_threshold  = 5;   // open circuit after 5 failures
midnight::zk::ProofServerResilientClient client(base, res);

// --- Public inputs (appear on-chain) ---
midnight::zk::PublicInputs inputs;
inputs.add_input("recipient", "0xaabbccdd...");

// --- Witness output (private — never leaves the prover) ---
midnight::zk::WitnessOutput witness;
witness.witness_name  = "localSecretKey";
witness.output_bytes  = { /* 32 secret bytes */ };

// --- Generate proof (auto-retries if server crashes mid-process) ---
auto result = client.generate_proof_resilient(
    "transfer",          // circuit name
    circuit_bytes,       // compiled .zkir data
    inputs,
    witness);

if (result.success) {
    std::string proof_hex = result.proof.to_hex();
    // include proof_hex in your transaction payload
}
```

---

### Bài 5 — State & Compact Contracts

Deploy Compact contracts, sync on-chain ledger state, and manage off-chain private state.

```cpp
#include "midnight/compact-contracts/compact_contracts.hpp"
#include "midnight/zk/ledger_state_sync.hpp"

// --- Deploy a Compact contract ---
midnight::compact_contracts::ContractDeployer deployer(node_rpc_url);
auto deployed = deployer.deploy_contract(
    "FaucetAMM", compiled_contract, constructor_args);

if (deployed.success)
    std::cout << "contract=" << deployed.contract_address << "\n";

// --- Interact with a deployed contract ---
midnight::compact_contracts::ContractInteractor interactor(
    node_rpc_url, deployed.contract_address);
auto call_result = interactor.call_function("increment", call_args);

// --- Ledger state sync (on-chain + off-chain private state) ---
midnight::zk::LedgerStateSyncManager sync(
    "https://rpc.preprod.midnight.network",
    std::chrono::seconds(6));
sync.start_monitoring();
sync.trigger_full_sync();          // pull latest state snapshot
```

---

### Bài 6 — Monitoring & GRANDPA Finality

Subscribe to new blocks, detect reorgs, track a transaction from mempool to finality, and await GRANDPA confirmation.

```cpp
#include "midnight/monitoring-finality/monitoring_finality.hpp"

const std::string rpc     = "https://rpc.preprod.midnight.network";
const std::string indexer = "https://indexer.preprod.midnight.network/api/v3/graphql";

// --- Subscribe to new blocks (polls every ~4–6 s) ---
midnight::monitoring_finality::BlockMonitor block_monitor(rpc, indexer);
block_monitor.subscribe_new_blocks([](const std::string& hash) {
    std::cout << "new block: " << hash << "\n";
});

// --- Detect chain reorgs ---
auto reorg = block_monitor.detect_reorg();
if (reorg)
    std::cout << "reorg at height " << reorg->fork_height << "\n";

// --- Track tx lifecycle: mempool → block → finalized ---
midnight::monitoring_finality::TransactionMonitor tx_monitor(rpc, indexer);
tx_monitor.monitor_transaction(tx_hash, [](const auto& update) {
    using S = midnight::monitoring_finality::TransactionStatusUpdate;
    if (update.status == S::FINALIZED_CONFIRMED)
        std::cout << "tx finalized at block " << update.block_height << "\n";
});

// --- Await GRANDPA finality for a specific block (non-blocking) ---
midnight::monitoring_finality::FinalizationMonitor fin_monitor(rpc);
fin_monitor.wait_for_finality(target_block_height, [](bool ok) {
    std::cout << (ok ? "finalized ✓" : "timeout ✗") << "\n";
});

// --- Or monitor finalization continuously ---
fin_monitor.monitor_finalization([](uint32_t finalized_height) {
    std::cout << "GRANDPA finalized up to " << finalized_height << "\n";
});
```

---

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
