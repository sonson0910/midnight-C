# Phase 4C: Proof Server Integration with Resilience

## Overview

**Phase 4C** extends the basic Proof Server client from Phase 4A with production-grade resilience features. The `ProofServerResilientClient` wrapper implements:

- **Exponential Backoff Retry Strategy**: Automatic retry with configurable backoff
- **Circuit Breaker Pattern**: Prevents cascading failures by stopping requests to failing servers
- **Health Checks**: Periodic and on-demand server health verification
- **Metrics Tracking**: Comprehensive statistics on operation success rates and retry behavior
- **Request Queuing**: Optional queueing of failed requests for later retry

## Architecture

### Layer Stack with Phase 4C

```
┌─────────────────────────────────────────────────────┐
│  Smart Contract Execution                           │
│  (Private + Proof → Blockchain)                      │
└────────────────────┬────────────────────────────────┘
                     │ ProofEnabledTransaction
┌────────────────────▼────────────────────────────────┐
│  Phase 4C: Proof Server Resilience                  │
│  - Exponential Backoff                              │
│  - Circuit Breaker                                  │
│  - Health Checks                                    │
│  - Metrics Tracking                                 │
└────────────────────┬────────────────────────────────┘
                     │ CircuitProof
┌────────────────────▼────────────────────────────────┐
│  Phase 4A: Basic Proof Server Client                │
│  - HTTP Communication (6300)                        │
│  - JSON Serialization                               │
└────────────────────┬────────────────────────────────┘
                     │ HTTP
┌────────────────────▼────────────────────────────────┐
│  Proof Server (localhost:6300)                      │
│  Docker: midnightntwrk/proof-server:8.0.3           │
└─────────────────────────────────────────────────────┘
```

## Core Concepts

### 1. Exponential Backoff Retry

Implements automatic retry with exponential delays to handle transient failures gracefully.

**Formula**: `backoff_time = initial_backoff × (multiplier ^ retry_count)`

**Example progression** (100ms initial, 2.0x multiplier):
```
Attempt 1: Immediate (0ms backoff)
Attempt 2: After 100ms
Attempt 3: After 200ms
Attempt 4: After 400ms
Attempt 5: After 800ms
Attempt 6: After capped at max (e.g., 30000ms)
```

**Benefits**:
- Avoids overwhelming a recovering server with immediate retries
- Reduces cascading failures when multiple clients retry
- Gives transient issues time to resolve
- Configurable to match specific server recovery characteristics

### 2. Circuit Breaker Pattern

Prevents cascading failures by stopping requests to a failing service.

**State Diagram**:
```
┌─────────────────────────────────────────────────────┐
│              HEALTHY OPERATION                      │
│         (Circuit Breaker: CLOSED)                   │
│  Requests proceed normally to Proof Server          │
└────────────────────┬────────────────────────────────┘
                     │ Failure threshold exceeded
┌────────────────────▼────────────────────────────────┐
│              PROTECT SYSTEM                         │
│         (Circuit Breaker: OPEN)                     │
│  All requests immediately rejected (fail-fast)      │
│  Prevents overwhelming recovering server            │
└────────────────────┬────────────────────────────────┘
                     │ Recovery timeout passes
┌────────────────────▼────────────────────────────────┐
│              TEST RECOVERY                          │
│         (Circuit Breaker: HALF_OPEN)                │
│  One request attempted to test if server recovered  │
└────────────────────┬────────────────────────────────┘
                     │
        ┌────────────┴──────────────┐
        │                           │
        ▼ (Success)                 ▼ (Failure)
    CLOSED                          OPEN
```

**Configuration Parameters**:
- `failure_threshold`: Number of failures before circuit opens (e.g., 5)
- `circuit_recovery_time`: Duration to wait before attempting recovery (e.g., 60s)

### 3. Health Checks

Monitors server availability proactively:

**Features**:
- **On-demand health check**: `perform_health_check()`
- **Automatic health checks**: Configurable interval (default 30s)
- **Status retrieved via**: `/status` endpoint on Proof Server
- **Automatic recovery**: Transitions from OPEN → CLOSED if healthy

**Use Cases**:
- Detect server availability before issuing operations
- Recover quickly when server comes back online
- Monitor long-running background processes

### 4. Metrics and Monitoring

Comprehensive statistics track resilience behavior:

**Metrics Tracked**:
- `total_operations`: Total requests attempted
- `successful_operations`: Successful operations
- `failure_rate`: (total - successful) / total
- `average_retries`: Total retries / total operations
- `circuit_breaker_state`: Current state (CLOSED/OPEN/HALF_OPEN)
- `consecutive_failures`: Current failure counter
- `queued_requests`: Requests waiting for retry

**Retrieval Methods**:
```cpp
resilient_client.get_total_operations();
resilient_client.get_successful_operations();
resilient_client.get_average_retries();
resilient_client.get_statistics_json();  // Complete stats as JSON
```

## Configuration Profiles

### Development Profile

**When to use**: Local development, testing with relaxed requirements

```cpp
auto config = resilience_util::create_dev_config();
// - Max retries: 5
// - Initial backoff: 50ms
// - Max backoff: 5000ms
// - Operation timeout: 120s (2 min)
// - Failure threshold: 10
// - Circuit recovery: 30s
```

**Benefits**: More forgiving, allows debugging, faster recovery

### Production Profile

**When to use**: Production deployment with tight SLAs

```cpp
auto config = resilience_util::create_production_config();
// - Max retries: 3
// - Initial backoff: 100ms
// - Max backoff: 30000ms
// - Operation timeout: 60s
// - Failure threshold: 5
// - Circuit recovery: 120s (2 min)
```

**Benefits**: Conservative, prevents system overload, stable under stress

### Test Profile

**When to use**: Automated testing, CI/CD pipelines

```cpp
auto config = resilience_util::create_test_config();
// - Max retries: 2
// - Initial backoff: 10ms
// - Max backoff: 100ms
// - Operation timeout: 5s
// - Failure threshold: 3
// - Circuit recovery: 5s
```

**Benefits**: Fast execution, immediate failure detection, no long waits

## Usage Patterns

### Pattern 1: Basic Setup with Production Config

```cpp
// Create base client
auto base_client = std::make_shared<ProofServerClient>();

// Wrap with production resilience
auto resilient_client = ProofServerResilientClient(
    base_client,
    resilience_util::create_production_config()
);

// All operations now have automatic retry + circuit breaker
ResilienceMetrics metrics;
try {
    bool verified = resilient_client.verify_proof_resilient(proof, metrics);
    std::cout << "Verification result: " << verified << std::endl;
    std::cout << "Retries: " << metrics.retry_count << std::endl;
} catch (const std::exception& e) {
    std::cerr << "Operation failed after all retries: " << e.what() << std::endl;
}
```

### Pattern 2: Custom Configuration

```cpp
ResilienceConfig custom_config;
custom_config.max_retries = 4;
custom_config.initial_backoff_ms = std::chrono::milliseconds(150);
custom_config.max_backoff_ms = std::chrono::milliseconds(20000);
custom_config.failure_threshold = 7;
custom_config.circuit_recovery_time = std::chrono::seconds(90);
custom_config.health_check_interval = std::chrono::seconds(45);

auto resilient_client = ProofServerResilientClient(base_client, custom_config);
```

### Pattern 3: Monitoring and Recovery

```cpp
// Check health proactively
if (!resilient_client.perform_health_check()) {
    std::cerr << "Proof Server is unavailable" << std::endl;
}

// Get detailed statistics
json stats = resilient_client.get_statistics_json();
std::cout << "Circuit breaker: " << stats["circuit_breaker_state"] << std::endl;
std::cout << "Failure rate: " << stats["failure_rate"] << std::endl;

// Manual recovery attempt
resilient_client.reset_circuit_breaker();
```

### Pattern 4: Handling Transient vs Permanent Failures

```cpp
ResilienceMetrics metrics;
try {
    auto proof = resilient_client.generate_proof_resilient(
        circuit_name, circuit_data, inputs, witnesses
    );
    std::cout << "Proof generated after " << metrics.retry_count << " retries" << std::endl;

} catch (const std::exception& e) {
    // All retries exhausted
    if (resilient_client.get_circuit_breaker_state() == CircuitBreakerState::OPEN) {
        std::cerr << "Server likely offline - circuit breaker is OPEN" << std::endl;
        std::cerr << "Will attempt recovery in "
                  << resilient_client.get_config().circuit_recovery_time.count()
                  << " seconds" << std::endl;
    } else {
        std::cerr << "Permanent failure: " << e.what() << std::endl;
    }
}
```

## Operations with Resilience

All Phase 4A operations now have resilient versions:

### generate_proof_resilient()

```cpp
ProofResult result = resilient_client.generate_proof_resilient(
    "sample_circuit",
    circuit_data,
    public_inputs,
    witness_output
);
```

**Resilience Features**:
- Automatic retry with exponential backoff
- Circuit breaker protection
- Metrics tracking per call
- Exception contains last error message

### verify_proof_resilient()

```cpp
ResilienceMetrics metrics;
bool verified = resilient_client.verify_proof_resilient(proof, metrics);
```

**Resilience Features**:
- Returns false on failure (non-throwing variant)
- Metrics available for monitoring
- Circuit breaker prevents cascading verification failures

### get_circuit_metadata_resilient()

```cpp
ResilienceMetrics metrics;
auto metadata = resilient_client.get_circuit_metadata_resilient(
    "sample_circuit",
    metrics
);
```

**Resilience Features**:
- Caches circuit structure locally during retries
- Metrics show how many attempts needed for metadata retrieval

### get_server_status_resilient()

```cpp
ResilienceMetrics metrics;
json status = resilient_client.get_server_status_resilient(metrics);
```

**Resilience Features**:
- Core operation for health checks
- Used by circuit breaker for recovery testing

### submit_proof_transaction_resilient()

```cpp
ResilienceMetrics metrics;
std::string tx_id = resilient_client.submit_proof_transaction_resilient(
    proof_enabled_tx,
    "http://rpc.example.com",
    metrics
);
```

**Resilience Features**:
- Critical path operation with full resilience
- Prevents blockchain submission failures on network hiccups

## Error Scenarios and Recovery

### Scenario 1: Transient Network Hiccup

```
Request 1: Connection refused
  ├─ Wait 100ms (initial backoff)
Request 2: Connection timeout
  ├─ Wait 200ms (exponential backoff)
Request 3: Success
└─► Operation completes with succeeded_after_retry = true
```

**SDK Behavior**: Transparent to caller; metrics show 2 retries

### Scenario 2: Temporary Server Overload

```
Requests 1-5: All timeout
  ├─ Consecutive failures = 5
  └─ Circuit breaker transitions to OPEN
Request 6: Immediately rejected (fail-fast)
  ├─ Circuit breaker prevents overwhelming server
  └─ Caller gets immediate error
[Wait 120 seconds]
Request 7: Attempted as recovery test (HALF_OPEN)
  ├─ If success: Circuit closes (CLOSED), operation succeeds
  └─ If failed: Circuit stays open (OPEN), wait again
```

**SDK Behavior**: Circuit breaker protects both server and client

### Scenario 3: Permanent Server Failure

```
All retry attempts exhaust → exception thrown
Circuit breaker opens after threshold → subsequent requests fail fast
Manual intervention required:
  ├─ Restart proof server
  ├─ Call reset_circuit_breaker()
  └─ Next request initiates recovery
```

**SDK Behavior**: Graceful degradation, clear error messages

## Integration with Phases 4A+4B

**Phase 4A** (ZK Types) + **Phase 4C** (Resilience):

```cpp
// Setup resilient client
auto base_client = std::make_shared<ProofServerClient>();
auto resilient_client = ProofServerResilientClient(
    base_client,
    resilience_util::create_production_config()
);

// [From Phase 4B] Get witness context with private data
WitnessContext witness_ctx = WitnessContextBuilder::build_from_managers(
    contract_address,
    state_mgr,
    secret_store,
    { "localSecretKey" }
);

// [TypeScript bridge] Execute witness function (external)
json witness_ctx_json = witness_ctx.to_json();
// ... call TypeScript witness function ...
WitnessOutput witness_output = /* result from TypeScript */;

// [Phase 4C] Generate proof with full resilience
ResilienceMetrics metrics;
ProofResult result = resilient_client.generate_proof_resilient(
    circuit_name,
    circuit_data,
    public_inputs,
    witness_output
);

// [Phase 4C] Get proof with lifecycle tracking
CircuitProof proof = result.proof;
std::cout << "Proof generated after " << metrics.retry_count << " retries" << std::endl;
std::cout << "Total time: " << metrics.total_time_ms.count() << "ms" << std::endl;

// [Phase 4C] Submit with resilience
std::string tx_id = resilient_client.submit_proof_transaction_resilient(
    ProofEnabledTransaction(proof, utxo_tx),
    rpc_endpoint,
    metrics
);
```

## Performance Considerations

### Retry Overhead

**Best Case** (immediate success):
- Additional overhead: ~1-2ms per operation
- No backoff delays

**Worst Case** (all retries exhausted):
- For 3 retries with 100ms initial, 2.0x multiplier:
  - Total backoff: 100 + 200 + 400 = 700ms
  - Plus operation timeouts: 3 × 60s = 180s
  - Total: ~180.7 seconds

**Recommendation**: Tune `operation_timeout_ms` and `max_retries` for your SLA

### Memory Usage

**Per Resilient Client**:
- Base ResilienceConfig: ~1KB
- Statistics tracking: ~1KB
- Request queue (max 100): ~10KB (if enabled)
- **Total**: ~12KB per client instance

### CPU Usage

**Retry Calculation**: Negligible (exponential backoff is O(n) where n = retries)

**Thread Blocking**: Uses `std::this_thread::sleep_for()` for backoff
- Blocks calling thread during backoff
- Not suitable for high-throughput async patterns
- Consider threading for production use

## Testing Phase 4C

### Unit Test Example

```cpp
void test_exponential_backoff() {
    auto resilient_client = resilience_util::create_test_resilient_client();
    ResilienceConfig config = resilient_client.get_config();

    // Verify backoff progression
    assert(config.initial_backoff_ms.count() == 10);
    assert(config.backoff_multiplier == 2.0);
    assert(config.max_backoff_ms.count() == 100);
}

void test_circuit_breaker_transitions() {
    auto client = resilience_util::create_test_resilient_client();

    // Initial state
    assert(client.get_circuit_breaker_state() == CircuitBreakerState::CLOSED);

    // Simulate failures
    for (int i = 0; i < 3; ++i) {
        // Force failure...
    }

    // After threshold
    assert(client.get_circuit_breaker_state() == CircuitBreakerState::OPEN);
}
```

### Integration Test Example

```cpp
void test_proof_generation_with_resilience() {
    auto resilient_client = resilience_util::create_test_resilient_client();

    // Attempt proof generation with real server
    ResilienceMetrics metrics;
    try {
        auto proof = resilient_client.generate_proof_resilient(
            "sample_circuit",
            get_circuit_data(),
            get_public_inputs(),
            get_witness_output()
        );

        // Verify metrics
        assert(metrics.retry_count <= resilient_client.get_config().max_retries);
        assert(metrics.total_time_ms.count() > 0);

    } catch (const std::exception& e) {
        std::cout << "Server unavailable: " << e.what() << std::endl;
        // Verify circuit breaker is protecting us
        assert(resilient_client.get_circuit_breaker_state() == CircuitBreakerState::OPEN);
    }
}
```

## Deployment Checklist

- [ ] Choose appropriate config profile (dev/prod/test)
- [ ] Set `failure_threshold` based on SLA
- [ ] Configure `circuit_recovery_time` for expected recovery duration
- [ ] Enable health checks with appropriate `health_check_interval`
- [ ] Set up monitoring for `get_statistics_json()` output
- [ ] Log metrics for operations meeting SLA target
- [ ] Test circuit breaker recovery (stop/restart proof server)
- [ ] Verify timeout values match network characteristics
- [ ] Document configuration rationale for ops team

## Summary

Phase 4C transforms Phase 4A's basic client into production-grade infrastructure:

| Feature | Phase 4A | Phase 4C |
|---------|----------|---------|
| Basic HTTP client | ✓ | ✓ |
| JSON serialization | ✓ | ✓ |
| Error messages | ✓ | ✓ |
| Automatic retry | ✗ | ✓ (exponential backoff) |
| Circuit breaker | ✗ | ✓ (3-state) |
| Health checks | ✗ | ✓ (configurable) |
| Metrics tracking | ✗ | ✓ (comprehensive) |
| Request queuing | ✗ | ✓ (optional) |
| Production ready | Partial | **Yes** |

**Result**: Midnight SDK Proof Server client now handles transient failures gracefully, prevents cascading failures, and provides comprehensive visibility into resilience behavior.
