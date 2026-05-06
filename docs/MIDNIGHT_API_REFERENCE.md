# Midnight Network API Reference - Complete Guide

> **Version**: Indexer API v4.1.0 (2026-04-20) | Wallet SDK 5.0.0 | Node v1.0
> **Last Updated**: May 2026

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Indexer API v4.1.0 - Complete GraphQL Schema](#2-indexer-api-v41---complete-graphql-schema)
3. [Network Endpoints](#3-network-endpoints)
4. [RPC Methods](#4-rpc-methods)
5. [Wallet SDK & DApp Connector](#5-wallet-sdk--dapp-connector)
6. [How to Get Wallet Info from C Library](#6-how-to-get-wallet-info-from-c-library)
7. [Example Queries](#7-example-queries)
8. [Type Reference](#8-type-reference)

---

## 1. Architecture Overview

### 1.1 Three Components for Blockchain Interaction

```
┌─────────────────────────────────────────────────────────────────┐
│                    MIDNIGHT NETWORK LAYERS                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────┐    ┌─────────────────┐    ┌──────────────┐ │
│  │  INDEXER API    │    │  NODE RPC       │    │ WALLET SDK   │ │
│  │  (GraphQL)      │    │  (JSON-RPC)    │    │ (TypeScript) │ │
│  │                 │    │                 │    │              │ │
│  │  Queries data   │    │  Submits txs    │    │ Manages keys │ │
│  │  Indexes chain  │    │  Queries state  │    │ Signs txs    │ │
│  │  Real-time subs  │    │  Smart contracts│   │ Balances     │ │
│  └────────┬────────┘    └────────┬────────┘    └──────┬───────┘ │
│           │                        │                    │         │
│           ▼                        ▼                    ▼         │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │              YOUR APPLICATION (C Library)                    ││
│  │                                                              ││
│  │  IndexerClient → query_wallet_state()                       ││
│  │  MidnightNodeRPC → submit_transaction()                     ││
│  │  Wallet SDK → FFI/WASM bridge (optional)                    ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Token System

Midnight uses a **three-token model**:


| Token          | Type       | Privacy      | Purpose                                | Symbol |
| -------------- | ---------- | ------------ | -------------------------------------- | ------ |
| NIGHT          | Unshielded | Public       | Main token, transferable               | NIGHT  |
| Shielded NIGHT | Shielded   | Private (ZK) | Private transfers via ZSwap            | NIGHT  |
| DUST           | Public     | Public       | Transaction fees, generated from NIGHT | DUST   |


### 1.3 Address Prefixes


| Prefix           | Type          | Example                        |
| ---------------- | ------------- | ------------------------------ |
| `mn_addr`        | Unshielded    | `mn_addr_preprod1qp3...`       |
| `mn_shield-addr` | Shielded      | `mn_shield-addr_preprod1zk...` |
| `mn_dust`        | DUST          | `mn_dust_preprod1dz...`        |
| `stake_test1`    | Cardano Stake | `stake_test1uqtgpdz0...`       |
| `stake1`         | Cardano Stake | `stake1ux3ts06...`             |


---

## 2. Indexer API v4.1.0 - Complete GraphQL Schema

### 2.1 Endpoints


| Type                      | URL                                         |
| ------------------------- | ------------------------------------------- |
| HTTP (Queries/Mutations)  | `POST https://<host>:<port>/api/v4/graphql` |
| WebSocket (Subscriptions) | `wss://<host>:<port>/api/v4/graphql/ws`     |


**Public endpoints:**

- Preprod Indexer: `https://indexer.preprod.midnight.network/api/v4/graphql`
- Preprod WebSocket: `wss://indexer.preprod.midnight.network/api/v4/graphql/ws`
- Testnet-02: `https://indexer.testnet-02.midnight.network/api/v4/graphql`

### 2.2 Custom Scalar Types


| Scalar                 | Description                  | Example                    |
| ---------------------- | ---------------------------- | -------------------------- |
| `HexEncoded`           | Hex-encoded bytes            | `"3031323..."`             |
| `ViewingKey`           | Viewing key (Bech32m or hex) | `"mn_shield-esk_dev1..."`  |
| `UnshieldedAddress`    | Bech32m unshielded address   | `"mn_addr_preprod1..."`    |
| `CardanoRewardAddress` | Cardano stake address        | `"stake_test1uqtgpdz0..."` |
| `DustAddress`          | Bech32m DUST address         | `"mn_dust_preprod1dz..."`  |
| `Unit`                 | Empty return type            | -                          |


### 2.3 Input Types (BlockOffset, TransactionOffset, ContractActionOffset)

```graphql
# Block query - by hash OR height (oneOf)
input BlockOffset @oneOf {
  hash: HexEncoded      # Hex-encoded block hash
  height: Int           # Block height
}

# Transaction query - by hash OR identifier (oneOf)
input TransactionOffset @oneOf {
  hash: HexEncoded      # Hex-encoded transaction hash
  identifier: HexEncoded # Transaction identifier
}

# Contract action - by block OR transaction (oneOf)
input ContractActionOffset @oneOf {
  blockOffset: BlockOffset        # Query by block
  transactionOffset: TransactionOffset # Query by transaction
}
```

---

## 3. Network Endpoints

### 3.1 Public RPC Endpoints


| Network    | URL                                        | Type     |
| ---------- | ------------------------------------------ | -------- |
| Testnet-02 | `https://rpc.testnet-02.midnight.network/` | HTTP/WSS |
| Preview    | `https://rpc.preview.midnight.network`     | HTTP/WSS |
| Preprod    | `https://rpc.preprod.midnight.network`     | HTTP/WSS |
| Mainnet    | `https://rpc.mainnet.midnight.network`     | HTTP/WSS |


### 3.2 Public Indexer Endpoints


| Network    | HTTP                                                         | WebSocket                                                     |
| ---------- | ------------------------------------------------------------ | ------------------------------------------------------------- |
| Testnet-02 | `https://indexer.testnet-02.midnight.network/api/v4/graphql` | `wss://indexer.testnet-02.midnight.network/api/v4/graphql/ws` |
| Preprod    | `https://indexer.preprod.midnight.network/api/v4/graphql`    | `wss://indexer.preprod.midnight.network/api/v4/graphql/ws`    |


---

## 4. RPC Methods

### 4.1 Standard Substrate RPC Methods


| Method                   | Description                |
| ------------------------ | -------------------------- |
| `system_chain`           | Get chain name             |
| `system_health`          | Get node health status     |
| `system_properties`      | Get chain properties       |
| `rpc_methods`            | List available RPC methods |
| `chain_getBlockHash`     | Get block hash by height   |
| `chain_getBlock`         | Get block by hash          |
| `chain_getHeader`        | Get current header         |
| `chain_getFinalizedHead` | Get finalized block hash   |


### 4.2 Midnight-Specific RPC Methods


| Method                       | Description                  |
| ---------------------------- | ---------------------------- |
| `midnight_submitTransaction` | Submit signed transaction    |
| `midnight_contractState`     | Query contract state         |
| `midnight_jsonContractState` | Query contract state as JSON |
| `midnight_zswapState`        | Get ZSwap chain state        |
| `midnight_unclaimedAmount`   | Get unclaimed amounts        |
| `midnight_ledgerVersion`     | Get ledger version           |


### 4.3 Example RPC Call

```bash
# Query system chain
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"system_chain","params":[],"id":1}' \
  https://rpc.testnet-02.midnight.network/

# Response: {"jsonrpc":"2.0","result":"Midnight Testnet","id":1}

# List all available RPC methods
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"rpc_methods","params":[],"id":1}' \
  https://rpc.testnet-02.midnight.network/
```

---

## 5. Wallet SDK & DApp Connector

### 5.1 Wallet SDK Packages


| Package                                        | Purpose                                   |
| ---------------------------------------------- | ----------------------------------------- |
| `@midnight-ntwrk/wallet-sdk-facade`            | Unified API for all wallet operations     |
| `@midnight-ntwrk/wallet-sdk-unshielded-wallet` | Manages NIGHT (unshielded tokens)         |
| `@midnight-ntwrk/wallet-sdk-shielded`          | Manages shielded tokens with ZK proofs    |
| `@midnight-ntwrk/wallet-sdk-dust-wallet`       | Manages DUST for transaction fees         |
| `@midnight-ntwrk/wallet-sdk-hd`                | Hierarchical deterministic key derivation |
| `@midnight-ntwrk/wallet-sdk-address-format`    | Bech32m address encoding/decoding         |
| `@midnight-ntwrk/wallet-sdk-node-client`       | Communicates with Midnight nodes          |
| `@midnight-ntwrk/wallet-sdk-indexer-client`    | Queries the Midnight indexer              |
| `@midnight-ntwrk/wallet-sdk-prover-client`     | Interfaces with proving server            |


### 5.2 HD Key Derivation Path

```
m / 44' / 2400' / account' / role / index
```


| Role                | Value      | Purpose                  |
| ------------------- | ---------- | ------------------------ |
| `0` (NightExternal) | Unshielded | Public NIGHT operations  |
| `3` (Zswap)         | Shielded   | Private ZSwap operations |
| `4` (Dust)          | DUST       | DUST token operations    |


### 5.3 DApp Connector API

The DApp Connector API is available via `window.midnight.{walletId}` in browser wallets.

#### Initial API (before connection)


| Method/Property      | Description                             |
| -------------------- | --------------------------------------- |
| `name`               | Wallet name (display to user)           |
| `icon`               | Wallet icon URL (base64 or HTTP)        |
| `apiVersion`         | API version (semver, e.g., `3.1.5`)     |
| `connect(networkId)` | Connect to wallet, returns ConnectedAPI |


#### ConnectedAPI Methods


| Method                            | Description                           |
| --------------------------------- | ------------------------------------- |
| `getConfiguration()`              | Get indexer/RPC/prover URIs           |
| `getShieldedBalances()`           | Get shielded balances by token type   |
| `getUnshieldedBalances()`         | Get unshielded balances by token type |
| `getDustBalance()`                | Get DUST balance                      |
| `getShieldedAddresses()`          | Get shielded addresses                |
| `getUnshieldedAddress()`          | Get unshielded address                |
| `getDustAddress()`                | Get DUST address                      |
| `makeTransfer(outputs[])`         | Create transfer transaction           |
| `makeIntent(inputs[], outputs[])` | Create unbalanced intent (for swaps)  |
| `balanceSealedTransaction(tx)`    | Balance a sealed transaction          |
| `balanceUnsealedTransaction(tx)`  | Balance an unsealed transaction       |
| `submitTransaction(tx)`           | Submit transaction to network         |
| `getProvingProvider(config)`      | Get ZK proving provider               |


#### Configuration Return Type

```typescript
interface ServiceUriConfig {
  indexerUri: string;        // Indexer HTTP URI
  indexerWsUri: string;     // Indexer WebSocket URI
  proverServerUri: string;   // Proving server URI
  substrateNodeUri: string;  // Substrate node URI
  networkId: string;         // Network ID (mainnet, preprod, etc.)
}
```

---

## 6. How to Get Wallet Info from C Library

### 6.1 Three Approaches

#### Approach 1: Indexer GraphQL API (Recommended for read-only)

```
┌─────────────────────────────────────────────────────┐
│  C Library → IndexerClient → GraphQL HTTP POST       │
│                                                     │
│  ✓ No private key needed                            │
│  ✓ Works for public/unshielded data                 │
│  ✓ For shielded data: need viewing key              │
│  ✗ Cannot sign transactions                          │
└─────────────────────────────────────────────────────┘
```

**For Unshielded Wallet (Public):**

1. Query `unshieldedTransactions` subscription for address
2. Track created/spent UTXOs
3. Calculate balance locally

**For Shielded Wallet (Private):**

1. Call `connect(viewingKey)` mutation → get `sessionId`
2. Subscribe to `shieldedTransactions(sessionId)`
3. Track Merkle tree updates and relevant transactions
4. Calculate balance locally

#### Approach 2: Node RPC (For transaction submission)

```
┌─────────────────────────────────────────────────────┐
│  C Library → MidnightNodeRPC → JSON-RPC HTTP POST    │
│                                                     │
│  ✓ Submit signed transactions                        │
│  ✓ Query UTXOs, block state                         │
│  ✓ Query contract state                              │
│  ✗ Cannot get shielded balances directly             │
└─────────────────────────────────────────────────────┘
```

#### Approach 3: Wallet SDK via WASM/FFI (For full wallet functionality)

```
┌─────────────────────────────────────────────────────┐
│  C Library → WASM/FFI Bridge → TypeScript Wallet SDK │
│                                                     │
│  ✓ Full wallet functionality                         │
│  ✓ Sign transactions                                 │
│  ✓ Manage keys                                      │
│  ✓ Shielded + Unshielded + DUST                      │
│  ✗ More complex integration                         │
│  ✗ Requires proving server or WASM proving          │
└─────────────────────────────────────────────────────┘
```

### 6.2 Using IndexerClient (Your Existing Code)

Based on your existing `include/midnight/network/indexer_client.hpp`:

```cpp
#include "midnight/network/indexer_client.hpp"

// Initialize indexer client
midnight::network::IndexerClient indexer(
    "https://indexer.preprod.midnight.network/api/v4/graphql"
);

// Query unshielded wallet state
auto wallet_state = indexer.query_wallet_state("mn_addr_preprod1...");

// Check balance
std::cout << "NIGHT Balance: " << wallet_state.unshielded_balance << std::endl;
std::cout << "DUST Balance: " << wallet_state.dust_balance << std::endl;
std::cout << "UTXO Count: " << wallet_state.available_utxo_count << std::endl;
std::cout << "Synced: " << (wallet_state.synced ? "Yes" : "No") << std::endl;

// Query UTXOs
auto utxos = indexer.query_utxos("mn_addr_preprod1...");

// Query contract state
auto contract = indexer.query_contract_state("contract_address_hex");

// Query DUST registration status
auto dust_status = indexer.query_dust_status("mn_addr_preprod1...");

// Get block height
uint64_t height = indexer.get_block_height();

// Check if indexer is synced
bool synced = indexer.is_synced();
```

### 6.3 Using MidnightNodeRPC (For transaction submission)

```cpp
#include "midnight/network/midnight_node_rpc.hpp"

// Initialize RPC client
midnight::network::MidnightNodeRPC rpc(
    "https://rpc.preprod.midnight.network"
);

// Get UTXOs
auto utxos = rpc.get_utxos("mn_addr_preprod1...");

// Get protocol parameters
auto params = rpc.get_protocol_params();

// Get chain tip
auto [height, hash] = rpc.get_chain_tip();

// Submit transaction
std::string tx_id = rpc.submit_transaction("c42b...");

// Wait for confirmation
bool confirmed = rpc.wait_for_confirmation(tx_id, 10);

// Check node readiness
bool ready = rpc.is_ready();
```

### 6.4 Shielded Wallet via GraphQL (Viewing Key Required)

```cpp
// Step 1: Connect with viewing key to get session
std::string viewing_key = "mn_shield-esk_dev1...";  // Your viewing key

json connect_result = indexer.graphql_query(
    R"(
        mutation {
            connect(viewingKey: "$viewing_key") {
                sessionId
            }
        }
    )",
    {{"viewing_key", viewing_key}}
);
std::string session_id = connect_result["connect"]["sessionId"];

// Step 2: Subscribe to shielded transactions
// Note: This requires WebSocket, see indexer_subscription.hpp

// Step 3: Calculate balance from Merkle tree updates
// The wallet SDK handles this internally
```

---

## 7. Example Queries

### 7.1 Query Latest Block

```graphql
query {
  block {
    hash
    height
    protocolVersion
    timestamp
    author
    parent {
      hash
    }
    zswapMerkleTreeRoot
    ledgerParameters
    systemParameters {
      dParameter {
        numPermissionedCandidates
        numRegisteredCandidates
      }
      termsAndConditions {
        hash
        url
      }
    }
    transactions {
      id
      hash
    }
  }
}
```

### 7.2 Query Block by Height

```graphql
query {
  block(offset: { height: 1000 }) {
    hash
    height
    timestamp
    transactions {
      id
      hash
      transactionResult {
        status
      }
    }
  }
}
```

### 7.3 Query Transactions by Hash

```graphql
query {
  transactions(offset: { hash: "3031323..." }) {
    id
    hash
    protocolVersion
    zswapMerkleTreeRoot
    zswapStartIndex
    zswapEndIndex
    fee
    block {
      height
      hash
    }
    contractActions {
      __typename
      ... on ContractDeploy {
        address
        state
        unshieldedBalances { tokenType amount }
      }
      ... on ContractCall {
        address
        state
        entryPoint
        unshieldedBalances { tokenType amount }
      }
      ... on ContractUpdate {
        address
        state
        unshieldedBalances { tokenType amount }
      }
    }
    unshieldedCreatedOutputs {
      owner
      value
      tokenType
      intentHash
      outputIndex
      ctime
      registeredForDustGeneration
    }
    unshieldedSpentOutputs {
      owner
      value
      tokenType
    }
    zswapLedgerEvents {
      id
      raw
    }
    dustLedgerEvents {
      id
      __typename
    }
  }
}
```

### 7.4 Query Contract State

```graphql
query {
  contractAction(address: "3031323...") {
    __typename
    ... on ContractDeploy {
      address
      state
      zswapState
      unshieldedBalances { tokenType amount }
    }
    ... on ContractCall {
      address
      state
      zswapState
      entryPoint
      deploy { address }
      unshieldedBalances { tokenType amount }
    }
    ... on ContractUpdate {
      address
      state
      zswapState
      unshieldedBalances { tokenType amount }
    }
  }
}
```

### 7.5 Query DUST Generation Status

```graphql
query {
  dustGenerationStatus(
    cardanoRewardAddresses: [
      "stake_test1uqtgpdz0chm6jnxx7erfd7rhqfud7t4ajazx8es8xk8x3ts06psdv"
    ]
  ) {
    cardanoRewardAddress
    dustAddress
    registered
    nightBalance
    generationRate
    maxCapacity
    currentCapacity
    utxoTxHash
    utxoOutputIndex
  }
}
```

### 7.6 Query All DUST Registrations

```graphql
query {
  dustGenerations(
    cardanoRewardAddresses: ["stake_test1..."]
  ) {
    cardanoRewardAddress
    registrations {
      dustAddress
      valid
      nightBalance
      generationRate
      maxCapacity
      currentCapacity
      utxoTxHash
      utxoOutputIndex
    }
  }
}
```

### 7.7 Subscribe to New Blocks (WebSocket)

```json
{
  "id": "1",
  "type": "start",
  "payload": {
    "query": "subscription { blocks { hash height timestamp author parent { hash } transactions { id } } }"
  }
}
```

### 7.8 Subscribe to Unshielded Transactions (WebSocket)

```json
{
  "id": "2",
  "type": "start",
  "payload": {
    "query": "subscription { unshieldedTransactions(address: \"mn_addr_preprod1...\") { __typename ... on UnshieldedTransaction { transaction { hash block { height } } createdUtxos { owner value tokenType intentHash outputIndex } spentUtxos { owner value tokenType } } ... on UnshieldedTransactionsProgress { highestTransactionId } } }"
  }
}
```

### 7.9 Subscribe to Shielded Transactions (WebSocket)

```json
{
  "id": "3",
  "type": "start",
  "payload": {
    "query": "subscription { shieldedTransactions(sessionId: \"1CYq6ZsLmn\", index: 0) { __typename ... on RelevantTransaction { zswapCollapsedUpdate { startIndex endIndex update protocolVersion } transaction { id hash zswapStartIndex zswapEndIndex } } ... on ShieldedTransactionsProgress { highestZswapEndIndex highestCheckedZswapEndIndex highestRelevantZswapEndIndex } } }"
  }
}
```

### 7.10 Connect Wallet (Get Session ID)

```graphql
mutation {
  connect(
    viewingKey: "mn_shield-esk_dev1abcdef...",
    options: { startIndex: 0 }
  )
}
```

Response:

```json
{
  "data": {
    "connect": "sessionIdHere123456"
  }
}
```

### 7.11 Disconnect Session

```graphql
mutation {
  disconnect(sessionId: "sessionIdHere123456")
}
```

### 7.12 Query D-Parameter History

```graphql
query {
  dParameterHistory {
    blockHeight
    blockHash
    timestamp
    numPermissionedCandidates
    numRegisteredCandidates
  }
}
```

### 7.13 Query Terms and Conditions History

```graphql
query {
  termsAndConditionsHistory {
    blockHeight
    blockHash
    timestamp
    hash
    url
  }
}
```

### 7.14 Query ZSwap Merkle Tree Update

```graphql
query {
  zswapMerkleTreeCollapsedUpdate(startIndex: 1000, endIndex: 2000) {
    startIndex
    endIndex
    update
    protocolVersion
  }
}
```

### 7.15 Query Dust Commitment Merkle Tree

```graphql
query {
  dustCommitmentMerkleTreeUpdate(startIndex: 0, endIndex: 100) {
    startIndex
    endIndex
    update
    protocolVersion
  }
}
```

---

## 8. Type Reference

### 8.1 Block Type

```graphql
type Block {
  hash: HexEncoded!                    # Block hash
  height: Int!                         # Block height
  protocolVersion: Int!                # Protocol version
  timestamp: Int!                      # UNIX timestamp (seconds)
  author: HexEncoded                   # Block author (optional)
  zswapMerkleTreeRoot: HexEncoded!     # ZSwap Merkle tree root
  ledgerParameters: HexEncoded!         # Ledger parameters
  parent: Block                        # Parent block reference
  transactions: [Transaction!]!         # Transactions in block
  systemParameters: SystemParameters!  # Governance parameters
  dustCommitmentMerkleTreeRoot: HexEncoded  # Dust commitment root
  dustGenerationMerkleTreeRoot: HexEncoded   # Dust generation root
}
```

### 8.2 Transaction Types

#### Transaction Interface (common fields)

```graphql
interface Transaction {
  id: Int!                              # Transaction ID
  hash: HexEncoded!                     # Transaction hash
  protocolVersion: Int!                 # Protocol version
  raw: HexEncoded!                      # Raw serialized transaction
  block: Block!                         # Containing block
  contractActions: [ContractAction!]!    # Contract interactions
  unshieldedCreatedOutputs: [UnshieldedUtxo!]!  # Created UTXOs
  unshieldedSpentOutputs: [UnshieldedUtxo!]!     # Spent UTXOs
  zswapLedgerEvents: [ZswapLedgerEvent!]!        # ZSwap events
  dustLedgerEvents: [DustLedgerEvent!]!          # Dust events
}
```

#### RegularTransaction (user transactions)

```graphql
type RegularTransaction implements Transaction {
  id: Int!
  hash: HexEncoded!
  protocolVersion: Int!
  raw: HexEncoded!
  transactionResult: TransactionResult!   # Execution result
  identifiers: [HexEncoded!]!            # Transaction identifiers
  zswapMerkleTreeRoot: HexEncoded!
  zswapStartIndex: Int!                  # ZSwap state start index
  zswapEndIndex: Int!                   # ZSwap state end index (exclusive)
  dustCommitmentStartIndex: Int!
  dustCommitmentEndIndex: Int!
  dustGenerationStartIndex: Int!
  dustGenerationEndIndex: Int!
  fee: String!                          # Fee in SPECK
  # ... interface fields
}
```

#### SystemTransaction (protocol transactions)

```graphql
type SystemTransaction implements Transaction {
  # Similar to Transaction interface
  # No transactionResult (system transactions are always successful)
}
```

### 8.3 TransactionResult Type

```graphql
type TransactionResult {
  status: TransactionResultStatus!  # SUCCESS, PARTIAL_SUCCESS, FAILURE
  segments: [Segment!]             # Only for PARTIAL_SUCCESS
}

enum TransactionResultStatus {
  SUCCESS           # All segments succeeded
  PARTIAL_SUCCESS   # Some segments succeeded
  FAILURE          # All segments failed
}

type Segment {
  id: Int!       # Segment ID
  success: Boolean!  # Whether this segment succeeded
}
```

### 8.4 UnshieldedUtxo Type

```graphql
type UnshieldedUtxo {
  owner: UnshieldedAddress!                    # Owner Bech32m address
  tokenType: HexEncoded!                       # Token type identifier
  value: String!                              # UTXO value (u128 as string)
  intentHash: HexEncoded!                     # Intent hash
  outputIndex: Int!                           # Index in creating transaction
  ctime: Int                                  # Creation timestamp (seconds)
  initialNonce: HexEncoded!                   # For DUST generation tracking
  registeredForDustGeneration: Boolean!       # Whether registered for DUST
  createdAtTransaction: Transaction!          # Creating transaction
  spentAtTransaction: Transaction            # Spending transaction (null if unspent)
}
```

### 8.5 ContractAction Types

#### ContractAction Interface

```graphql
interface ContractAction {
  address: HexEncoded!              # Contract address
  state: HexEncoded!                # Contract state
  zswapState: HexEncoded!           # Contract ZSwap state
  transaction: Transaction!         # Containing transaction
  unshieldedBalances: [ContractBalance!]!  # Token balances
}
```

#### ContractBalance Type

```graphql
type ContractBalance {
  tokenType: HexEncoded!    # Token type identifier
  amount: String!           # Balance amount (u128 as string)
}
```

#### ContractDeploy

```graphql
type ContractDeploy implements ContractAction {
  address: HexEncoded!
  state: HexEncoded!
  zswapState: HexEncoded!
  transaction: Transaction!
  unshieldedBalances: [ContractBalance!]!  # Always empty (new deploy)
}
```

#### ContractCall

```graphql
type ContractCall implements ContractAction {
  address: HexEncoded!
  state: HexEncoded!
  zswapState: HexEncoded!
  entryPoint: String!              # Entry point name
  transaction: Transaction!
  deploy: ContractDeploy!         # Reference to deploy
  unshieldedBalances: [ContractBalance!]!
}
```

#### ContractUpdate

```graphql
type ContractUpdate implements ContractAction {
  address: HexEncoded!
  state: HexEncoded!
  zswapState: HexEncoded!
  transaction: Transaction!
  unshieldedBalances: [ContractBalance!]!
}
```

### 8.6 DustLedgerEvent Types

```graphql
interface DustLedgerEvent {
  id: Int!              # Event ID
  raw: HexEncoded!      # Raw serialized event
  maxId: Int!           # Maximum event ID
  protocolVersion: Int!
}

# Initial DUST UTXO creation
type DustInitialUtxo implements DustLedgerEvent {
  id: Int!
  raw: HexEncoded!
  maxId: Int!
  protocolVersion: Int!
  output: DustOutput!
}

# DUST generation decay time update
type DustGenerationDtimeUpdate implements DustLedgerEvent {
  id: Int!
  raw: HexEncoded!
  maxId: Int!
  protocolVersion: Int!
}

# DUST spend processed
type DustSpendProcessed implements DustLedgerEvent {
  id: Int!
  raw: HexEncoded!
  maxId: Int!
  protocolVersion: Int!
}

# DUST parameter change
type ParamChange implements DustLedgerEvent {
  id: Int!
  raw: HexEncoded!
  maxId: Int!
  protocolVersion: Int!
}

type DustOutput {
  nonce: HexEncoded!    # 32-byte nonce
}
```

### 8.7 DustGenerationStatus Type

```graphql
type DustGenerationStatus {
  cardanoRewardAddress: CardanoRewardAddress!  # Stake address
  dustAddress: DustAddress                    # Associated DUST address
  registered: Boolean!                         # Is registered
  nightBalance: String!                       # NIGHT balance in STAR
  generationRate: String!                     # SPECK per second
  maxCapacity: String!                        # Max DUST in SPECK
  currentCapacity: String!                    # Current DUST capacity in SPECK
  utxoTxHash: HexEncoded                      # UTXO tx hash for update
  utxoOutputIndex: Int                        # UTXO output index
}
```

### 8.8 DustGenerations Types (v4.1.0 new)

```graphql
type DustGenerations {
  cardanoRewardAddress: CardanoRewardAddress!
  registrations: [DustRegistration!]!
}

type DustRegistration {
  dustAddress: DustAddress!            # Bech32m DUST address
  valid: Boolean!                      # Is valid registration
  nightBalance: String!               # NIGHT balance in STAR
  generationRate: String!              # SPECK per second
  maxCapacity: String!               # Max DUST in SPECK
  currentCapacity: String!            # Current capacity in SPECK
  utxoTxHash: HexEncoded              # UTXO tx hash
  utxoOutputIndex: Int               # UTXO output index
}

# Subscription event
union DustGenerationsEvent = DustGenerationsItem | DustGenerationsProgress

type DustGenerationsItem {
  merkleIndex: Int!
  owner: HexEncoded!                  # Dust address
  value: String!                       # NIGHT value in STAR
  nonce: HexEncoded!
  ctime: Int!
  transactionId: Int!
  collapsedMerkleTree: MerkleTreeCollapsedUpdate
}

type DustGenerationsProgress {
  highestIndex: Int!
  collapsedMerkleTree: MerkleTreeCollapsedUpdate
}
```

### 8.9 ZswapLedgerEvent Type

```graphql
type ZswapLedgerEvent {
  id: Int!              # Event ID
  raw: HexEncoded!      # Raw serialized event
  maxId: Int!           # Maximum event ID
  protocolVersion: Int!
}
```

### 8.10 Merkle Tree Types

```graphql
type MerkleTreeCollapsedUpdate {
  startIndex: Int!         # Start index
  endIndex: Int!          # End index
  update: HexEncoded!      # Hex-encoded update
  protocolVersion: Int!
}

type CollapsedMerkleTree {
  startIndex: Int!
  endIndex: Int!
  update: HexEncoded!
  protocolVersion: Int!
}
```

### 8.11 Shielded Transactions Subscription

```graphql
union ShieldedTransactionsEvent = RelevantTransaction | ShieldedTransactionsProgress

type RelevantTransaction {
  transaction: RegularTransaction!
  zswapCollapsedUpdate: MerkleTreeCollapsedUpdate
  collapsedMerkleTree: CollapsedMerkleTree  # Deprecated
}

type ShieldedTransactionsProgress {
  highestZswapEndIndex: Int!              # Highest indexed index
  highestEndIndex: Int!                 # Deprecated
  highestCheckedZswapEndIndex: Int!     # Highest checked index
  highestCheckedEndIndex: Int!          # Deprecated
  highestRelevantZswapEndIndex: Int!    # Highest relevant index
  highestRelevantEndIndex: Int!         # Deprecated
}
```

### 8.12 Unshielded Transactions Subscription

```graphql
union UnshieldedTransactionsEvent = UnshieldedTransaction | UnshieldedTransactionsProgress

type UnshieldedTransaction {
  transaction: Transaction!
  createdUtxos: [UnshieldedUtxo!]!
  spentUtxos: [UnshieldedUtxo!]!
}

type UnshieldedTransactionsProgress {
  highestTransactionId: Int!
}
```

### 8.13 Nullifier Transactions (v4.1.0 new)

```graphql
# For dust nullifiers
type DustNullifierTransaction {
  nullifier: HexEncoded!      # Matched nullifier
  commitment: HexEncoded!    # Commitment
  transactionId: Int!        # Transaction ID
  blockHeight: Int!          # Block height
  blockHash: HexEncoded!     # Block hash
}

# For shielded (zswap) nullifiers
type ShieldedNullifierTransaction {
  transactionId: Int!
  blockHash: HexEncoded!
  blockHeight: Int!
  nullifier: HexEncoded!
}
```

### 8.14 Governance Types

```graphql
type SystemParameters {
  dParameter: DParameter!
  termsAndConditions: TermsAndConditions  # Null if not set
}

type DParameter {
  numPermissionedCandidates: Int!
  numRegisteredCandidates: Int!
}

type TermsAndConditions {
  hash: HexEncoded!
  url: String!
}

type DParameterChange {
  blockHeight: Int!
  blockHash: HexEncoded!
  timestamp: Int!
  numPermissionedCandidates: Int!
  numRegisteredCandidates: Int!
}

type TermsAndConditionsChange {
  blockHeight: Int!
  blockHash: HexEncoded!
  timestamp: Int!
  hash: HexEncoded!
  url: String!
}
```

### 8.15 SPO/Governance Types (v4.1.0 new)

```graphql
type CommitteeMember {
  epochNo: Int!
  position: Int!
  sidechainPubkeyHex: String!
  expectedSlots: Int!
  auraPubkeyHex: String
  poolIdHex: String
  spoSkHex: String
}

type EpochInfo {
  epochNo: Int!
  durationSeconds: Int!
  elapsedSeconds: Int!
}

type EpochPerf {
  epochNo: Int!
  spoSkHex: String!
  produced: Int!
  expected: Int!
  identityLabel: String
  stakeSnapshot: String
  poolIdHex: String
  validatorClass: String
}

type SpoIdentity {
  poolIdHex: String!
  mainchainPubkeyHex: String!
  sidechainPubkeyHex: String!
  auraPubkeyHex: String
  validatorClass: String!
}

type PoolMetadata {
  poolIdHex: String!
  hexId: String
  name: String
  ticker: String
  homepageUrl: String
  logoUrl: String
}

type Spo {
  poolIdHex: String!
  validatorClass: String!
  sidechainPubkeyHex: String!
  auraPubkeyHex: String
  name: String
  ticker: String
  homepageUrl: String
  logoUrl: String
}

type StakeShare {
  poolIdHex: String!
  name: String
  ticker: String
  homepageUrl: String
  logoUrl: String
  liveStake: String           # Lovelace
  activeStake: String         # Lovelace
  liveDelegators: Int
  liveSaturation: Float      # 0.0 to 1.0+
  declaredPledge: String      # Lovelace
  livePledge: String          # Lovelace
  stakeShare: Float           # Fraction of total stake
}
```

### 8.16 Mutation Types

```graphql
type Mutation {
  # Connect wallet with viewing key, returns session ID
  connect(
    viewingKey: ViewingKey!,
    options: ConnectOptions
  ): HexEncoded!
  
  # Disconnect wallet session
  disconnect(sessionId: HexEncoded!): Unit!
}

input ConnectOptions {
  startIndex: Int  # Transaction index to start searching (inclusive)
}
```

### 8.17 Subscription Types

```graphql
type Subscription {
  # Subscribe to new blocks
  blocks(offset: BlockOffset): Block!
  
  # Subscribe to contract actions
  contractActions(
    address: HexEncoded!,
    offset: BlockOffset
  ): ContractAction!
  
  # Subscribe to DUST generations (v4.1.0)
  dustGenerations(
    dustAddress: HexEncoded!,
    startIndex: Int!,
    endIndex: Int!
  ): DustGenerationsEvent!
  
  # Subscribe to DUST ledger events
  dustLedgerEvents(id: Int): DustLedgerEvent!
  
  # Subscribe to DUST nullifier matches (v4.1.0)
  dustNullifierTransactions(
    nullifierPrefixes: [HexEncoded!]!,
    fromBlock: Int,
    toBlock: Int
  ): DustNullifierTransaction!
  
  # Subscribe to shielded nullifier matches (v4.1.0)
  shieldedNullifierTransactions(
    nullifierPrefixes: [HexEncoded!]!,
    fromBlock: Int,
    toBlock: Int
  ): ShieldedNullifierTransaction!
  
  # Subscribe to shielded transactions
  shieldedTransactions(
    sessionId: HexEncoded!,
    index: Int
  ): ShieldedTransactionsEvent!
  
  # Subscribe to unshielded transactions
  unshieldedTransactions(
    address: UnshieldedAddress!,
    transactionId: Int
  ): UnshieldedTransactionsEvent!
  
  # Subscribe to ZSwap ledger events
  zswapLedgerEvents(id: Int): ZswapLedgerEvent!
}
```

---

## Appendix A: Query Limits

The indexer may apply limitations to queries:


| Limit Type   | Description              |
| ------------ | ------------------------ |
| `max-depth`  | Maximum nesting depth    |
| `max-fields` | Maximum fields per query |
| `timeout`    | Query timeout            |
| `complexity` | Query complexity cost    |


**Example error:**

```json
{
  "data": null,
  "errors": [
    {
      "message": "Query has too many fields: 20. Max fields: 10."
    }
  ]
}
```

---

## Appendix B: Authentication

- **Shielded transactions subscription**: Requires `sessionId` from `connect` mutation
- **Public queries**: No authentication required
- **Mutations**: Require valid viewing key for `connect`

---

## Appendix C: Network Identifiers


| Network    | ID           | Address Prefix                                                          |
| ---------- | ------------ | ----------------------------------------------------------------------- |
| Mainnet    | `mainnet`    | `mn_addr`, `mn_shield-addr`, `mn_dust`                                  |
| Preprod    | `preprod`    | `mn_addr_preprod`, `mn_shield-addr_preprod`, `mn_dust_preprod`          |
| Preview    | `preview`    | `mn_addr_preview`, `mn_shield-addr_preview`, `mn_dust_preview`          |
| Undeployed | `undeployed` | `mn_addr_undeployed`, `mn_shield-addr_undeployed`, `mn_dust_undeployed` |


