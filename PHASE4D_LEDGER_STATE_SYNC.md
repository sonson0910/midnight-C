# Phase 4D: Ledger State Management with Synchronization

## Overview

**Phase 4D** adds automatic ledger state management and synchronization to the Midnight SDK. The `LedgerStateSyncManager` keeps off-chain (cached) and on-chain state in sync through:

- **Automatic State Synchronization** - Tracks contract state changes on the blockchain
- **Block Event Monitoring** - Listens for new blocks and emits events
- **Reorg Detection & Recovery** - Handles blockchain reorganizations gracefully
- **State Caching** - Local cache with TTL for performance
- **Conflict Resolution** - Resolves divergences between on-chain and off-chain state
- **Comprehensive Monitoring** - Statistics and diagnostics for operational visibility

## Architecture

### Complete Smart Contract Proof Flow (4A+4B+4C+4D)

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. Smart Contract Execution                                      │
│    - Private logic with witness data                             │
│    - Off-chain computation                                       │
└──────────────────────┬───────────────────────────────────────────┘
                       │ WitnessOutput (Phase 4B)
┌──────────────────────▼───────────────────────────────────────────┐
│ 2. Private State Management (Phase 4B)                           │
│    - SecretKeyStore (private witness secrets)                    │
│    - PrivateStateManager (contract state)                        │
└──────────────────────┬───────────────────────────────────────────┘
                       │ CircuitProof, PublicInputs (Phase 4A)
┌──────────────────────▼───────────────────────────────────────────┐
│ 3. Basic Proof Server Client (Phase 4A)                          │
│    - HTTP communication (localhost:6300)                         │
│    - JSON serialization                                          │
└──────────────────────┬───────────────────────────────────────────┘
                       │ Raw exception on network issue
┌──────────────────────▼───────────────────────────────────────────┐
│ 4. Proof Server Resilience (Phase 4C)                            │
│    - Exponential backoff, circuit breaker, health checks         │
│    - Transparent error recovery                                  │
└──────────────────────┬───────────────────────────────────────────┘
                       │ Verified CircuitProof
┌──────────────────────▼───────────────────────────────────────────┐
│ 5. LEDGER STATE MANAGEMENT (Phase 4D) ← NEW                      │
│    - Automatic state synchronization                             │
│    - Block event monitoring                                      │
│    - Reorg detection and recovery                                │
│    - Conflict resolution                                         │
│    ✓ Keeps SDK in sync with blockchain
│    ✓ Handles reorgs automatically
│    ✓ Visible state tracking
└──────────────────────┬───────────────────────────────────────────┘
                       │ ProofEnabledTransaction
┌──────────────────────▼───────────────────────────────────────────┐
│ 6. Proof-Enabled Transaction & Blockchain                        │
│    - Transaction submitted to blockchain                         │
│    - Smart contract state updated on-chain                       │
│    - State synchronized back to Phase 4D cache                   │
└──────────────────────────────────────────────────────────────────┘
```

## Core Concepts

### 1. Block Event Monitoring

Watches the blockchain for state changes relevant to registered contracts.

**Event Types**:

```cpp
enum class EventType {
    BLOCK_ADDED,    // New block added to chain
    BLOCK_REORG,    // Chain reorganization
    STATE_CHANGED   // Contract state changed
};
```

**Workflow**:
```
┌─────────────────────┐
│ New Block on Chain  │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Poll RPC for Block  │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Extract Transactions│
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Find State Changes  │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Update Local Cache  │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Emit BlockEvent     │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ Call User Callback  │
└─────────────────────┘
```

**Usage**:
```cpp
// Register for block events
sync_manager->register_block_event_listener(
    [](const BlockEvent& event) {
        std::cout << "Block " << event.block.height << " received" << std::endl;
        for (const auto& addr : event.affected_contracts) {
            std::cout << "  State changed: " << addr << std::endl;
        }
    }
);
```

### 2. Automatic State Synchronization

Keeps cached contract state in sync with on-chain state.

**States Monitored**:
```
ContractState {
  contract_address       // Smart contract identity
  public_state           // Public state (visible on-chain)
  private_state_commit   // ZK commitment (privacy preserved)
  block_height           // Block where last updated
  transaction_id         // Transaction that caused update
  updated_at             // Last update timestamp
  confirmations          // Confirmation count
}
```

**Sync Points**:
1. **Automatic** - On block events (when registered blocks arrive)
2. **Manual** - Via `sync_contract_state()` or `trigger_full_sync()`
3. **Polling** - Periodic polling based on `poll_interval`

**Sync Result**:
```cpp
SyncResult {
  Status status;                  // SUCCESS, PARTIAL, DESYNCED, REORG, ERROR
  uint64_t on_chain_height;      // Latest on-chain block height
  uint64_t local_height;         // Latest locally tracked height
  json state_diff;               // Changes from last sync
  vector<ContractState> changes; // Updated states
  uint32_t sync_time_ms;         // Sync duration
}
```

### 3. Blockchain Reorganization Handling

Detects and recovers from blockchain reorgs (common when using probabilistic finality).

**Reorg Detection**:
```
┌─────────────────────────┐
│ Monitor Block Hashes    │
└──────────┬──────────────┘
           │
           ▼ (Hash mismatch)
┌─────────────────────────┐
│ Calculate Reorg Depth   │
│ (how many blocks back)  │
└──────────┬──────────────┘
           │
           ▼
┌─────────────────────────┐
│ Report BLOCK_REORG      │
│ Event + Depth           │
└─────────────────────────┘
```

**Reorg Recovery**:
```
Reorg Detected (depth = 3 blocks)
│
├─ Invalidate all cache entries at height > (current - 3)
│
├─ Request new canonical blocks from RPC
│
├─ Re-sync affected contracts
│
└─ Emit BLOCK_REORG event for application handling
```

**3-State Tracking**:
```
┌──────────────────────────────────┐
│ Normal Operation                 │
│ CLOSED: chain_height >= local_h  │
└───────────────────┬──────────────┘
                    │ Reorg detected
┌───────────────────▼──────────────┐
│ Reorg In Progress                │
│ OPEN: invalidating & re-syncing  │
└───────────────────┬──────────────┘
                    │ Recovery complete
┌───────────────────▼──────────────┐
│ Normal Operation Resumed         │
│ CLOSED: state restored           │
└──────────────────────────────────┘
```

### 4. State Caching

Local cache improves performance by avoiding repeated RPC queries.

**Cache Configuration**:
```cpp
StateCacheConfig {
  bool enable_caching = true;
  chrono::seconds cache_ttl{300};           // 5 minutes
  size_t max_cached_states = 1000;
  chrono::seconds stale_state_warning{600}; // 10 minutes
  bool enable_state_validation = true;      // Validate vs on-chain
}
```

**Cache Policies**:

1. **TTL (Time-To-Live)**: Entries expire after `cache_ttl` seconds
2. **LRU Eviction**: When cache full, evict least-recently-used entry
3. **Validation**: Optional periodic validation that cached state matches on-chain
4. **Staleness Detection**: Warn if state older than `stale_state_warning`

**Cache Lifecycle**:
```
State Update
  │
  ├─ Add to state_history[contract]
  │  (keep last 1000 updates)
  │
  ├─ Update state_cache[contract]
  │  with timestamp
  │
  └─ Emit event to listeners
```

### 5. Conflict Resolution

When on-chain and cached state diverge, resolution strategy is configurable.

**Conflict Scenarios**:

1. **Stale Cache**
   - Local: height=100, updated 10m ago
   - On-chain: height=105, updated now
   - Resolution: Load on-chain version

2. **Out-of-sync After Reorg**
   - Local: height=100 (orphaned)
   - On-chain: height=99 (canonical)
   - Resolution: Invalidate and re-fetch

3. **Cache Miss**
   - Local: not cached
   - On-chain: exists
   - Resolution: Load and cache

**Resolution Strategy**:
```cpp
// Prefer on-chain state (authority)
ContractState resolved = sync_manager->resolve_state_conflict(contract);
```

### 6. Configuration Profiles

Three pre-configured profiles for different deployment scenarios:

#### Development Profile
```cpp
auto mgr = ledger_util::create_dev_sync_manager();
// - Poll: 5 second intervals
// - Cache TTL: 10 minutes
// - Validation: disabled (faster)
// - Warning: 20 minutes
// Best for: Local development, testing
```

#### Production Profile
```cpp
auto mgr = ledger_util::create_production_sync_manager(rpc_endpoint);
// - Poll: 2 second intervals
// - Cache TTL: 5 minutes
// - Validation: enabled (strict)
// - Warning: 10 minutes
// - Cache size: 10,000 states
// Best for: Production deployments
```

#### Test Profile
```cpp
auto mgr = ledger_util::create_test_sync_manager();
// - Poll: 1 second intervals
// - Cache TTL: 30 seconds
// - Validation: enabled
// - Warning: 1 minute
// - Cache size: 100 states
// Best for: Automated testing, CI/CD
```

## Usage Patterns

### Pattern 1: Basic Setup with Monitoring

```cpp
// Create sync manager
auto sync_manager = std::make_shared<LedgerStateSyncManager>(
    "http://localhost:3030",
    std::chrono::milliseconds(2000)  // Poll every 2 seconds
);

// Register contracts to monitor
sync_manager->register_contract("contract_address_1");
sync_manager->register_contract("contract_address_2");

// Start monitoring
sync_manager->start_monitoring();

// Subscribe to block events
sync_manager->register_block_event_listener(
    [](const BlockEvent& event) {
        std::cout << "Block " << event.block.height << std::endl;
        for (const auto& state : event.state_changes) {
            std::cout << "State updated: " << state.contract_address << std::endl;
        }
    }
);
```

### Pattern 2: Manual Sync with State Query

```cpp
// Sync specific contract
auto result = sync_manager->sync_contract_state(contract_address);
if (result.status == SyncResult::Status::SUCCESS) {
    std::cout << "Synced to block " << result.on_chain_height << std::endl;
}

// Query cached state
auto cached = sync_manager->get_cached_state(contract_address);
if (cached) {
    std::cout << "State: " << cached->public_state.dump() << std::endl;
}
```

### Pattern 3: Conflict Detection and Resolution

```cpp
// Check for conflicts
if (sync_manager->has_state_conflict(contract_addr)) {
    std::cout << "Conflict detected, resolving..." << std::endl;
    auto resolved = sync_manager->resolve_state_conflict(contract_addr);
    std::cout << "Resolved to block " << resolved.block_height << std::endl;
}

// Get state history
auto history = sync_manager->get_state_history(contract_addr, 10);
for (const auto& state : history) {
    std::cout << "Block " << state.block_height << ": "
              << state.transaction_id << std::endl;
}
```

### Pattern 4: Reorg Handling

```cpp
// Detect reorg
uint64_t reorg_depth = sync_manager->detect_reorg();
if (reorg_depth > 0) {
    std::cout << "Reorg detected, depth: " << reorg_depth << std::endl;

    // Handle it
    if (sync_manager->handle_reorg(reorg_depth)) {
        std::cout << "Reorg handled, last stable height: "
                  << sync_manager->get_last_reorg_height() << std::endl;
    }
}
```

### Pattern 5: Monitoring and Statistics

```cpp
// Get current statistics
auto stats = sync_manager->get_sync_statistics();
std::cout << "Total syncs: " << stats.total_syncs << std::endl;
std::cout << "Success rate: "
          << (stats.successful_syncs * 100 / stats.total_syncs) << "%" << std::endl;
std::cout << "Reorgs handled: " << stats.reorgs_detected << std::endl;

// Get detailed diagnostics
auto diag = sync_manager->get_diagnostics_json();
std::cout << diag.dump(2) << std::endl;

// Check health
if (sync_manager->is_healthy()) {
    std::cout << "Manager is healthy and operational" << std::endl;
}
```

## Data Flow Examples

### Example 1: Proof Generation → State Update → Sync

```
User Application
  │
  ├─ Create proof (Phase 4A/4B/4C)
  │  - Generate via resilient client
  │  - Results in verified CircuitProof
  │
  └─ Submit to blockchain
     - ProofEnabledTransaction created
     - Sent via RPC
     - On-chain state updated
        │
        ▼
Blockchain (RPC)
  │
  ├─ Transaction included in block
  │  - Smart contract executes
  │  - Public state updated
  │  - Private commitment revealed
  │
  └─ Block added to chain (height=105)
        │
        ▼
LedgerStateSyncManager (Phase 4D)
  │
  ├─ Polls RPC for new blocks
  │  - Detects block 105
  │
  ├─ Extracts state changes
  │  - Finds transaction updating state
  │  - Extracts new ContractState
  │
  ├─ Updates local cache
  │  - state_cache[contract] = new_state
  │  - state_history[contract] += new_state
  │
  ├─ Emits BlockEvent
  │  - type: BLOCK_ADDED
  │  - affected_contracts: [contract_addr]
  │
  └─ Calls user callback
     - Application notified of state change
     - Can update UI, trigger new proofs, etc.
```

### Example 2: Reorganization Detection and Recovery

```
Normal Operation
  Local: blocks 100-105, heights synced ✓
  │
  ▼ (RPC reports height=104, block at 105 different)
Reorg Detected (depth=1)
  │
  ├─ Invalidate cache for block 105
  │  - state_cache entries removed
  │  - state_history entries marked invalid
  │
  ├─ Fetch new block 105 (canonical)
  │
  ├─ Extract state changes from new block
  │
  ├─ Update cache with new canonical state
  │
  ├─ Emit BlockEvent(BLOCK_REORG, depth=1)
  │
  └─ Application can:
     - Invalidate dependent proofs
     - Cancel pending transactions
     - Recompute state
```

## Integration with Phases 4A-4C

**Complete Smart Contract Lifecycle**:

```cpp
// 1. Setup managers (Phase 4B+4D)
auto state_mgr = PrivateStateManager();
auto secret_store = SecretKeyStore();
auto sync_mgr = ledger_util::create_production_sync_manager(rpc);

// 2. Register contract for sync (Phase 4D)
sync_mgr->register_contract(contract_address);
sync_mgr->start_monitoring();

// 3. Build witness context (Phase 4B)
auto witness_ctx = WitnessContextBuilder()
    .set_contract_address(contract_address)
    .set_public_state(current_ledger_state)
    .add_private_secret("key", secret_data)
    .build();

// 4. Generate proof with resilience (Phase 4A+4C)
auto resilient_client = ProofServerResilientClient(...);
ResilienceMetrics metrics;
auto proof = resilient_client.generate_proof_resilient(...);

// 5. Submit to blockchain (Phase 1/2)
std::string tx_id = resilient_client.submit_proof_transaction_resilient(
    ProofEnabledTransaction(proof, utxo_tx),
    rpc_endpoint,
    metrics
);

// 6. Monitor state update (Phase 4D)
sync_mgr->register_block_event_listener(
    [](const BlockEvent& event) {
        if (event.type == BlockEvent::EventType::BLOCK_ADDED) {
            // Retrieve updated state
            auto new_state = sync_mgr->get_cached_state(contract_address);
            // Use in next iteration...
        }
    }
);
```

## Performance Considerations

### Synchronization Overhead

**Per Sync Operation**:
- RPC query for block: ~100-500ms
- State extraction: ~10-50ms
- Cache update: ~1-5ms
- Event emission: ~1-2ms
- **Total**: ~120-560ms per full sync

**Optimization**:
- Cache TTL reduces queries to ~1 per 5 minutes
- Polling interval (~2s) = 30 syncs/minute = manageable load

### Memory Usage

**Per Sync Manager**:
- Configuration: ~1KB
- Statistics: ~2KB
- Event history (100 events): ~10KB
- State cache (1000 states max): ~500KB
- State history: ~2MB
- **Total**: ~2.5MB (worst case)

### Scalability

- **Single manager**: Handles 100+ contracts efficiently
- **Multiple contracts**: Batch sync with `sync_all_contracts()`
- **High-frequency updates**: Increase poll interval or use dedicated thread

## Testing Phase 4D

### Unit Test Example

```cpp
void test_state_cache_ttl() {
    auto mgr = ledger_util::create_test_sync_manager();
    auto config = mgr.get_cache_config();

    // Cache TTL should be 30 seconds for test
    assert(config.cache_ttl.count() == 30);
}

void test_reorg_detection() {
    auto mgr = ledger_util::create_test_sync_manager();

    // Initial state
    assert(mgr.get_last_reorg_height() == 0);

    // Detect reorg (mock)
    uint64_t depth = mgr.detect_reorg();
    assert(depth == 0);  // No reorg in normal operation
}
```

### Integration Test Example

```cpp
void test_full_sync_cycle() {
    auto mgr = ledger_util::create_test_sync_manager(
        "http://localhost:3030"
    );

    mgr->register_contract("test_contract");
    mgr->start_monitoring();

    // Trigger sync
    bool success = mgr->trigger_full_sync();
    assert(success);

    // Verify cache updated
    auto cached = mgr->get_cached_state("test_contract");
    assert(cached.has_value());

    // Verify statistics updated
    auto stats = mgr->get_sync_statistics();
    assert(stats.total_syncs > 0);
}
```

## Deployment Checklist

- [ ] Configure RPC endpoint for blockchain connection
- [ ] Set poll interval based on block time (~2s for fast chains)
- [ ] Choose cache profile (dev/prod/test)
- [ ] Register contracts to monitor
- [ ] Set up block event handlers
- [ ] Configure reorg depth threshold
- [ ] Enable state validation in production
- [ ] Set up monitoring/alerting for sync failures
- [ ] Test recovery from network disconnections
- [ ] Verify reorg handling with test reorg
- [ ] Document RPC endpoint topology
- [ ] Plan for high-availability setup

## Summary

Phase 4D transforms the SDK from "proof generation" focus to "complete smart contract state management":

| Feature | Phase 3 | Phase 4C | Phase 4D |
|---------|---------|---------|---------|
| Ed25519 Signing | ✓ | ✓ | ✓ |
| ZK Proof Types | ✗ | ✓ | ✓ |
| Proof Server Client | ✗ | ✓ | ✓ |
| Private State | ✗ | ✓ | ✓ |
| Resilience | ✗ | ✓ | ✓ |
| **Block Monitoring** | ✗ | ✗ | **✓** |
| **State Sync** | ✗ | ✗ | **✓** |
| **Reorg Handling** | ✗ | ✗ | **✓** |
| **Conflict Resolution** | ✗ | ✗ | **✓** |
| **Production Ready** | Signing Only | ZK Proofs | **Full Stack** |

**Result**: Midnight SDK can now build complete, production-grade smart contract applications with automatic state management and blockchain synchronization.
