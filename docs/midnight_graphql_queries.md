# Midnight Indexer v4.1.0 - Complete GraphQL Query Examples

> **File**: `docs/midnight_graphql_queries.md`
> **API Version**: v4.1.0 (2026-04-20)
> **Indexer Endpoint**: `POST https://<host>:<port>/api/v4/graphql`
> **WebSocket**: `wss://<host>:<port>/api/v4/graphql/ws`

---

## Table of Contents

1. [Scalar Types](#1-scalar-types)
2. [Input Types](#2-input-types)
3. [Query Examples](#3-query-examples)
4. [Mutation Examples](#4-mutation-examples)
5. [Subscription Examples](#5-subscription-examples)
6. [Complete Query Templates](#6-complete-query-templates)

---

## 1. Scalar Types

| Scalar | Description | Example |
|--------|-------------|---------|
| `HexEncoded` | Hex-encoded bytes | `"3031323..."` |
| `ViewingKey` | Wallet viewing key (Bech32m or hex) | `"mn_shield-esk_dev1..."` |
| `UnshieldedAddress` | Bech32m unshielded address | `"mn_addr_preprod1..."` |
| `CardanoRewardAddress` | Cardano stake address | `"stake_test1uqtgpdz0..."` |
| `DustAddress` | Bech32m DUST address | `"mn_dust_preprod1dz..."` |
| `Unit` | Empty return type | - |

---

## 2. Input Types

### 2.1 BlockOffset (oneOf - specify hash OR height)

```graphql
input BlockOffset @oneOf {
  hash: HexEncoded      # e.g., "0xabc123..."
  height: Int          # e.g., 12345
}
```

### 2.2 TransactionOffset (oneOf - specify hash OR identifier)

```graphql
input TransactionOffset @oneOf {
  hash: HexEncoded       # e.g., "0xdef456..."
  identifier: HexEncoded  # e.g., "0xghi789..."
}
```

### 2.3 ContractActionOffset (oneOf - specify block OR transaction)

```graphql
input ContractActionOffset @oneOf {
  blockOffset: BlockOffset
  transactionOffset: TransactionOffset
}
```

### 2.4 ConnectOptions

```graphql
input ConnectOptions {
  startIndex: Int  # Transaction index to start searching for relevant transactions (inclusive)
}
```

---

## 3. Query Examples

### 3.1 Get Latest Block (all fields)

```graphql
query {
  block {
    hash
    height
    protocolVersion
    timestamp
    author
    zswapMerkleTreeRoot
    ledgerParameters
    dustCommitmentMerkleTreeRoot
    dustGenerationMerkleTreeRoot
    parent {
      hash
      height
    }
    transactions {
      id
      hash
    }
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
  }
}
```

**Response Example:**
```json
{
  "data": {
    "block": {
      "hash": "0xabc123...",
      "height": 12345,
      "protocolVersion": 1,
      "timestamp": 1704067200,
      "author": "0xdef456...",
      "zswapMerkleTreeRoot": "0x789...",
      "ledgerParameters": "0x...",
      "dustCommitmentMerkleTreeRoot": "0x...",
      "dustGenerationMerkleTreeRoot": "0x...",
      "parent": {
        "hash": "0xparent...",
        "height": 12344
      },
      "transactions": [...],
      "systemParameters": {
        "dParameter": {
          "numPermissionedCandidates": 10,
          "numRegisteredCandidates": 50
        },
        "termsAndConditions": {
          "hash": "0x...",
          "url": "https://..."
        }
      }
    }
  }
}
```

### 3.2 Get Block by Height

```graphql
query {
  block(offset: { height: 1000 }) {
    hash
    height
    timestamp
    author
    transactionCount: transactions {
      id
    }
    systemParameters {
      dParameter {
        numPermissionedCandidates
        numRegisteredCandidates
      }
    }
  }
}
```

### 3.3 Get Block by Hash

```graphql
query {
  block(offset: { hash: "0xabc123def456..." }) {
    hash
    height
    timestamp
    transactions {
      id
      hash
    }
  }
}
```

### 3.4 Get Transactions by Hash

```graphql
query {
  transactions(offset: { hash: "0xtransactionhash..." }) {
    id
    hash
    protocolVersion
    raw
    zswapMerkleTreeRoot
    zswapStartIndex
    zswapEndIndex
    dustCommitmentStartIndex
    dustCommitmentEndIndex
    dustGenerationStartIndex
    dustGenerationEndIndex
    fee
    block {
      height
      hash
      timestamp
    }
    transactionResult {
      status
      segments {
        id
        success
      }
    }
    identifiers
    contractActions {
      __typename
      address
      state
      zswapState
      ... on ContractCall {
        entryPoint
        deploy {
          address
        }
      }
      unshieldedBalances {
        tokenType
        amount
      }
    }
    unshieldedCreatedOutputs {
      owner
      value
      tokenType
      intentHash
      outputIndex
      ctime
      initialNonce
      registeredForDustGeneration
    }
    unshieldedSpentOutputs {
      owner
      value
      tokenType
      intentHash
      outputIndex
    }
    zswapLedgerEvents {
      id
      raw
    }
    dustLedgerEvents {
      id
      __typename
      raw
    }
  }
}
```

### 3.5 Get Transactions by Identifier

```graphql
query {
  transactions(offset: { identifier: "0xtxidentifier..." }) {
    id
    hash
    block {
      height
      hash
    }
    unshieldedCreatedOutputs {
      owner
      value
      tokenType
    }
    unshieldedSpentOutputs {
      owner
      value
      tokenType
    }
  }
}
```

### 3.6 Get Contract Action (Latest)

```graphql
query {
  contractAction(address: "0xcontractaddress...") {
    __typename
    address
    state
    zswapState
    transaction {
      id
      hash
    }
    unshieldedBalances {
      tokenType
      amount
    }
  }
}
```

### 3.7 Get Contract Action by Block Height

```graphql
query {
  contractAction(
    address: "0xcontractaddress...",
    offset: { blockOffset: { height: 5000 } }
  ) {
    __typename
    address
    state
    zswapState
    transaction {
      id
      hash
    }
    unshieldedBalances {
      tokenType
      amount
    }
  }
}
```

### 3.8 Get Contract Action by Transaction Hash

```graphql
query {
  contractAction(
    address: "0xcontractaddress...",
    offset: { transactionOffset: { hash: "0xtxhash..." } }
  ) {
    __typename
    address
    state
    zswapState
    ... on ContractCall {
      entryPoint
      deploy {
        address
      }
    }
    unshieldedBalances {
      tokenType
      amount
    }
  }
}
```

### 3.9 Get DUST Generation Status

```graphql
query {
  dustGenerationStatus(
    cardanoRewardAddresses: [
      "stake_test1uqtgpdz0chm6jnxx7erfd7rhqfud7t4ajazx8es8xk8x3ts06psdv",
      "stake_test1ux3ts06psdvchm6jnxx7erfd7rhqfud7t4ajazx8es8xk8x3ts06..."
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

**Response Example:**
```json
{
  "data": {
    "dustGenerationStatus": [
      {
        "cardanoRewardAddress": "stake_test1uqtgpdz0...",
        "dustAddress": "mn_dust_preprod1dz...",
        "registered": true,
        "nightBalance": "1000000000",
        "generationRate": "8267",
        "maxCapacity": "5000000000",
        "currentCapacity": "2500000000",
        "utxoTxHash": "0x...",
        "utxoOutputIndex": 0
      }
    ]
  }
}
```

### 3.10 Get All DUST Registrations (v4.1.0)

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

### 3.11 Get ZSwap Merkle Tree Update (v4.1.0)

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

### 3.12 Get Dust Commitment Merkle Tree Update (v4.1.0)

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

### 3.13 Get D-Parameter History

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

### 3.14 Get Terms and Conditions History

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

### 3.15 Get SPO Identities (v4.1.0)

```graphql
query {
  spoIdentities(limit: 10, offset: 0) {
    poolIdHex
    mainchainPubkeyHex
    sidechainPubkeyHex
    auraPubkeyHex
    validatorClass
  }
}
```

### 3.16 Get SPO by Pool ID (v4.1.0)

```graphql
query {
  spoByPoolId(poolIdHex: "pool1abc123...") {
    poolIdHex
    validatorClass
    sidechainPubkeyHex
    auraPubkeyHex
    name
    ticker
    homepageUrl
    logoUrl
  }
}
```

### 3.17 Get Pool Metadata (v4.1.0)

```graphql
query {
  poolMetadata(poolIdHex: "pool1abc123...") {
    poolIdHex
    hexId
    name
    ticker
    homepageUrl
    logoUrl
  }
}
```

### 3.18 Get Stake Distribution (v4.1.0)

```graphql
query {
  stakeDistribution(limit: 20, offset: 0, orderByStakeDesc: true) {
    poolIdHex
    name
    ticker
    liveStake
    activeStake
    liveDelegators
    liveSaturation
    declaredPledge
    livePledge
    stakeShare
  }
}
```

### 3.19 Get Current Epoch Info (v4.1.0)

```graphql
query {
  currentEpochInfo {
    epochNo
    durationSeconds
    elapsedSeconds
  }
}
```

### 3.20 Get SPO Performance (v4.1.0)

```graphql
query {
  epochPerformance(epoch: 100, limit: 10, offset: 0) {
    epochNo
    spoSkHex
    produced
    expected
    identityLabel
    stakeSnapshot
    poolIdHex
    validatorClass
  }
}
```

### 3.21 Get Committee Members (v4.1.0)

```graphql
query {
  committee(epoch: 100) {
    epochNo
    position
    sidechainPubkeyHex
    expectedSlots
    auraPubkeyHex
    poolIdHex
    spoSkHex
  }
}
```

### 3.22 Get Registration Totals Series (v4.1.0)

```graphql
query {
  registeredTotalsSeries(fromEpoch: 50, toEpoch: 100) {
    epochNo
    totalRegistered
    newlyRegistered
  }
}
```

### 3.23 Get SPO Count (v4.1.0)

```graphql
query {
  spoCount
}
```

### 3.24 Get Stake Pool Operators (v4.1.0)

```graphql
query {
  stakePoolOperators(limit: 10)
}
```

---

## 4. Mutation Examples

### 4.1 Connect Wallet (Get Session ID)

```graphql
mutation {
  connect(viewingKey: "mn_shield-esk_dev1abcdef123456...") {
    sessionId
  }
}
```

**With options:**
```graphql
mutation {
  connect(
    viewingKey: "mn_shield-esk_dev1abcdef123456...",
    options: { startIndex: 0 }
  ) {
    sessionId
  }
}
```

**Response:**
```json
{
  "data": {
    "connect": "sessionId123456789abc..."
  }
}
```

### 4.2 Disconnect Session

```graphql
mutation {
  disconnect(sessionId: "sessionId123456789abc...") {
    success
  }
}
```

---

## 5. Subscription Examples

### 5.1 Subscribe to New Blocks

```json
{
  "id": "sub-1",
  "type": "start",
  "payload": {
    "query": "subscription { blocks { hash height timestamp author parent { hash } } }"
  }
}
```

### 5.2 Subscribe to Blocks Starting from Height

```json
{
  "id": "sub-2",
  "type": "start",
  "payload": {
    "query": "subscription { blocks(offset: { height: 1000 }) { hash height timestamp transactions { id hash } } }"
  }
}
```

### 5.3 Subscribe to Contract Actions

```json
{
  "id": "sub-3",
  "type": "start",
  "payload": {
    "query": "subscription { contractActions(address: \"0xcontractaddress...\") { __typename address state transaction { id hash } } }"
  }
}
```

### 5.4 Subscribe to Unshielded Transactions

```json
{
  "id": "sub-4",
  "type": "start",
  "payload": {
    "query": "subscription { unshieldedTransactions(address: \"mn_addr_preprod1...\") { __typename ... on UnshieldedTransaction { transaction { hash block { height } } createdUtxos { owner value tokenType intentHash outputIndex } spentUtxos { owner value tokenType } } ... on UnshieldedTransactionsProgress { highestTransactionId } } }"
  }
}
```

### 5.5 Subscribe to Unshielded Transactions from Specific ID

```json
{
  "id": "sub-5",
  "type": "start",
  "payload": {
    "query": "subscription { unshieldedTransactions(address: \"mn_addr_preprod1...\", transactionId: 1000) { __typename ... on UnshieldedTransaction { transaction { hash block { height } } createdUtxos { owner value tokenType } spentUtxos { owner value tokenType } } ... on UnshieldedTransactionsProgress { highestTransactionId } } }"
  }
}
```

### 5.6 Subscribe to Shielded Transactions

```json
{
  "id": "sub-6",
  "type": "start",
  "payload": {
    "query": "subscription { shieldedTransactions(sessionId: \"sessionId123...\", index: 0) { __typename ... on RelevantTransaction { zswapCollapsedUpdate { startIndex endIndex update protocolVersion } transaction { id hash zswapStartIndex zswapEndIndex block { height hash } } } ... on ShieldedTransactionsProgress { highestZswapEndIndex highestCheckedZswapEndIndex highestRelevantZswapEndIndex } } }"
  }
}
```

### 5.7 Subscribe to Shielded Transactions from Index

```json
{
  "id": "sub-7",
  "type": "start",
  "payload": {
    "query": "subscription { shieldedTransactions(sessionId: \"sessionId123...\", index: 500) { __typename ... on RelevantTransaction { zswapCollapsedUpdate { startIndex endIndex update } transaction { id hash } } ... on ShieldedTransactionsProgress { highestZswapEndIndex highestRelevantZswapEndIndex } } }"
  }
}
```

### 5.8 Subscribe to DUST Ledger Events

```json
{
  "id": "sub-8",
  "type": "start",
  "payload": {
    "query": "subscription { dustLedgerEvents { id __typename ... on DustInitialUtxo { output { nonce } } ... on DustGenerationDtimeUpdate { } ... on DustSpendProcessed { } ... on ParamChange { } raw maxId } }"
  }
}
```

### 5.9 Subscribe to DUST Ledger Events from ID

```json
{
  "id": "sub-9",
  "type": "start",
  "payload": {
    "query": "subscription { dustLedgerEvents(id: 100) { id __typename raw maxId } }"
  }
}
```

### 5.10 Subscribe to ZSwap Ledger Events

```json
{
  "id": "sub-10",
  "type": "start",
  "payload": {
    "query": "subscription { zswapLedgerEvents { id raw maxId protocolVersion } }"
  }
}
```

### 5.11 Subscribe to DUST Generations (v4.1.0)

```json
{
  "id": "sub-11",
  "type": "start",
  "payload": {
    "query": "subscription { dustGenerations(dustAddress: \"0xdustaddress...\", startIndex: 0, endIndex: 1000) { __typename ... on DustGenerationsItem { merkleIndex owner value nonce ctime transactionId collapsedMerkleTree { startIndex endIndex update } } ... on DustGenerationsProgress { highestIndex collapsedMerkleTree { startIndex endIndex update } } } }"
  }
}
```

### 5.12 Subscribe to DUST Nullifier Transactions (v4.1.0)

```json
{
  "id": "sub-12",
  "type": "start",
  "payload": {
    "query": "subscription { dustNullifierTransactions(nullifierPrefixes: [\"0xprefix1...\", \"0xprefix2...\"], fromBlock: 0, toBlock: 10000) { nullifier commitment transactionId blockHeight blockHash } }"
  }
}
```

### 5.13 Subscribe to Shielded Nullifier Transactions (v4.1.0)

```json
{
  "id": "sub-13",
  "type": "start",
  "payload": {
    "query": "subscription { shieldedNullifierTransactions(nullifierPrefixes: [\"0xshieldedprefix...\"], fromBlock: 0) { transactionId blockHash blockHeight nullifier } }"
  }
}
```

### 5.14 WebSocket Connection Init

```json
{
  "type": "connection_init",
  "payload": {
    "Authorization": "Bearer your-token-here"  // Optional
  }
}
```

### 5.15 WebSocket Ping/Pong

```json
{
  "type": "ping",
  "payload": {}
}
```

**Pong response:**
```json
{
  "type": "pong",
  "payload": {}
}
```

### 5.16 Stop Subscription

```json
{
  "id": "sub-1",
  "type": "stop"
}
```

---

## 6. Complete Query Templates

### 6.1 Full Block Query with All Details

```graphql
query FullBlockQuery($height: Int!) {
  block(offset: { height: $height }) {
    hash
    height
    protocolVersion
    timestamp
    author
    zswapMerkleTreeRoot
    ledgerParameters
    dustCommitmentMerkleTreeRoot
    dustGenerationMerkleTreeRoot
    parent {
      hash
      height
      timestamp
    }
    transactions {
      id
      hash
      protocolVersion
      raw
      zswapMerkleTreeRoot
      zswapStartIndex
      zswapEndIndex
      dustCommitmentStartIndex
      dustCommitmentEndIndex
      dustGenerationStartIndex
      dustGenerationEndIndex
      fee
      transactionResult {
        status
        segments {
          id
          success
        }
      }
      contractActions {
        __typename
        address
        state
        zswapState
        ... on ContractCall {
          entryPoint
          deploy { address }
        }
        unshieldedBalances {
          tokenType
          amount
        }
      }
      unshieldedCreatedOutputs {
        owner
        value
        tokenType
        intentHash
        outputIndex
        ctime
        initialNonce
        registeredForDustGeneration
      }
      unshieldedSpentOutputs {
        owner
        value
        tokenType
        intentHash
        outputIndex
      }
      zswapLedgerEvents {
        id
        raw
      }
      dustLedgerEvents {
        id
        __typename
        raw
      }
    }
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
  }
}
```

### 6.2 Full Transaction Query

```graphql
query FullTransactionQuery($hash: HexEncoded!) {
  transactions(offset: { hash: $hash }) {
    id
    hash
    protocolVersion
    raw
    identifiers
    zswapMerkleTreeRoot
    zswapStartIndex
    zswapEndIndex
    dustCommitmentStartIndex
    dustCommitmentEndIndex
    dustGenerationStartIndex
    dustGenerationEndIndex
    fee
    transactionResult {
      status
      segments {
        id
        success
      }
    }
    block {
      hash
      height
      timestamp
      author
      systemParameters {
        dParameter {
          numPermissionedCandidates
          numRegisteredCandidates
        }
      }
    }
    contractActions {
      __typename
      address
      state
      zswapState
      ... on ContractDeploy {
        transaction { id }
      }
      ... on ContractCall {
        entryPoint
        deploy {
          address
          state
        }
      }
      ... on ContractUpdate {
        transaction { id }
      }
      unshieldedBalances {
        tokenType
        amount
      }
    }
    unshieldedCreatedOutputs {
      owner
      value
      tokenType
      intentHash
      outputIndex
      ctime
      initialNonce
      registeredForDustGeneration
      createdAtTransaction {
        id
        hash
      }
      spentAtTransaction {
        id
        hash
      }
    }
    unshieldedSpentOutputs {
      owner
      value
      tokenType
      intentHash
      outputIndex
      createdAtTransaction {
        id
        hash
      }
    }
    zswapLedgerEvents {
      id
      raw
    }
    dustLedgerEvents {
      id
      __typename
      raw
    }
  }
}
```

### 6.3 Complete Contract Query

```graphql
query CompleteContractQuery($address: HexEncoded!, $height: Int) {
  contractAction(
    address: $address,
    offset: { blockOffset: { height: $height } }
  ) {
    __typename
    address
    state
    zswapState
    transaction {
      id
      hash
      block {
        height
        hash
        timestamp
      }
    }
    ... on ContractDeploy {
      unshieldedBalances {
        tokenType
        amount
      }
    }
    ... on ContractCall {
      entryPoint
      deploy {
        address
        state
        zswapState
        transaction {
          id
          hash
        }
      }
      unshieldedBalances {
        tokenType
        amount
      }
    }
    ... on ContractUpdate {
      unshieldedBalances {
        tokenType
        amount
      }
    }
  }
}
```

### 6.4 Complete DUST Query

```graphql
query CompleteDustQuery($stakeAddr: CardanoRewardAddress!) {
  dustGenerationStatus(cardanoRewardAddresses: [$stakeAddr]) {
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
  dustGenerations(cardanoRewardAddresses: [$stakeAddr]) {
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

### 6.5 Complete SPO Governance Query (v4.1.0)

```graphql
query CompleteGovernanceQuery($poolId: String!, $epoch: Int!) {
  spoCompositeByPoolId(poolIdHex: $poolId) {
    identity {
      poolIdHex
      mainchainPubkeyHex
      sidechainPubkeyHex
      auraPubkeyHex
      validatorClass
    }
    metadata {
      poolIdHex
      name
      ticker
      homepageUrl
      logoUrl
    }
    performance {
      epochNo
      produced
      expected
      identityLabel
      stakeSnapshot
      validatorClass
    }
  }
  committee(epoch: $epoch) {
    epochNo
    position
    sidechainPubkeyHex
    expectedSlots
    auraPubkeyHex
    poolIdHex
  }
  epochPerformance(epoch: $epoch) {
    epochNo
    spoSkHex
    produced
    expected
    identityLabel
    stakeSnapshot
    poolIdHex
    validatorClass
  }
  epochUtilization(epoch: $epoch)
  currentEpochInfo {
    epochNo
    durationSeconds
    elapsedSeconds
  }
}
```

### 6.6 Variables for Queries

```json
{
  "height": 12345,
  "hash": "0xabc123...",
  "address": "0xcontractaddress...",
  "stakeAddr": "stake_test1uqtgpdz0chm6jnxx7erfd7rhqfud7t4ajazx8es8xk8x3ts06psdv",
  "poolId": "pool1abc123...",
  "epoch": 100
}
```

---

## Appendix: HTTP Request Format

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <token>" \
  -d '{
    "query": "query { block { hash height } }",
    "variables": {},
    "operationName": null
  }' \
  https://indexer.preprod.midnight.network/api/v4/graphql
```

## Appendix: WebSocket Protocol

The Midnight Indexer uses the [GraphQL over WebSocket protocol](https://github.com/enisdenjo/graphql-ws/blob/master/PROTOCOL.md):

1. `connection_init` - Initialize connection
2. `subscribe` - Start subscription
3. `next` - Receive data
4. `error` - Receive error
5. `complete` - Subscription complete
6. `ping` / `pong` - Keep-alive
