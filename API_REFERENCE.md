# Midnight SDK - Complete API Reference

## Table of Contents

1. [Phase 1: RPC Architecture](#phase-1-rpc-architecture)
2. [Phase 2: HTTPS Transport](#phase-2-https-transport)
3. [Phase 3: Ed25519 Cryptography](#phase-3-ed25519-cryptography)
4. [Phase 4A: ZK Proof Types](#phase-4a-zk-proof-types)
5. [Phase 4B: Private State Management](#phase-4b-private-state-management)
6. [Phase 4C: Resilience](#phase-4c-resilience)
7. [Phase 4D: Ledger State Sync](#phase-4d-ledger-state-synchronization)
8. [Phase 4E: Deployment Utilities](#phase-4e-deployment-utilities)
9. [Phase 5: Official SDK Bridge (No Mock Transfer)](#phase-5-official-sdk-bridge-no-mock-transfer)

---

## Phase 1: RPC Architecture

### RpcServer

```cpp
class RpcServer {
public:
    // Initialize RPC server on specified host/port
    RpcServer(const std::string& host, uint16_t port);

    // Start listening for connections
    void start();

    // Stop listening
    void stop();

    // Check if server is running
    bool is_running() const;
};
```

### TransactionBuilder

```cpp
class TransactionBuilder {
public:
    // Create new transaction builder
    TransactionBuilder();

    // Set transaction sender
    TransactionBuilder& set_sender(const std::string& address);

    // Set transaction recipient
    TransactionBuilder& set_recipient(const std::string& address);

    // Set transaction amount
    TransactionBuilder& set_amount(uint64_t amount);

    // Set gas limit
    TransactionBuilder& set_gas_limit(uint64_t gas);

    // Add data payload
    TransactionBuilder& set_data(const std::vector<uint8_t>& data);

    // Build transaction
    Transaction build();
};
```

### BlockHandler

```cpp
class BlockHandler {
public:
    // Get block by number
    Block get_block(uint64_t block_number);

    // Get latest block
    Block get_latest_block();

    // Get block transactions
    std::vector<Transaction> get_block_transactions(uint64_t block_number);

    // Get block by hash
    Block get_block_by_hash(const std::string& block_hash);
};
```

---

## Phase 2: HTTPS Transport

### HttpsClient

```cpp
class HttpsClient {
public:
    // Create HTTPS client for endpoint
    explicit HttpsClient(const std::string& endpoint_url);

    // Make GET request
    HttpResponse get(const std::string& path,
                     const std::map<std::string, std::string>& headers = {});

    // Make POST request
    HttpResponse post(const std::string& path,
                      const std::string& body,
                      const std::map<std::string, std::string>& headers = {});

    // Set connection timeout
    void set_timeout_ms(uint32_t timeout_ms);

    // Close all connections
    void close();

    // Check if connected
    bool is_connected() const;
};
```

### ConnectionPool

```cpp
class ConnectionPool {
public:
    // Create connection pool for endpoint
    ConnectionPool(const std::string& endpoint, size_t pool_size = 4);

    // Get available connection
    std::shared_ptr<Connection> get_connection();

    // Return connection to pool
    void return_connection(std::shared_ptr<Connection> conn);

    // Get pool statistics
    PoolStatistics get_statistics() const;

    // Current connection count
    size_t active_connections() const;

    // Available connections
    size_t available_connections() const;
};
```

### TlsContext

```cpp
class TlsContext {
public:
    // Create TLS context with certificate verification
    TlsContext();

    // Load CA certificates from file
    void load_ca_certificates(const std::string& cert_file);

    // Enable/disable certificate verification
    void set_verify_peer(bool verify);

    // Set minimum TLS version
    void set_min_tls_version(TlsVersion version);

    // Get active TLS version
    TlsVersion get_active_version() const;

    // Get cipher name
    std::string get_cipher_name() const;
};
```

---

## Phase 3: Ed25519 Cryptography

### Ed25519Signer

```cpp
class Ed25519Signer {
public:
    // Generate new Ed25519 keypair
    static KeyPair generate_keypair();

    // Create signer with private key
    explicit Ed25519Signer(const std::vector<uint8_t>& private_key);

    // Sign message
    std::vector<uint8_t> sign(const std::vector<uint8_t>& message);

    // Get public key
    std::vector<uint8_t> get_public_key() const;

    // Sign transaction
    std::string sign_transaction(const Transaction& tx);
};
```

### PublicKeyManager

```cpp
class PublicKeyManager {
public:
    // Create key manager
    PublicKeyManager();

    // Register public key
    void register_key(const std::string& key_id,
                     const std::vector<uint8_t>& public_key);

    // Get public key
    std::vector<uint8_t> get_key(const std::string& key_id) const;

    // Check if key exists
    bool has_key(const std::string& key_id) const;

    // Revoke key
    void revoke_key(const std::string& key_id);

    // List all registered keys
    std::vector<std::string> list_keys() const;
};
```

### SignatureValidator

```cpp
class SignatureValidator {
public:
    // Validate signature
    static bool validate(const std::vector<uint8_t>& message,
                        const std::vector<uint8_t>& signature,
                        const std::vector<uint8_t>& public_key);

    // Validate signed transaction
    bool validate_transaction(const SignedTransaction& tx);

    // Validate transaction batch
    std::vector<bool> validate_batch(
        const std::vector<SignedTransaction>& transactions);
};
```

---

## Phase 4A: ZK Proof Types

### PublicInputs

```cpp
class PublicInputs {
public:
    // Create empty public inputs
    PublicInputs();

    // Set input field
    void set_field(const std::string& name, const std::string& value);

    // Get input field
    std::string get_field(const std::string& name) const;

    // Check if field exists
    bool has_field(const std::string& name) const;

    // Serialize to JSON
    std::string to_json() const;

    // Deserialize from JSON
    static PublicInputs from_json(const std::string& json);
};
```

### WitnessOutput

```cpp
class WitnessOutput {
public:
    // Create empty witness output
    WitnessOutput();

    // Set private output field
    void set_field(const std::string& name, const std::string& value);

    // Get private output field
    std::string get_field(const std::string& name) const;

    // Serialize (encrypted)
    std::vector<uint8_t> serialize() const;

    // Deserialize (decrypted)
    static WitnessOutput deserialize(const std::vector<uint8_t>& data);

    // Size in bytes
    size_t size_bytes() const;
};
```

### ZkProof

```cpp
class ZkProof {
public:
    // Create proof with public inputs
    ZkProof(const PublicInputs& public_inputs);

    // Add proof component
    void add_component(const std::string& name,
                      const std::vector<uint8_t>& component);

    // Get proof component
    std::vector<uint8_t> get_component(const std::string& name) const;

    // Serialize to JSON
    std::string to_json() const;

    // Deserialize from JSON
    static ZkProof from_json(const std::string& json);

    // Get total size
    size_t size_bytes() const;
};
```

### ProofServerClient

```cpp
class ProofServerClient {
public:
    // Create client for Proof Server
    explicit ProofServerClient(const std::string& endpoint);

    // Generate proof from witness
    ZkProof generate_proof(const WitnessOutput& witness);

    // Verify proof on server
    bool verify_proof(const ZkProof& proof);

    // Get server status
    ServerStatus get_status();

    // Get server health
    HealthStatus get_health();

    // Close connection
    void close();
};
```

---

## Phase 4B: Private State Management

### SecretKeyStore

```cpp
class SecretKeyStore {
public:
    // Create secret key store
    SecretKeyStore();

    // Store secret with encryption
    void store_secret(const std::string& key_id,
                     const std::vector<uint8_t>& secret);

    // Retrieve secret (decrypted)
    std::vector<uint8_t> get_secret(const std::string& key_id);

    // Check if secret exists
    bool has_secret(const std::string& key_id) const;

    // Delete secret
    void delete_secret(const std::string& key_id);

    // Clear all secrets
    void clear_all();

    // Get all key IDs
    std::vector<std::string> list_keys() const;
};
```

### PrivateStateManager

```cpp
class PrivateStateManager {
public:
    // Create state manager for contract
    explicit PrivateStateManager(const std::string& contract_address);

    // Store private state
    void store_state(const std::string& state_key,
                    const std::string& state_value);

    // Retrieve private state
    std::string get_state(const std::string& state_key) const;

    // Update private state
    void update_state(const std::string& state_key,
                     const std::string& new_value);

    // Delete state entry
    void delete_state(const std::string& state_key);

    // Get state as JSON
    std::string get_state_json() const;

    // Load state from JSON
    void load_state_json(const std::string& json);
};
```

### WitnessContextBuilder

```cpp
class WitnessContextBuilder {
public:
    // Create builder for witness execution
    WitnessContextBuilder();

    // Add private secret to context
    WitnessContextBuilder& add_secret(const std::string& key,
                                     const std::vector<uint8_t>& value);

    // Add contract state to context
    WitnessContextBuilder& add_state(const std::string& address,
                                    const std::string& state_json);

    // Add public commitment to context
    WitnessContextBuilder& add_commitment(const std::string& name,
                                         const std::string& commitment);

    // Build witness context (JSON for TypeScript)
    std::string build_json();

    // Verify context is ready for execution
    bool validate() const;
};
```

---

## Phase 4C: Resilience

### ExponentialBackoff

```cpp
class ExponentialBackoff {
public:
    // Create backoff strategy
    ExponentialBackoff(uint32_t initial_delay_ms = 100,
                       double multiplier = 2.0,
                       uint32_t max_delay_ms = 30000);

    // Get next delay in milliseconds
    uint32_t next_delay_ms();

    // Reset to initial state
    void reset();

    // Get attempt count
    uint32_t get_attempt_count() const;

    // Get total delay so far
    uint64_t get_total_delay_ms() const;
};
```

### CircuitBreaker

```cpp
class CircuitBreaker {
public:
    // Create circuit breaker
    CircuitBreaker(size_t failure_threshold = 5,
                   uint32_t recovery_timeout_ms = 120000);

    // Record success
    void record_success();

    // Record failure
    void record_failure();

    // Check if circuit is open
    bool is_open() const;

    // Check if circuit is half-open (testing recovery)
    bool is_half_open() const;

    // Check if circuit is closed (normal operation)
    bool is_closed() const;

    // Get current state
    CircuitState get_state() const;

    // Get statistics
    CircuitBreakerStats get_stats() const;

    // Reset circuit
    void reset();
};
```

### ResilienceConfig

```cpp
struct ResilienceConfig {
    // Exponential backoff parameters
    uint32_t initial_delay_ms = 100;
    double backoff_multiplier = 2.0;
    uint32_t max_delay_ms = 30000;
    size_t max_retries = 3;

    // Circuit breaker parameters
    size_t circuit_failure_threshold = 5;
    uint32_t circuit_recovery_timeout_ms = 120000;

    // Timeout settings
    uint32_t operation_timeout_ms = 30000;
};

class ResilientClient {
public:
    // Create resilient client
    explicit ResilientClient(const ResilienceConfig& config);

    // Execute operation with resilience
    template<typename F>
    auto execute(F operation) -> decltype(operation());

    // Get circuit breaker for server endpoint
    CircuitBreaker& get_circuit_breaker();

    // Get statistics
    ResilienceStatistics get_statistics() const;
};
```

---

## Phase 4D: Ledger State Synchronization

### BlockEvent

```cpp
struct BlockEvent {
    uint64_t block_number;
    std::string block_hash;
    uint64_t timestamp;
    std::vector<std::string> transaction_ids;
    std::map<std::string, std::string> state_changes;
};
```

### LedgerStateSyncManager

```cpp
class LedgerStateSyncManager {
public:
    // Create sync manager
    explicit LedgerStateSyncManager(const std::string& rpc_endpoint);

    // Start monitoring blocks
    void start_monitoring();

    // Stop monitoring
    void stop_monitoring();

    // Check if monitoring is active
    bool is_monitoring() const;

    // Register block event callback
    void on_block_event(std::function<void(const BlockEvent&)> callback);

    // Get cached state for contract
    std::string get_cached_state(const std::string& contract_address);

    // Synchronize contract state from blockchain
    void sync_contract_state(const std::string& contract_address);

    // Get cache statistics
    CacheStatistics get_cache_statistics() const;

    // Get synchronization statistics
    SyncStatistics get_sync_statistics() const;

    // Detect and handle blockchain reorganization
    std::optional<ReorgEvent> get_last_reorg();
};
```

### BlockEventMonitor

```cpp
class BlockEventMonitor {
public:
    // Create monitor for RPC endpoint
    explicit BlockEventMonitor(const std::string& rpc_endpoint);

    // Start polling for blocks
    void start_polling(uint32_t poll_interval_ms = 2000);

    // Stop polling
    void stop_polling();

    // Register callback for new blocks
    void on_block(std::function<void(uint64_t block_number)> callback);

    // Get last observed block number
    uint64_t get_last_block_number() const;

    // Get polling interval
    uint32_t get_poll_interval_ms() const;
};
```

### StateCacheManager

```cpp
class StateCacheManager {
public:
    // Create state cache
    StateCacheManager(size_t max_cached_states = 10000,
                     uint32_t ttl_seconds = 300);

    // Store state in cache
    void cache_state(const std::string& address, const std::string& state);

    // Get state from cache
    std::optional<std::string> get_cached(const std::string& address);

    // Invalidate cache entry
    void invalidate(const std::string& address);

    // Clear entire cache
    void clear();

    // Get cache statistics
    CacheStats get_stats() const;

    // Set TTL for entries
    void set_ttl_seconds(uint32_t ttl);

    // Evict expired entries
    void evict_expired();
};
```

### ReorgDetector

```cpp
class ReorgDetector {
public:
    // Create reorg detector
    ReorgDetector(uint32_t max_reorg_depth = 10);

    // Record block hash at height
    void record_block(uint64_t height, const std::string& hash);

    // Detect reorganization
    std::optional<ReorgEvent> detect_reorg(
        uint64_t current_height,
        const std::string& current_hash);

    // Get reorg statistics
    ReorgStats get_stats() const;

    // Get longest reorg detected
    uint64_t get_longest_reorg_depth() const;

    // Clear history
    void clear();
};

struct ReorgEvent {
    uint64_t reorg_depth;
    uint64_t first_affected_block;
    uint64_t last_affected_block;
    std::vector<std::string> affected_states;
};
```

---

## Phase 4E: Deployment Utilities

### DeploymentConfig

```cpp
struct DeploymentConfig {
    // Environment
    std::string environment;  // "development", "production", "edge"

    // RPC Configuration
    std::string rpc_endpoint;
    uint32_t rpc_timeout_ms = 5000;
    size_t rpc_retries = 3;

    // Proof Server Configuration
    std::string proof_server_endpoint;
    uint32_t proof_server_timeout_ms = 30000;
    size_t proof_server_max_retries = 3;

    // Resilience Parameters
    uint32_t exponential_backoff_initial_ms = 100;
    double exponential_backoff_multiplier = 2.0;
    size_t circuit_breaker_failure_threshold = 5;

    // Synchronization Parameters
    uint32_t ledger_sync_poll_interval_ms = 2000;
    uint32_t ledger_sync_cache_ttl_seconds = 300;
    size_t ledger_sync_cache_size = 10000;

    // Monitoring
    std::string log_level;  // "DEBUG", "INFO", "WARNING", "ERROR"
    bool metrics_enabled = true;
    uint16_t metrics_port = 9090;
};

class ConfigurationManager {
public:
    // Load config from environment variables
    static DeploymentConfig load_from_env();

    // Load config from file
    static DeploymentConfig load_from_file(const std::string& path);

    // Get preset config
    static DeploymentConfig get_development_config();
    static DeploymentConfig get_production_config();
    static DeploymentConfig get_edge_config();

    // Validate configuration
    static bool validate_config(const DeploymentConfig& config);
};
```

### MonitoringSystem

```cpp
class MetricsCollector {
public:
    // Record operation latency
    void record_operation_latency(const std::string& operation,
                                 uint64_t latency_ms);

    // Record operation success
    void record_operation_success(const std::string& operation);

    // Record operation failure
    void record_operation_failure(const std::string& operation);

    // Get metric value
    double get_metric(const std::string& metric_name) const;

    // Get all metrics as JSON
    std::string get_metrics_json() const;

    // Export metrics in Prometheus format
    std::string export_prometheus_format() const;
};

class HealthChecker {
public:
    // Check RPC endpoint health
    bool check_rpc_health();

    // Check Proof Server health
    bool check_proof_server_health();

    // Get overall system health
    SystemHealth get_system_health();

    // Get diagnostics JSON
    std::string get_diagnostics_json() const;
};

struct SystemHealth {
    bool rpc_healthy;
    bool proof_server_healthy;
    bool cache_healthy;
    uint32_t cache_hit_rate_percent;
    uint32_t average_latency_ms;
    std::string circuit_breaker_state;
};
```

---

## Phase 5: Official SDK Bridge (No Mock Transfer)

### Core C++ APIs

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

midnight::blockchain::OfficialTransferResult transfer_official_night(
    const std::string& seed_hex,
    const std::string& to_address,
    const std::string& amount,
    const std::string& network = "preprod",
    const std::string& proof_server = "",
    uint32_t sync_timeout_seconds = 120,
    const std::string& bridge_script = "tools/official-transfer-night.mjs");

bool register_wallet_seed_hex(
    const std::string& wallet_alias,
    const std::string& seed_hex,
    const std::string& network = "",
    const std::string& wallet_store_dir = "",
    std::string* error = nullptr);

midnight::blockchain::OfficialTransferResult transfer_official_night_with_wallet(
    const std::string& wallet_alias,
    const std::string& to_address,
    const std::string& amount,
    const std::string& network = "preprod",
    const std::string& proof_server = "",
    uint32_t sync_timeout_seconds = 120,
    const std::string& wallet_store_dir = "",
    const std::string& bridge_script = "tools/official-transfer-night.mjs");

midnight::blockchain::OfficialWalletStateResult query_official_wallet_state_with_wallet(
    const std::string& wallet_alias,
    const std::string& network = "preprod",
    const std::string& proof_server = "",
    const std::string& node_url = "",
    const std::string& indexer_url = "",
    const std::string& indexer_ws_url = "",
    uint32_t sync_timeout_seconds = 120,
    const std::string& wallet_store_dir = "",
    const std::string& bridge_script = "tools/official-transfer-night.mjs");

midnight::blockchain::OfficialContractDeployResult deploy_official_compact_contract_with_wallet(
    const std::string& wallet_alias,
    const std::string& contract = "FaucetAMM",
    const std::string& network = "undeployed",
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
    const std::string& wallet_store_dir = "",
    const std::string& bridge_script = "tools/official-deploy-contract.mjs");
```

`tools/official-transfer-night.mjs` and `tools/official-deploy-contract.mjs` now support `--seed-file <path>` in addition to `--seed <hex>`. The C++ bridge uses a short-lived temp seed file by default for transfer/state/indexer/deploy commands so the raw seed no longer appears directly in process arguments.

Wallet alias storage is encrypted at rest (AES-256-GCM) and requires:

- `MIDNIGHT_WALLET_STORE_PASSPHRASE` set to at least 16 characters before `register_wallet_seed_hex` (or `wallet-add`) and before any alias-based read/use.
- OpenSSL crypto support enabled in the C++ build.

### Required Runtime Dependencies (Real Network Path)

- Node.js and npm are required by the official bridge scripts.
- The transfer bridge dynamically imports these packages:
  - `@midnight-ntwrk/midnight-js-network-id`
  - `@midnight-ntwrk/wallet-sdk-hd`
  - `@midnight-ntwrk/wallet-sdk-unshielded-wallet`
  - `@midnight-ntwrk/wallet-sdk-address-format`
  - `@midnight-ntwrk/wallet-sdk-facade`
  - `@midnight-ntwrk/wallet-sdk-shielded`
  - `@midnight-ntwrk/wallet-sdk-dust-wallet`
  - `@midnight-ntwrk/ledger-v8`
  - `undici`
  - `ws`
- Module resolution order in `tools/official-transfer-night.mjs`:
  1. `MIDNIGHT_SDK_NODE_MODULES` (if set)
  2. `./node_modules`
  3. `./midnight-research/node_modules` (legacy fallback)
- C++ dependencies (`nlohmann_json`, `fmt`, `jsoncpp`, `cpp-httplib`) are auto-resolved by CMake (`find_package` + `FetchContent` fallback).

### C++ NIGHT Transfer Example (No Mock)

```cpp
#include <iostream>
#include <string>

#include "midnight/blockchain/official_sdk_bridge.hpp"

int main() {
    const std::string network = "undeployed";
    const std::string wallet_alias = "ops_wallet";
    const std::string seed_hex = "<64-hex-seed>";
    const std::string to_address = "<mn_addr_destination>";
    const std::string amount = "1000000000000000000"; // smallest units
    const uint32_t timeout_s = 120;

    // Required for encrypted wallet alias storage.
    // export MIDNIGHT_WALLET_STORE_PASSPHRASE='your-strong-passphrase'

    std::string register_error;
    if (!midnight::blockchain::register_wallet_seed_hex(wallet_alias, seed_hex, network, "", &register_error)) {
        std::cerr << "Wallet registration failed: " << register_error << '\n';
        return 1;
    }

    const auto state = midnight::blockchain::query_official_wallet_state_with_wallet(
        wallet_alias,
        network,
        "",
        "",
        "",
        "",
        timeout_s,
        "",
        "tools/official-transfer-night.mjs");

    if (!state.success) {
        std::cerr << "Wallet state query failed: " << state.error << '\n';
        return 2;
    }

    if (state.likely_dust_designation_lock || state.likely_pending_utxo_lock) {
        std::cerr << "Wallet appears locked by pending dust/UTXO state. Wait and retry.\n";
        return 3;
    }

    const auto tx = midnight::blockchain::transfer_official_night_with_wallet(
        wallet_alias,
        to_address,
        amount,
        network,
        "",
        timeout_s,
        "",
        "tools/official-transfer-night.mjs");

    if (!tx.success) {
        std::cerr << "Transfer failed: " << tx.error << '\n';
        return 4;
    }

    std::cout << "Transfer success\n";
    std::cout << "Source: " << tx.source_address << '\n';
    std::cout << "Destination: " << tx.destination_address << '\n';
    std::cout << "Amount: " << tx.amount << '\n';
    std::cout << "TxId: " << tx.txid << '\n';
    return 0;
}
```

### Build and Run

```bash
cmake --build .cmake-build/manual --target official_sdk_bridge_example -j
export MIDNIGHT_WALLET_STORE_PASSPHRASE='your-strong-passphrase'
./.cmake-build/manual/bin/official_sdk_bridge_example wallet-add ops_wallet <seed_hex> undeployed
./.cmake-build/manual/bin/official_sdk_bridge_example transfer-wallet undeployed ops_wallet <mn_addr...> 1000000000000000000
./.cmake-build/manual/bin/official_sdk_bridge_example deploy-wallet undeployed ops_wallet FaucetAMM
```

---

## Complete Usage Example

```cpp
#include "midnight/sdk.h"

using namespace midnight;

int main() {
    // Phase 4E: Load production configuration
    auto config = ConfigurationManager::get_production_config();

    // Phase 4B: Initialize private state management
    auto secret_store = std::make_shared<SecretKeyStore>();
    auto state_mgr = std::make_shared<PrivateStateManager>("0xvoting");

    // Phase 4C: Create resilient client
    auto resilient_client = std::make_shared<ResilientClient>(
        ResilienceConfig{
            .max_retries = 3,
            .circuit_failure_threshold = 5
        }
    );

    // Phase 4D: Initialize ledger sync
    auto sync_mgr = std::make_shared<LedgerStateSyncManager>(
        config.rpc_endpoint
    );

    sync_mgr->on_block_event([](const BlockEvent& event) {
        std::cout << "Block " << event.block_number << " received\n";
    });

    sync_mgr->start_monitoring();

    // Phase 4A: Create and verify proofs
    PublicInputs inputs;
    inputs.set_field("commitment", "0x...");

    ZkProof proof(inputs);

    // Phase 4E: Monitor operations
    MetricsCollector metrics;

    // Cast a vote with full SDK
    auto vote_result = resilient_client->execute([&]() {
        return sync_mgr->get_cached_state("0xvoting");
    });

    std::cout << "Production voting app ready!\n";

    return 0;
}
```

---

## API Summary

**Total API Surface**: 100+ classes and functions

| Phase | Domain | Classes | Methods |
| ----- | ------ | ------- | ------- |
| 1 | RPC Architecture | 3 | 20+ |
| 2 | HTTPS Transport | 3 | 15+ |
| 3 | Cryptography | 3 | 18+ |
| 4A | ZK Proofs | 4 | 22+ |
| 4B | Private State | 3 | 20+ |
| 4C | Resilience | 3 | 15+ |
| 4D | Ledger Sync | 4 | 25+ |
| 4E | Deployment | 3 | 12+ |
| **Total** | | **26** | **147+** |

This comprehensive API provides production-ready functionality for implementing privacy-preserving smart contracts on the Midnight blockchain.
