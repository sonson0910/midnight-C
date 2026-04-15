# Midnight Blockchain - C++ SDK

## Overview

Midnight is a Layer-1 blockchain with comprehensive IoT support. This SDK provides a professional C++ interface for interacting with the Midnight network, inspired by the architecture patterns of production blockchains like Cardano.

## Architecture Patterns

The SDK follows best practices from mature blockchain libraries:

- **Modular Design**: Clear separation of concerns (crypto, transactions, addresses, encoding)
- **Transaction Builder Pattern**: Fluent interface for constructing complex transactions
- **Reference Counting**: Proper resource management for blockchain objects
- **Error Handling**: Comprehensive Result types for all operations
- **Type Safety**: Strong typing for blockchain primitives

## Core Components

### MidnightBlockchain

Main interface for blockchain operations:

```cpp
midnight::blockchain::ProtocolParams params;
params.min_fee_a = 44;
params.min_fee_b = 155381;

midnight::blockchain::MidnightBlockchain blockchain;
blockchain.initialize("preprod", params);
blockchain.connect("https://rpc.preprod.midnight.network");

// Query UTXOs
auto utxos = blockchain.query_utxos("midnight_address");

// Build transaction
auto result = blockchain.build_transaction(utxos, outputs, change_address);

// Sign & submit
auto signed = blockchain.sign_transaction(result.result, private_key);
auto submitted = blockchain.submit_transaction(signed.result);
```

### Wallet

HD wallet for managing private keys and addresses:

```cpp
midnight::blockchain::Wallet wallet;

// Create from mnemonic (BIP39)
wallet.create_from_mnemonic("word1 word2 ... word12", "optional_passphrase");

// Get addresses
std::string address = wallet.get_address();
auto addresses = wallet.get_addresses();

// Derive custom path (CIP1852-like)
std::string derived = wallet.derive_key("m/44'/0'/0'/0/0");

// Get balance
uint64_t balance = wallet.get_balance(address, &blockchain);
```

### Transaction

Represents a blockchain transaction:

```cpp
midnight::blockchain::Transaction tx;

// Add inputs (UTXOs to spend)
tx.add_input({
    "tx_hash_hex_string",
    0  // output index
});

// Add outputs
tx.add_output({
    "recipient_address",
    5000000,  // amount
    {}        // multi-assets
});

// Serialize
std::string cbor = tx.to_cbor_hex();
std::string json = tx.to_json();

// Calculate fee
uint64_t fee = tx.calculate_min_fee();
```

## Data Types

### UTXO (Unspent Transaction Output)

```cpp
midnight::blockchain::UTXO {
    std::string tx_hash;           // Previous transaction hash
    uint32_t output_index;         // Output index
    std::string address;           // Owner address
    uint64_t amount;               // Value in basic units
    std::map<...> assets;          // Multi-assets (optional)
};
```

### ProtocolParams

```cpp
midnight::blockchain::ProtocolParams {
    uint64_t min_fee_a;            // Base fee coefficient
    uint64_t min_fee_b;            // Per-byte fee coefficient
    uint64_t price_memory;         // Memory execution price
    uint64_t price_steps;          // CPU steps execution price
    uint64_t utxo_cost_per_byte;   // UTxO storage cost
    uint32_t max_tx_size;          // Max transaction size
    uint32_t max_block_size;       // Max block size
    uint64_t min_pool_cost;        // Minimum pool cost
    uint32_t coins_per_utxo_word;  // UTxO entry cost
};
```

### Result Type

All blocking operations return Result:

```cpp
struct Result {
    bool success;                  // Operation succeeded?
    std::string error_message;     // Error details
    std::string result;            // Operation result
};

auto result = blockchain.build_transaction(utxos, outputs, change_address);
if (result.success) {
    std::cout << "Built transaction: " << result.result << std::endl;
} else {
    std::cerr << "Error: " << result.error_message << std::endl;
}
```

## Usage Examples

### Example 1: Simple Transfer

```cpp
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/blockchain/wallet.hpp"

// Create wallet
midnight::blockchain::Wallet wallet;
wallet.create_from_mnemonic(mnemonic, "");

// Initialize blockchain
midnight::blockchain::ProtocolParams params = {...};
midnight::blockchain::MidnightBlockchain blockchain;
blockchain.initialize("preprod", params);
blockchain.connect("https://rpc.preprod.midnight.network");

// Query UTXOs
std::string address = wallet.get_address();
auto utxos = blockchain.query_utxos(address);

// Build transaction
std::vector<std::pair<std::string, uint64_t>> outputs = {
    {"destination_address", 2000000}
};

auto build_result = blockchain.build_transaction(utxos, outputs, address);

if (build_result.success) {
    // Sign
    auto sign_result = blockchain.sign_transaction(
        build_result.result,
        "private_key_hex"
    );

    if (sign_result.success) {
        // Submit
        auto submit_result = blockchain.submit_transaction(sign_result.result);
        if (submit_result.success) {
            std::cout << "TX ID: " << submit_result.result << std::endl;
        }
    }
}
```

### Example 2: Multi-Asset Transaction

```cpp
midnight::blockchain::Transaction::Output output;
output.address = "midnight_address";
output.amount = 5000000;

// Add custom assets
output.assets["policy_id_123"]["NFT_Name"] = 1;
output.assets["policy_id_456"]["Token_X"] = 100;

tx.add_output(output);
```

### Example 3: Wallet Derivation

```cpp
// Generate multiple addresses for hot/cold wallets
for (uint32_t i = 0; i < 10; ++i) {
    // External chain (receiving addresses)
    std::string external = wallet.generate_address(0, 0, i);

    // Internal chain (change addresses)
    std::string internal = wallet.generate_address(0, 1, i);

    std::cout << "Address " << i << ": " << external << std::endl;
}
```

## Building

```bash
# Configure
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_MQTT=ON \
      -DENABLE_HTTP=ON \
      -DENABLE_BLOCKCHAIN=ON ..

# Build
cmake --build . --config Release

# Install
sudo cmake --install .
```

## Network Configuration

### Testnet

```cpp
midnight::blockchain::ProtocolParams params;
params.min_fee_a = 44;
params.min_fee_b = 155381;

bloc.initialize("testnet", params);
blockchain.connect("http://testnet-node.midnight.network:5678");
```

### Stagenet

```cpp
blockchain.initialize("stagenet", params);
blockchain.connect("https://stagenet-node.midnight.network:5678");
```

### Mainnet

```cpp
blockchain.initialize("mainnet", params);
blockchain.connect("https://mainnet-node.midnight.network:5678");
```

## Design Philosophy

This SDK adopts patterns proven in production systems:

1. **Functional Composition**: Build complex operations from simple primitives
2. **Immutability**: Transactions are immutable once created
3. **Explicit Error Handling**: No exceptions, use Result types
4. **Clear Ownership**: No hidden state, predictable behavior
5. **Modular Testing**: Each component independently testable

## Integration with IoT

The Midnight SDK integrates seamlessly with the IoT protocol stack:

```cpp
// Combine IoT protocols with blockchain
midnight::protocols::mqtt::MqttClient mqtt;
mqtt.connect("mqtt.broker.local");
mqtt.subscribe("device/readings", [&blockchain](const auto& topic, const auto& payload) {
    // Process sensor data and record on blockchain
    // ...
});
```

## Security Considerations

- **Private Key Management**: Use hardware wallets when possible
- **Address Validation**: Always validate addresses before transactions
- **Fee Estimation**: Account for network congestion
- **Timeout Handling**: Set appropriate timeouts for network operations
- **Error Context**: Preserve error information for debugging

## Performance

- **Transaction Building**: Optimized for rapid prototyping
- **Memory Efficiency**: Reference counting prevents leaks
- **Async Operations**: Non-blocking I/O for network calls (future releases)
- **Batch Processing**: Support for bulk transaction submission

## License

Apache License 2.0
