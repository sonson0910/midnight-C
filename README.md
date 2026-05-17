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
- Production-canonical wallet address derivation through native `libmidnight_ledger_ffi`
- Transaction build/prove/serialize through the Midnight ledger backend
- UTXO, block, transaction, contract-state, inclusion, and finality queries
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

### Production Midnight Setup

Production wallet and transaction flows require the native ledger FFI built from
`midnight-research` so address derivation, proof payloads, transaction bytes,
and hashes stay canonical.
The repository records the active source-compatible version set in
[`midnight-versions.json`](midnight-versions.json). Treat the local
`midnight-research` source tree as the build source of truth when public docs
lag behind release-candidate code.

```bash
export MIDNIGHT_NETWORK=preview
export MIDNIGHT_LEDGER_FFI_LIBRARY="$PWD/build/lib/libmidnight_ledger_ffi.dylib"
export MIDNIGHT_NODE_URL="https://rpc.preview.midnight.network"
export MIDNIGHT_SOURCE_URL="wss://rpc.preview.midnight.network"
export MIDNIGHT_INDEXER_URL="https://indexer.preview.midnight.network/api/v4/graphql"
export MIDNIGHT_PROOF_SERVER_URL="http://127.0.0.1:6300"
export MIDNIGHT_STATE_MODE="local-cache"

./build/bin/wallet-generator-new
```

`midnight::blockchain::Wallet` is retained for encrypted seed storage and legacy
compatibility. It must not be used to produce production Midnight transactions.
Use `midnight::production::MidnightClient` for build, submit, and confirmation.

For future maintainers and Codex/ChatGPT sessions, read
[`docs/MIDNIGHT_CXX_SDK_CONTEXT.md`](docs/MIDNIGHT_CXX_SDK_CONTEXT.md) first.
It records the current production communication model, ledger FFI boundary,
live-test workflow, and the Midnight-specific format rules that should not be
regressed.

## Module Quick Start (2–6)

### 2 — Network & RPC

Switch between networks, query the chain, submit and confirm transactions.

```cpp
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/network/network_config.hpp"

// Pick a network: DEVNET | PREVIEW | PREPROD | MAINNET
auto endpoint = midnight::network::NetworkConfig::get_rpc_endpoint(
    midnight::network::NetworkConfig::Network::PREVIEW);

midnight::network::MidnightNodeRPC rpc(endpoint, /*timeout_ms=*/5000);

if (rpc.is_ready()) {
    auto [height, hash] = rpc.get_chain_tip();
    std::cout << "tip: " << height << " " << hash << "\n";

    // Broadcast ledger-serialized Midnight transaction bytes as
    // Midnight::send_mn_transaction.
    std::string tx_id = rpc.submit_transaction(transaction_hex);

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
auto info = client.ledger_backend_info();
auto validation = client.validate_ledger_value("contract-address", contract_hex);
auto tx_info = client.inspect_transaction_hex(ledger_built_deploy_tx_hex);
auto state = client.query_contract_state(contract_hex);
```

Do not use `ContractManager::encode_constructor_args()` or
`ContractManager::encode_call_args()` for production. They intentionally throw:
Compact constructor/call payloads must come from compiled Compact artifacts and
the native ledger/toolkit path.

### 6 — Production Transaction Building

The C++ production path uses a native Midnight ledger backend loaded through a
small C ABI (`libmidnight_ledger_ffi`). The backend returns canonical serialized
Midnight ledger transaction bytes; C++ then wraps them as
`Midnight::send_mn_transaction`, submits through `author_submitExtrinsic`, saves
artifacts, and waits until the indexer sees the ledger transaction hash.

```cpp
#include "midnight/production/midnight_client.hpp"

auto config = midnight::production::ClientConfig::for_network(
    midnight::network::NetworkConfig::Network::PREPROD);
config.ledger_ffi_library = "/absolute/path/to/libmidnight_ledger_ffi.dylib";
midnight::production::MidnightClient client(config);

midnight::ledger::TransferNightParams tx;
tx.source.src_url = "wss://rpc.preprod.midnight.network";
tx.source_seed = "<wallet-seed-hex>";
tx.destination_addresses = {"mn_addr_preprod1..."};
tx.amount = "1000000000"; // u128 decimal string

midnight::production::PipelineOptions options;
options.artifacts.root_dir = "midnight-artifacts";
options.artifacts.network = "preprod";
options.artifacts.wallet_id = "wallet-a";

auto result = client.transfer_night(options, tx);
if (!result.success) {
    throw std::runtime_error(result.error.message + ": " + result.error.detail);
}

auto included = client.wait_for_inclusion(result.transaction.build.transaction_hash);
auto finalized = client.wait_for_finality(result.transaction.build.transaction_hash);
```

For applications, prefer `ClientConfig::from_environment()` when endpoints need
to be customized without recompiling. It reads `MIDNIGHT_NETWORK`,
`MIDNIGHT_NODE_URL`, `MIDNIGHT_INDEXER_URL`, `MIDNIGHT_PROOF_SERVER_URL`,
`MIDNIGHT_LEDGER_FFI_LIBRARY`, and timeout overrides.

The required native backend ABI is documented in
`include/midnight/ledger/ledger_ffi.h`. This repository now includes a native
Rust `cdylib` implementation at
`midnight-research/midnight-node/util/ledger-ffi`; it links directly against the
Midnight ledger/toolkit crates and does not shell out to `midnight-node-toolkit`.
When Rust/Cargo is installed, CMake builds it automatically with
`MIDNIGHT_BUILD_LEDGER_FFI=ON`, or you can build only the backend:

```bash
cargo build \
  --manifest-path midnight-research/midnight-node/Cargo.toml \
  --package midnight-ledger-ffi \
  --release
```

Use the resulting `libmidnight_ledger_ffi.dylib`/`.so`/`.dll` as
`ClientConfig::ledger_ffi_library`.

Production transaction builds should run from a prepared state backend, not from
an accidental cold sync on a user request path. The default is local cache:

```cpp
midnight::production::ClientConfig config =
    midnight::production::ClientConfig::from_environment();
config.state_backend_mode = midnight::production::StateBackendMode::LocalCache;

midnight::production::MidnightClient client(config);
auto status = client.state_status(source);
if (!status.ready) {
    auto refreshed = client.refresh_state(sync_params);
}
```

For managed deployments, set `MIDNIGHT_STATE_MODE=managed` and
`MIDNIGHT_STATE_SERVICE_URL=https://...`; the SDK will use the managed state
service for status/refresh while keeping C++ as the public API and ledger FFI as
the canonical transaction builder.

Live submit verification is intentionally gated and never runs by default:

```bash
export MIDNIGHT_RUN_LIVE_SUBMIT_TESTS=1
export MIDNIGHT_LEDGER_FFI_LIBRARY="$PWD/build_codex_verify_full/midnight-ledger-ffi/release/libmidnight_ledger_ffi.dylib"
export MIDNIGHT_LIVE_NODE_URL="https://rpc.preprod.midnight.network"
export MIDNIGHT_LIVE_SOURCE_URL="wss://rpc.preprod.midnight.network"
export MIDNIGHT_LIVE_SOURCE_SEED="<wallet-seed-hex>"
export MIDNIGHT_LIVE_DESTINATION_ADDRESS="mn_addr_preprod1..."
export MIDNIGHT_LIVE_AMOUNT="1000000000"
ctest --test-dir build_codex_verify_full --output-on-failure
```

Optional live-test overrides: `MIDNIGHT_LIVE_INDEXER_URL`,
`MIDNIGHT_LIVE_PROOF_SERVER_URL`, `MIDNIGHT_LIVE_FETCH_CACHE`,
`MIDNIGHT_LIVE_LEDGER_STATE_DB`, `MIDNIGHT_LIVE_ARTIFACT_DIR`,
`MIDNIGHT_LIVE_WALLET_ID`, `MIDNIGHT_LIVE_DUST_WARP`,
`MIDNIGHT_LIVE_FETCH_CONCURRENCY`, `MIDNIGHT_LIVE_FETCH_RETRY_COUNT`,
`MIDNIGHT_LIVE_FETCH_RETRY_DELAY_MS`, and
`MIDNIGHT_LIVE_CONFIRMATION_TIMEOUT_MS`.

For an interactive preprod flow, use the helper script:

```bash
tools/midnight-preprod-live.sh generate-wallet
# Paste the printed NIGHT address into https://faucet.preprod.midnight.network/
# Start your proof server, then check it:
tools/midnight-preprod-live.sh proof-health

# First production build needs a valid ledger cache. This can take a while on
# a cold machine, but the helper writes midpoint progress to
# midnight_cache/sync-ledger-state.log. Transient preview RPC fetch failures are
# auto-resumed by default; tune with MIDNIGHT_LIVE_SYNC_MAX_RESTARTS.
tools/midnight-preprod-live.sh sync-ledger-state
tools/midnight-preprod-live.sh sync-status
tools/midnight-preprod-live.sh watch-checkpoint 10
tools/midnight-preprod-live.sh enable-local-mode
tools/midnight-preprod-live.sh local-mode-status

# If the public preview RPC drops the background WSS task during cold sync,
# retry with lower concurrency:
MIDNIGHT_LIVE_FETCH_CONCURRENCY=1 MIDNIGHT_LIVE_FETCH_RETRY_COUNT=10 \
  tools/midnight-preprod-live.sh sync-ledger-state

# Reuse the cache later or move it to another machine:
tools/midnight-preprod-live.sh export-cache
tools/midnight-preprod-live.sh import-cache midnight_cache/midnight-preview-cache.tar.gz

# After cache sync:
tools/midnight-preprod-live.sh register-dust
tools/midnight-preprod-live.sh transfer-night
```

Wallet secrets are written under `.secrets/midnight-<network>/`, which is
ignored by git. The source transaction cache and wallet ledger-state cache live
under `midnight_cache/`; applications should treat them as build artifacts and
not commit them.

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

# Consumer projects can use either:
#   find_package(midnight CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE midnight::midnight-core)
# or pkg-config:
#   pkg-config --cflags --libs midnight
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
# Requires a native libmidnight_ledger_ffi backend.
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
- ✅ Production transaction builder abstraction through native ledger FFI
- ✅ DUST registration, NIGHT transfer, Compact deploy/call, custom intent submission
- ✅ Native FFI backend info, format validation, tx hash/verify, and deploy address extraction
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
