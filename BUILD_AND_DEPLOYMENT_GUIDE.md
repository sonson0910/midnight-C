# Midnight SDK - Build and Deployment Guide

## Table of Contents

1. [Quick Start](#quick-start)
2. [Build from Source](#build-from-source)
3. [Running Examples](#running-examples)
4. [Running Tests](#running-tests)
5. [Docker Deployment](#docker-deployment)
6. [Kubernetes Deployment](#kubernetes-deployment)
7. [Production Deployment](#production-deployment)
8. [Troubleshooting](#troubleshooting)

---

## Quick Start

### Prerequisites

```bash
# Ubuntu 22.04 / Debian 12
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    pkg-config

# macOS
brew install cmake openssl@3

# Windows (MSVC 2022)
# - Download Visual Studio Community
# - Install "Desktop Development with C++" workload
# - Download CMake from cmake.org
```

### Build in 5 Minutes

```bash
git clone https://github.com/yourusername/midnight-sdk.git
cd midnight-sdk

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# Verify build
ctest --output-on-failure
```

---

## Build from Source

### Step 1: Clone Repository

```bash
git clone https://github.com/yourusername/midnight-sdk.git
cd midnight-sdk
```

### Step 2: Create Build Directory

```bash
mkdir build
cd build
```

### Step 3: Configure Build

```bash
# Development build (with debug symbols)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Production build (optimized)
cmake .. -DCMAKE_BUILD_TYPE=Release

# With specific compiler
cmake .. -DCMAKE_CXX_COMPILER=clang++
```

### Step 4: Compile

```bash
# Serial build
cmake --build . --config Release

# Parallel build (8 threads)
cmake --build . --config Release -- -j8

# Verbose output
cmake --build . --config Release -- VERBOSE=1
```

### Step 5: Install

```bash
# Install to default location
cmake --install .

# Install to custom location
cmake --install . --prefix /opt/midnight-sdk
```

### Build Configuration Options

```bash
# Enable all tests
cmake .. -DENABLE_TESTS=ON

# Enable only specific phase tests
cmake .. -DPHASE1_TESTS=ON -DPHASE4C_TESTS=ON

# Disable optional examples
cmake .. -DBUILD_EXAMPLES=OFF

# Use static linking
cmake .. -DBUILD_SHARED_LIBS=OFF
```

---

## Running Examples

### Phase 1: Basic RPC Usage

```bash
./example_phase1_basic_rpc

# Expected output:
# [INFO] Starting RPC server on localhost:3030
# [INFO] Creating transaction...
# [INFO] Transaction ID: 0x...
# [INFO] Fetching block #100
# [INFO] Block has 25 transactions
```

### Phase 2: HTTPS Transport

```bash
./example_phase2_https_transport --endpoint https://testnet.midnight.com:443

# Expected output:
# [INFO] Connecting to https://testnet.midnight.com:443
# [INFO] TLS handshake successful (TLS 1.3)
# [INFO] Connection pool: 4 connections
# [INFO] Request latency: 150ms average
```

### Phase 3: Ed25519 Signing

```bash
./example_phase3_ed25519_signing

# Expected output:
# [INFO] Generating Ed25519 keypair...
# [INFO] Public key: 0x...
# [INFO] Creating transaction
# [INFO] Signing transaction...
# [INFO] Signature: 0x...
# [INFO] Verifying signature... ✓ Valid
```

### Phase 4A: ZK Proofs

```bash
./example_phase4a_zk_proofs --proof-server localhost:6300

# Expected output:
# [INFO] Connecting to proof server...
# [INFO] Creating zk proof type
# [INFO] Sending to proof server...
# [INFO] Proof generated: 0x...
# [INFO] Proof size: 512 bytes
```

### Phase 4B: Private State Management

```bash
./example_phase4b_private_state

# Expected output:
# [INFO] Creating private state manager...
# [INFO] Storing private secrets...
# [INFO] Building witness context...
# [INFO] Witness context ready for TypeScript
# [INFO] Result: Private witness executed successfully
```

### Phase 4C: Proof Server Resilience

```bash
./example_phase4c_resilience --flaky-server

# Expected output:
# [INFO] Testing exponential backoff...
# [INFO] Attempt 1 failed (connection timeout)
# [INFO] Waiting 100ms...
# [INFO] Attempt 2 failed (server busy)
# [INFO] Waiting 200ms...
# [INFO] Attempt 3 succeeded
# [INFO] Circuit breaker state: CLOSED
```

### Phase 4D: State Synchronization

```bash
./example_phase4d_state_sync --rpc http://localhost:3030

# Expected output:
# [INFO] Monitoring block events...
# [INFO] Block #105 arrived
# [INFO] Syncing state for contract: 0x...
# [INFO] Cache hit rate: 85%
# [INFO] Reorg detected (depth=2)
# [INFO] State invalidated and re-synced
```

### Phase 4E: End-to-End Voting Application

```bash
./example_phase4e_voting_app --voting-contract 0x... --voters 100

# Expected output:
# [INFO] Midnight SDK Voting Application - Phase 4E
# [INFO] Initializing voting app...
# [INFO] Contract address: 0x...
# [INFO] Processing 100 votes...
# [INFO] Voter 1: cast vote (proof verified)
# [INFO] Voter 2: cast vote (proof verified)
# ...
# [INFO] Voting statistics:
#   - Total votes: 100
#   - Proofs verified: 100
#   - Failed proofs: 0
#   - Average proof latency: 250ms
#   - Circuit breaker: CLOSED
#   - Cache hit rate: 92%
```

---

## Running Tests

### All Tests

```bash
cd build
ctest --output-on-failure
```

### Specific Phase Tests

```bash
# Phase 1 tests
ctest -R phase1 --output-on-failure

# Phase 4C (Resilience) tests
ctest -R phase4c --output-on-failure

# Phase 4D (State Sync) tests
ctest -R phase4d --output-on-failure
```

### Verbose Test Output

```bash
ctest --output-on-failure -VV
```

### Test Coverage Report

```bash
# Enable coverage
cmake .. -DENABLE_COVERAGE=ON
cmake --build .
ctest

# Generate coverage report
gcovr --root . --html coverage.html --html-details
open coverage.html
```

---

## Docker Deployment

### Build Docker Image

```dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    ca-certificates

# Copy source
COPY . /src/midnight-sdk
WORKDIR /src/midnight-sdk

# Build
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --config Release && \
    cmake --install . --prefix /opt/midnight-sdk

# Runtime stage
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    libssl3 \
    ca-certificates

COPY --from=builder /opt/midnight-sdk /opt/midnight-sdk
ENV PATH="/opt/midnight-sdk/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/midnight-sdk/lib:${LD_LIBRARY_PATH}"

# Run voting app
ENTRYPOINT ["example_phase4e_voting_app"]
```

### Build Image

```bash
docker build -t midnight-sdk:latest .
```

### Run Container

```bash
# Single instance
docker run -d \
    --name midnight-sdk \
    -p 6300:6300 \
    -p 3030:3030 \
    -e RPC_ENDPOINT=http://testnet.midnight.com \
    -e PROOF_SERVER=localhost:6300 \
    midnight-sdk:latest

# Check logs
docker logs midnight-sdk

# Stop container
docker stop midnight-sdk
```

---

## Kubernetes Deployment

### Create Deployment YAML

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: midnight-sdk
  labels:
    app: midnight-sdk
spec:
  replicas: 3
  selector:
    matchLabels:
      app: midnight-sdk
  template:
    metadata:
      labels:
        app: midnight-sdk
    spec:
      containers:
      - name: sdk
        image: midnight-sdk:latest
        ports:
        - containerPort: 6300
          name: proof-server
        - containerPort: 3030
          name: rpc
        env:
        - name: RPC_ENDPOINT
          value: "http://midnight-rpc.default.svc.cluster.local"
        - name: PROOF_SERVER
          value: "localhost:6300"
        - name: CACHE_SIZE
          value: "10000"
        - name: LOG_LEVEL
          value: "INFO"
        resources:
          requests:
            memory: "512Mi"
            cpu: "500m"
          limits:
            memory: "1Gi"
            cpu: "1000m"
        livenessProbe:
          httpGet:
            path: /health
            port: 6300
          initialDelaySeconds: 10
          periodSeconds: 10
        readinessProbe:
          httpGet:
            path: /ready
            port: 6300
          initialDelaySeconds: 5
          periodSeconds: 5
---
apiVersion: v1
kind: Service
metadata:
  name: midnight-sdk-service
spec:
  selector:
    app: midnight-sdk
  ports:
  - port: 6300
    targetPort: 6300
    name: proof-server
  - port: 3030
    targetPort: 3030
    name: rpc
  type: LoadBalancer
```

### Deploy to Kubernetes

```bash
# Create namespace
kubectl create namespace midnight

# Deploy
kubectl apply -f deployment.yaml -n midnight

# Check status
kubectl get pods -n midnight
kubectl get svc -n midnight

# View logs
kubectl logs -n midnight -l app=midnight-sdk

# Scale up
kubectl scale deployment midnight-sdk --replicas=5 -n midnight
```

---

## Production Deployment

### Pre-Deployment Checklist

```bash
# ✓ Build configuration verified
cmake . -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# ✓ All tests pass
ctest --output-on-failure
echo "Exit code: $?"  # Should be 0

# ✓ Security scanning
./scan_secrets.sh
./check_dependencies.sh

# ✓ Performance baseline
./benchmark_phase4c_resilience
./benchmark_phase4d_state_sync

# ✓ Documentation complete
ls -la PHASE*.md
ls -la docs/

# ✓ Version tagged
git tag -a v4.0.0 -m "Production release v4.0.0"
git push origin v4.0.0
```

### Environment Configuration

Create `.env.production`:

```bash
# RPC Configuration
RPC_ENDPOINT=https://mainnet.midnight.com:443
RPC_TIMEOUT_MS=5000
RPC_RETRIES=3

# Proof Server Configuration
PROOF_SERVER_ENDPOINT=https://proof-server.example.com:443
PROOF_SERVER_TIMEOUT_MS=30000
PROOF_SERVER_MAX_RETRIES=3

# Resilience Configuration
EXPONENTIAL_BACKOFF_INITIAL_MS=100
EXPONENTIAL_BACKOFF_MAX_MS=30000
EXPONENTIAL_BACKOFF_MULTIPLIER=2.0
CIRCUIT_BREAKER_FAILURE_THRESHOLD=5
CIRCUIT_BREAKER_RECOVERY_TIMEOUT_MS=120000

# State Synchronization
LEDGER_SYNC_POLL_INTERVAL_MS=2000
LEDGER_SYNC_CACHE_TTL_SECONDS=300
LEDGER_SYNC_CACHE_SIZE=10000
LEDGER_SYNC_MAX_REORG_DEPTH=10

# Monitoring
LOG_LEVEL=WARNING
METRICS_ENABLED=true
METRICS_PORT=9090
HEALTH_CHECK_PORT=6300
```

### Deploy Procedure

```bash
# 1. Pull latest code
git pull origin main

# 2. Build
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 3. Run tests
ctest --output-on-failure

# 4. Load environment
source ../.env.production

# 5. Start service
./example_phase4e_voting_app \
    --rpc $RPC_ENDPOINT \
    --proof-server $PROOF_SERVER_ENDPOINT \
    --workers 8 \
    --cache-size $LEDGER_SYNC_CACHE_SIZE

# 6. Verify health
curl http://localhost:6300/health
curl http://localhost:9090/metrics
```

### Monitoring in Production

```bash
# View metrics
curl http://localhost:9090/metrics | grep midnight_sdk

# Check circuit breaker status
curl http://localhost:6300/diagnostics | jq '.circuit_breaker'

# View cache statistics
curl http://localhost:6300/diagnostics | jq '.cache_stats'

# Check sync latency
curl http://localhost:6300/diagnostics | jq '.sync_latency_p99_ms'
```

---

## Troubleshooting

### Build Issues

**Problem**: CMake not found
```bash
# Solution: Install CMake
sudo apt-get install cmake  # Ubuntu
brew install cmake          # macOS
```

**Problem**: OpenSSL not found
```bash
# Solution: Install OpenSSL development headers
sudo apt-get install libssl-dev  # Ubuntu
brew install openssl@3           # macOS
```

**Problem**: Compilation errors
```bash
# Solution: Check C++ standard
cmake .. -DCMAKE_CXX_STANDARD=17

# Try verbose build
cmake --build . -- VERBOSE=1
```

### Runtime Issues

**Problem**: "Circuit breaker is OPEN"
```bash
# Check Proof Server status
curl http://proof-server.example.com:6300/health

# Wait 120s for recovery
sleep 120

# Or reset manually
curl -X POST http://localhost:6300/reset-circuit-breaker
```

**Problem**: "State sync failure"
```bash
# Check RPC endpoint connectivity
curl $RPC_ENDPOINT/health

# Verify network
ping proof-server.example.com

# Check logs
tail -f /var/log/midnight-sdk.log
```

**Problem**: "Slow proof generation (>30s)"
```bash
# Check Proof Server load
curl http://proof-server.example.com:6300/metrics

# Increase timeout
export PROOF_SERVER_TIMEOUT_MS=60000

# Verify circuit breaker isn't open
curl http://localhost:6300/diagnostics | jq '.circuit_breaker.state'
```

### Performance Issues

**Problem**: High latency (>1s per operation)
```bash
# Check cache hit rate
curl http://localhost:6300/diagnostics | jq '.cache_stats.hit_rate'

# If low (<80%), increase cache size
export LEDGER_SYNC_CACHE_SIZE=20000

# Check reorg frequency
curl http://localhost:6300/diagnostics | jq '.reorg_stats'
```

**Problem**: High CPU usage
```bash
# Check polling frequency
ps aux | grep midnight-sdk

# Reduce polling if network is congested
export LEDGER_SYNC_POLL_INTERVAL_MS=5000  # Increase from 2000

# Profile with perf
perf record ./example_phase4e_voting_app
perf report
```

---

## Summary

This guide covers:

✅ Building from source (CMake, compilation, installation)
✅ Running all 8 phase examples with expected output
✅ Running comprehensive tests with coverage
✅ Docker containerization and deployment
✅ Kubernetes orchestration and scaling
✅ Production deployment procedures
✅ Monitoring and health checks
✅ Troubleshooting common issues

**The Midnight SDK is now ready for production deployment!** 🚀
