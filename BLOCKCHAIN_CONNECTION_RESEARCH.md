# Blockchain Connection Architecture Research

## Cardano-C Library Connection Pattern

### Architecture Overview
**cardano-c** is a low-level C library designed for transaction construction, NOT direct blockchain communication.

```
┌──────────────────────┐
│   Application Code   │
└──────────────┬───────┘
               │
┌──────────────▼───────────────────────┐
│      Transaction Building Layer      │
│  (cardano-c: TxBuilder, CBOR, etc)   │
└──────────────┬───────────────────────┘
               │
┌──────────────▼───────────────────────┐
│    Provider Abstraction Layer         │
│  (Abstract interface for node comms)  │
└──────────────┬───────────────────────┘
               │
    ┌──────────┴──────────────────┐
    │                             │
┌───▼──────────┐      ┌──────────▼────┐
│  REST API    │      │  Other Methods │
│  (HTTP/RPC)  │      │  (Blockfrost,  │
└──────────────┘      │   etc.)        │
                      └────────────────┘
    │
┌───▼──────────────────────────┐
│  Cardano Node / Blockchain   │
└──────────────────────────────┘
```

### Key Design Principles:
1. **Provider Pattern**: cardano-c uses a `provider` interface/abstraction
   - Different provider implementations handle different connection methods
   - REST API providers for HTTP connections
   - Direct socket providers for P2P
   - Emscripten provider for Web environments

2. **Separation of Concerns**:
   - **Core**: Transaction serialization, CBOR encoding, cryptography
   - **Providers**: Network communication (pluggable)
   - Example: `cardano_tx_builder_new()` no longer requires provider (recent change)

3. **Direct Node Access** (Optional):
   - Can connect directly to Cardano node via socket (P2P protocol)
   - Most projects use REST API (Blockfrost, Kupo, custom services)
   - cardano-cli remains the reference implementation for direct chain interaction

### Connection Methods:
```c
// 1. REST API Provider (most common)
provider_t* provider = create_rest_provider("https://preprod.blockfrost.io");
utxos = provider->query_utxos(address);
provider->submit_transaction(tx_hex);

// 2. Custom Provider Implementation
// Implement: query_utxos(), get_protocol_params(), submit_transaction(), etc.

// 3. Direct Node (via socket/P2P)
// Rare in library code, more for specialized applications
```

---

## Midnight Blockchain Connection Pattern

### Architecture Overview
**Midnight** emphasizes privacy and confidential transactions with a different connectivity model.

```
┌──────────────────────┐
│   Application Code   │
└──────────────┬───────┘
               │
┌──────────────▼──────────────────────────┐
│      SDK Layer (Compact language)       │
│  (Smart contracts, transaction building)│
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│    GraphQL API Layer                    │
│  (Standard query interface)              │
└──────────────┬──────────────────────────┘
               │
    ┌──────────┴──────────────────┐
    │                             │
┌───▼──────────┐      ┌──────────▼────┐
│  Indexer RPC │      │  GraphQL Node  │
│  (Preprod,   │      │  Endpoints     │
│   Mainnet)   │      │  (Proof system)│
└──────────────┘      └────────────────┘
    │
┌───▼──────────────────────────┐
│  Midnight Node / Blockchain  │
│  (Privacy-first, ZK proofs)  │
└──────────────────────────────┘
```

### Key Design Principles:
1. **GraphQL-First Approach**:
   - Uses GraphQL endpoints instead of raw RPC
   - Standardized query interface across different providers
   - Built-in filtering, pagination, type safety

2. **Indexer-Based Architecture**:
   - Separate indexing services (like "midnight-server-analytics")
   - These services aggregate and serve blockchain data
   - Examples: `preprod-explorer`, `mainnet-explorer`

3. **Privacy-Native**:
   - ZK proof verification happens on-chain
   - Selective disclosure requirements affect how data is indexed
   - Confidential transactions require different query patterns

### Connection Methods:
```typescript
// 1. GraphQL Endpoint (primary)
const midnight = new MidnightClient({
  endpoint: "https://api.midnight.network/graphql"
});

// Query UTXOs
const utxos = await midnight.query(gql`
  query {
    utxos(owner: $address) {
      id
      amount
      commitments
    }
  }
`);

// 2. Indexer Services
const indexer = new MidnightIndexer({
  url: "https://preprod-explorer.midnight.network"
});

// 3. Direct Node (rare, for validators)
const node = new MidnightNode({
  rpcUrl: "http://localhost:5678"
});
```

---

## Comparison: Cardano-C vs Midnight

| Aspect | Cardano-C | Midnight |
|--------|-----------|----------|
| **Primary Model** | Provider Pattern (pluggable) | GraphQL-First API |
| **Transaction Layer** | CBOR serialization + signing | Compact language + ZK proofs |
| **Node Connection** | REST/HTTP or P2P sockets | GraphQL endpoints + Indexers |
| **Query Interface** | Method calls (provider-specific) | GraphQL queries (standardized) |
| **Data Model** | UTXOs (transparent) | UTXOs + Commitments (confidential) |
| **Default Transport** | HTTP REST (Blockfrost) | GraphQL over HTTP |
| **Abstraction Layer** | Provider interface | GraphQL schema |
| **Target Use** | General-purpose blockchain apps | Privacy/confidential apps |

---

## For the Midnight SDK Design

### Recommended Architecture Pattern:

**Option 1: Direct Node RPC (Lowest latency)**
```cpp
// Connect directly to Midnight node for highest performance
class MidnightBlockchain {
    // Internal: HTTP client to node RPC endpoint
    std::unique_ptr<MidnightNodeRPC> node_rpc_;

    // Public API remains the same
    Result<std::vector<UTXO>> query_utxos(...);
    Result<bool> submit_transaction(...);
};
```

**Option 2: GraphQL Indexer (Recommended for most apps)**
```cpp
// Use GraphQL indexer for standardized queries
class MidnightBlockchain {
    // Internal: GraphQL client for queries
    std::unique_ptr<GraphQLClient> indexer_;

    // Internal: Direct node connection only for submission
    std::unique_ptr<MidnightNodeRPC> node_for_tx_;
};
```

**Option 3: Provider Pattern (Like cardano-c, highest flexibility)**
```cpp
// Abstract provider interface
class BlockchainProvider {
    virtual std::vector<UTXO> query_utxos(...) = 0;
    virtual bool submit_transaction(...) = 0;
    virtual ProtocolParams get_protocol_params() = 0;
};

// Different implementations
class GraphQLProvider : public BlockchainProvider { ... };
class DirectNodeProvider : public BlockchainProvider { ... };
class IndexerProvider : public BlockchainProvider { ... };

class MidnightBlockchain {
    std::unique_ptr<BlockchainProvider> provider_;
};
```

### Recommendation for Your SDK:
- **Short term**: Use **Option 2** (GraphQL Indexer for queries + Direct Node for submissions)
  - More reliable than relying on single node
  - Leverages existing Midnight ecosystem infrastructure
  - Clear separation: read via indexer, write via node

- **Long term**: Consider **Option 3** (Provider Pattern)
  - Allows users to plug in their own providers
  - Matches cardano-c's proven pattern
  - Enables testing with mock providers

---

## Midnight Node Connection Details

### GraphQL Query Example (What indexers expose):
```graphql
query GetUTXOs($owner: String!) {
  utxos(owner: $owner) {
    txId
    outputIndex
    owner
    amount
    commitment  # Confidential asset commitment
    status
  }
}
```

### Direct Node RPC (What nodes expose):
- Typically **JSON-RPC** based
- Methods like: `submitTransaction`, `getChainTip`, `evaluateScript`
- Lower-level than GraphQL, more direct
- Better for privacy operations (ZK proof verification)

### Indexer (What explorers expose):
- Higher-level aggregation of chain data
- Built-in filtering: `byOwner`, `byStatus`, `byTime`
- Includes transaction history, metadata
- May have delayed updates (reorg safety delay)

---

## Next Steps for Implementation

1. **Network Client Library Choice**:
   - HTTP client: `nlohmann_json` + `cpp-httplib` or `cURL`
   - GraphQL: Consider GraphQL C++ client library

2. **Connection Options in SDK**:
   ```cpp
   enum ConnectionType {
       GRAPHQL_INDEXER,      // Read queries only
       DIRECT_NODE_RPC,      // Full RPC access
       HYBRID                 // GraphQL for reads, RPC for writes
   };
   ```

3. **Configuration**:
   ```cpp
   struct MidnightNodeConfig {
       std::string graphql_endpoint;  // e.g., "https://api.midnight.network/graphql"
       std::string rpc_endpoint;      // e.g., "http://localhost:5678"
       ConnectionType connection_type;
       uint32_t timeout_ms = 5000;
   };
   ```

4. **Research Gaps to Fill**:
   - Exact Midnight node RPC specification
   - Midnight GraphQL schema documentation
   - Transaction submission format (Compact language? CBOR? JSON?)
   - ZK proof verification requirements

---

## References
- cardano-c GitHub: https://github.com/Biglup/cardano-c
- cardano-c docs: https://cardano-c.readthedocs.io/
- Midnight docs: https://docs.midnight.network/
- Midnight examples: https://github.com/midnightntwrk/
