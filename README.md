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

## Module Quick Start (2–6)

### 2 — Network & RPC

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

### 3 — IoT Protocols (MQTT · CoAP · HTTP · WebSocket)

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
auto resp = http.get("https://indexer.preprod.midnight.network/api/v4/graphql");

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

### 4 — ZK Proofs

Generate proofs via the local Proof Server, isolate private witnesses, and embed the proof in a transaction.

```cpp
#include "midnight/zk/proof_server_client.hpp"

// --- Proof server at localhost:6300 by default ---
midnight::zk::ProofServerClient::Config cfg;   // host=localhost, port=6300
midnight::zk::ProofServerClient client(cfg);
auto result = client.post_prove_tx_payload(ledger_built_prove_tx_payload);
```

---

### 5 — State & Compact Contracts

Deploy Compact contracts and query state through ledger-built transaction bytes and the indexer/RPC clients.

```cpp
#include "midnight/contract/contract_manager.hpp"
#include "midnight/production/midnight_client.hpp"

midnight::contract::ContractManager mgr(node_rpc_url, proof_server_url, indexer_url);

// Transaction bytes must be produced by the Midnight ledger/proof pipeline.
auto deployed = mgr.submit_deploy_transaction(ledger_built_deploy_tx_bytes);
auto called = mgr.submit_call_transaction(ledger_built_call_tx_bytes);

midnight::production::MidnightClient client(config);
auto state = client.query_contract_state(contract_hex);
```

### 6 — Production Transaction Building

The C++ library builds production transactions by delegating to the official
`midnight-node-toolkit` from `midnight-research/midnight-node/util/toolkit`.
The returned `.mn` file is parsed as `SerializedTx`, the raw tagged
`tx.Midnight` bytes are wrapped as `Midnight::send_mn_transaction`, then
submitted via `author_submitExtrinsic`. The production pipeline can also save
artifacts and wait until the indexer sees the ledger transaction hash.

```cpp
#include "midnight/production/midnight_client.hpp"

midnight::production::ClientConfig config;
config.node_url = "https://rpc.preprod.midnight.network";
config.indexer_url = "https://indexer.preprod.midnight.network/api/v4/graphql";
config.proof_server_url = "http://127.0.0.1:6300";
midnight::production::MidnightClient client(config);

midnight::ledger::ToolkitConfig toolkit;
toolkit.toolkit_binary = "midnight-node-toolkit";
toolkit.proof_server_url = "http://127.0.0.1:6300";

midnight::ledger::TransferNightParams tx;
tx.source.src_url = "wss://rpc.preprod.midnight.network";
tx.source_seed = "<wallet-seed-hex>";
tx.destination_addresses = {"mn_addr_preprod1..."};
tx.amount = "1000000000"; // u128 decimal string

midnight::production::PipelineOptions options;
options.toolkit = toolkit;
options.artifacts.root_dir = "midnight-artifacts";
options.artifacts.network = "preprod";
options.artifacts.wallet_id = "wallet-a";

auto result = client.transfer_night(options, tx);
if (!result.success) {
    throw std::runtime_error(result.error.message + ": " + result.error.detail);
}
```

---

### 7 — Monitoring & GRANDPA Finality

Subscribe to new blocks, detect reorgs, track a transaction from mempool to finality, and await GRANDPA confirmation.

```cpp
#include "midnight/monitoring-finality/monitoring_finality.hpp"

const std::string rpc     = "https://rpc.preprod.midnight.network";
const std::string indexer = "https://indexer.preprod.midnight.network/api/v4/graphql";

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

# Connectivity and compatibility check with Midnight Preprod
# Returns exit code 0 when READY for production
./bin/http_connectivity_test

# Production ledger transaction pipeline
# Requires midnight-node-toolkit built from midnight-research/midnight-node/util/toolkit.
./bin/production_transactions_example
```

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
- ✅ Indexer v4 wallet, UTXO, transaction, and block queries
- ✅ Node JSON-RPC submission for ledger-built Midnight transactions
- ✅ Production transaction builder bridge through `midnight-node-toolkit`
- ✅ DUST registration, NIGHT transfer, Compact deploy/call, custom intent submission
- ✅ Artifact saving and indexer confirmation pipeline
- ⏳ Optional protocol integrations such as CoAPS/DTLS

## Documentation

- [MIDNIGHT_BLOCKCHAIN.md](MIDNIGHT_BLOCKCHAIN.md) - Blockchain API details
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - System architecture
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) - Getting started guide

## License

Apache License 2.0

## Contact

**Project**: Midnight SDK - Blockchain-integrated IoT infrastructure

**Repository**: [https://github.com/sonson0910/midnight-C](https://github.com/sonson0910/midnight-C)
