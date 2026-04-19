# Midnight Network Configuration & Endpoints

## Network Overview

Midnight Network uses a privacy-first architecture with the following networks:

### Production Networks

| Network | Purpose | Status | Endpoint |
|---------|---------|--------|----------|
| **Preprod** | Testnet for development & testing | Active | `https://preprod.midnight.network` |
| **Mainnet** | Production network | Active (Soon) | TBD |

### Development Networks

| Network | Purpose | Setup |
|---------|---------|-------|
| **Devnet** | Local development | `midnight-node start --devnet` |
| **Docker** | Containerized local | `docker-compose up` |

## Preprod (Testnet) Details

### Endpoint Configuration
```cpp
// Preprod Configuration
const std::string PREPROD_NODE_URL = "https://preprod.midnight.network/api";
const std::string PREPROD_FAUCET_URL = "https://faucet.preprod.midnight.network/";

// Network Parameters (Preprod)
midnight::blockchain::ProtocolParams preprod_params{
    .min_fee_a = 44,
    .min_fee_b = 155381,
    .utxo_cost_per_byte = 4310,
    .max_tx_size = 16384,
    .max_block_size = 65536,
    .max_value_size = 5000,
    .price_memory = 0.0577,
    .price_steps = 0.0000721
};
```

### Faucet & Test Funds
- **Faucet URL**: https://faucet.preprod.midnight.network/
- **Purpose**: Get test tokens for Preprod testing
- **Token Name**: MIDNIGHT (testnet coins)
- **No real value** - Preprod coins cannot be transferred to mainnet

### Connection Instructions
```cpp
// Example: Connect to Preprod
midnight::blockchain::MidnightBlockchain blockchain;
blockchain.initialize("preprod", preprod_params);

// Uses RPC endpoint for queries and submission
bool connected = blockchain.connect("https://preprod.midnight.network/api");

if (connected) {
    // Query UTXOs
    auto utxos = blockchain.query_utxos(your_address);

    // Submit transaction
    auto result = blockchain.submit_transaction(signed_tx);
}
```

## Node Architecture

Midnight nodes are built on **Polkadot SDK** with:

### Core Features
- **Block Time**: 6 seconds
- **Session Length**: 1200 slots
- **Hash Function**: blake2_256
- **Account Type**: sr25519 public key
- **Consensus**: AURA + Grandpa

### Signature Schemes
- **sr25519** - Block authorship (based on Schnorrkel/Ristretto)
- **ed25519** - Finality-related messages
- **ECDSA** - Partnerchain consensus messages

### Initial Testnet Configuration
- **12 trusted permissioned nodes** operated by Shielded
- **Community-operated registered nodes** (unlimited)
- **Initial supply**: 100,000,000,000,000,000 units (testnet only)

## RPC API Methods

### Implemented in MidnightNodeRPC

```cpp
// Query Methods
std::vector<UTXO> get_utxos(const std::string& address);
uint64_t get_balance(const std::string& address);
ProtocolParams get_protocol_params();
std::pair<uint64_t, std::string> get_chain_tip();

// Transaction Methods
std::string submit_transaction(const std::string& tx_hex);
json get_transaction(const std::string& tx_id);
bool wait_for_confirmation(const std::string& tx_id, uint32_t max_blocks = 10);

// Node Methods
json get_node_info();
bool is_ready();
```

### Midnight Node RPC Format

All requests use **JSON-RPC 2.0** format:

```json
{
  "jsonrpc": "2.0",
  "method": "getUTXOs",
  "params": {
    "address": "mn_addr_preprod1..."
  },
  "id": 1
}
```

**Response Format**:
```json
{
  "jsonrpc": "2.0",
  "result": [
    {
      "txHash": "abc123...",
      "outputIndex": 0,
      "amount": 5000000,
      "address": "mn_addr_preprod1..."
    }
  ],
  "id": 1
}
```

## GraphQL Indexer (Optional)

Midnight also provides **GraphQL endpoints** for more advanced queries:

```graphql
query {
  utxos(owner: "addr_test_...") {
    txId
    outputIndex
    owner
    amount
    commitment
    status
  }
}
```

### Indexer URLs (Future)
- **Preprod Explorer**: https://preprod-explorer.midnight.network/graphql
- **Mainnet Explorer**: https://explorer.midnight.network/graphql

## Midnight Block Structure

### Block Header
- **Version**: Protocol version
- **Height**: Block number
- **Timestamp**: Block creation time
- **Merkle Root**: Transactions root hash
- **Nonce**: Mining/consensus nonce
- **Parent Hash**: Previous block hash

### Block Content
- **Transactions**: List of validated transactions
- **Certificates**: Staking/governance certificates
- **Extrinsics**: On-chain events

## Transaction Model

### UTXO Structure
```cpp
struct UTXO {
    std::string tx_hash;              // 64-char hex string
    uint32_t output_index;            // Output position in tx
    std::string address;              // Owner address
    uint64_t amount;                  // Amount in base units
    std::map<std::string, uint64_t> multi_assets;  // Additional assets
};
```

### Transaction Fields
- **Inputs**: UTXOs being spent
- **Outputs**: New UTXOs being created
- **Certificates**: Staking operations
- **Metadata**: Optional transaction metadata
- **Validity Window**: (from_slot, to_slot) - when tx is valid
- **Fee**: Calculated as: `min_fee_a * size + min_fee_b`

### Privacy Features
- **Commitments**: Hide actual amounts
- **ZK Proofs**: Verify without revealing data
- **Selective Disclosure**: Choose what to reveal

## Network Parameters Cache

Protocol parameters are fetched from the node on connection:

```cpp
// Auto-fetched from node if not pre-initialized
blockchain.connect(node_url);

// Or pre-set before connection
midnight::blockchain::ProtocolParams params{...};
blockchain.initialize("preprod", params);
blockchain.connect(node_url);
```

### Fee Calculation
```cpp
uint64_t fee = min_fee_a * tx_size_bytes + min_fee_b;
// Example: fee = 44 * 500 + 155381 = 177381 units
```

## Wallet Integration

### Address Format
- **Bech32m Format**: `mn_addr_preprod1...` (Preprod/Testnet)
- **Encoding**: Base32 with checksum
- **Validation**: Checksum verification

### Key Derivation
- **BIP39**: Mnemonic seed phrases
- **CIP1852**: Multi-account hierarchical derivation
- **Path Format**: `m/1852'/1'/account'/change/address_index`

## Testing & Development

### Local Setup
```bash
# Run Midnight node locally
docker-compose up midnight-node

# Connect SDK to local node
midnight::blockchain::MidnightBlockchain blockchain;
blockchain.connect("http://localhost:5678");
```

### Preprod Testing
```bash
# Get test coins from faucet
# https://faucet.preprod.midnight.network/

# Connect to Preprod
blockchain.connect("https://preprod.midnight.network/api");

# All transactions are permanent on testnet
# Cannot be reverted
```

## Common Issues & Solutions

### Issue: Connection Refused
```
Error: Cannot connect to https://preprod.midnight.network/api
```
**Solution**:
- Check network connectivity
- Verify node is running (if local)
- Use correct testnet endpoint

### Issue: Insufficient Funds
```
Error: Not enough UTXOs to cover transaction
```
**Solution**:
- Query UTXOs to verify balance
- Request more tokens from faucet
- Confirm UTXO selection includes necessary amounts

### Issue: Transaction Rejected
```
Error: Transaction validation failed
```
**Solution**:
- Verify inputs are valid UTXOs
- Check fee is sufficient
- Ensure validity window is correct

## References

- **Official Docs**: https://docs.midnight.network/
- **GitHub Repos**: https://github.com/midnightntwrk/
- **Discord**: https://discord.com/invite/midnightnetwork
- **Faucet**: https://faucet.preprod.midnight.network/
- **Explorer**: https://scan.midnight.network/ (when available)
