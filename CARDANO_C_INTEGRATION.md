# Cardano-C Integration Guide

## Overview

Midnight SDK now supports **real Cardano blockchain integration** using the [cardano-c](https://github.com/Biglup/cardano-c) library - a production-grade C library for interacting with the Cardano blockchain.

## Features

With cardano-c integration, Midnight SDK provides:

- ✅ **Transaction Building**: Construct complex Cardano transactions with multiple inputs/outputs
- ✅ **Address Generation**: Create payment addresses using BIP32 hierarchical deterministic key derivation
- ✅ **Signing & Verification**: Ed25519 cryptographic operations for transaction signing
- ✅ **UTXO Management**: Query and manage unspent transaction outputs
- ✅ **Multi-Asset Support**: Handle native tokens and NFTs
- ✅ **Staking Operations**: Support for staking certificates and delegation
- ✅ **Governance**: Conway era support including DRep registration and voting
- ✅ **CBOR Serialization**: CIP-116 JSON and CBOR format compatibility

## Building with Cardano-C

### Prerequisites

```bash
# Linux (Ubuntu/Debian)
sudo apt-get install cmake libsodium-dev libgmp-dev libssl-dev pkg-config

# macOS
brew install cmake libsodium gmp openssl pkg-config

# Windows (using vcpkg)
vcpkg install libsodium libgmp openssl
```

### Step 1: Clone and Build Cardano-C

```bash
# Clone cardano-c repository
git clone https://github.com/Biglup/cardano-c.git
cd cardano-c

# Configure CMake
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build and install
cmake --build . --config Release
sudo cmake --install .

# Verify installation
pkg-config --cflags --libs cardano
```

### Step 2: Build Midnight SDK with Cardano-C

```bash
cd d:\venera\midnight\night_fund
mkdir build && cd build

# Configure with cardano-c support
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_MQTT=ON \
      -DENABLE_COAP=ON \
      -DENABLE_HTTP=ON \
      -DENABLE_WEBSOCKET=ON \
      -DENABLE_BLOCKCHAIN=ON \
      ..

# Build
cmake --build . --config Release

# Install
sudo cmake --install .
```

## API Usage Examples

### Example 1: Create Wallet from Mnemonic

```cpp
#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/cardano_adapter.hpp"

midnight::blockchain::Wallet wallet;

// Create wallet from BIP39 mnemonic
std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                      "abandon abandon abandon abandon abandon about";

wallet.create_from_mnemonic(mnemonic, "passphrase");

// Get payment address
std::string address = wallet.get_address();
std::cout << "Address: " << address << std::endl;
```

### Example 2: Build and Sign Transaction

```cpp
#include "midnight/blockchain/cardano_adapter.hpp"
#include "midnight/blockchain/transaction.hpp"

// Initialize adapter
midnight::blockchain::CardanoAdapter adapter;
midnight::blockchain::CardanoProtocolParams params;
params.min_fee_a = 44;
params.min_fee_b = 155381;
params.utxo_cost_per_byte = 4310;

adapter.initialize("preprod", params);
adapter.connect("http://preprod-node.cardano-testnet.iohk.io:3001");

// Query UTXOs
auto utxos = adapter.query_utxos("addr_test_...");

// Prepare outputs
std::vector<std::pair<std::string, uint64_t>> outputs = {
    {"addr_test_...", 5000000}  // 5 ADA
};

// Build transaction
auto result = adapter.build_transaction_ex(utxos, outputs, "addr_test_...");

if (result.success) {
    // Sign transaction
    auto signed = adapter.sign_transaction(result.result, private_key);

    // Submit to network
    auto submit_result = adapter.submit_transaction(signed.result);

    if (submit_result.success) {
        std::cout << "TX ID: " << submit_result.result << std::endl;
    }
}
```

### Example 3: Transaction Construction

```cpp
#include "midnight/blockchain/transaction.hpp"

midnight::blockchain::Transaction tx;

// Add inputs (UTXOs to spend)
tx.add_input({
    "e9b59ff6a2b9fb5a1f8e1f2f3f4f5f6f7f8f9fafbfcfdfeff00010203040506",
    0
});

// Add outputs
tx.add_output({
    "addr_test_qzx9f5qq0nkvjk40emjrq69m0yhtxwwlc54p8upd60svsuw3jpjycg",
    5000000,
    {}  // No multi-assets
});

// Set validity period (2 hours)
uint64_t ttl = cardano_utils_get_time() + 7200;
tx.set_validity(ttl);

// Serialize to CBOR/JSON
std::string cbor_hex = tx.to_cbor_hex();
std::string json = tx.to_json();

std::cout << "CBOR: " << cbor_hex << std::endl;
std::cout << "Fee estimate: " << tx.calculate_min_fee() << " lovelace" << std::endl;
```

## Conditional Compilation

The SDK supports both with and without cardano-c:

```cpp
#ifdef ENABLE_CARDANO_REAL
    // Uses actual cardano-c library functions
    // Production-grade implementation
#else
    // Uses mock implementations
    // Useful for testing without dependencies
#endif
```

## Configuration Options

Pass these to CMake to control compilation:

```bash
cmake -DENABLE_CARDANO_REAL=ON \      # Enable cardano-c integration
      -DENABLE_MQTT=ON \             # Enable MQTT protocol
      -DENABLE_COAP=ON \             # Enable CoAP protocol
      -DENABLE_HTTP=ON \             # Enable HTTP/HTTPS
      -DENABLE_WEBSOCKET=ON \        # Enable WebSocket
      -DBUILD_TESTS=ON .             # Build unit tests
```

## Advanced Features

### Multi-Asset Transactions

```cpp
midnight::blockchain::Transaction::Output output;
output.address = "addr_test_...";
output.amount = 5000000;  // 5 ADA

// Add native tokens
output.assets["policy_id"]["asset_name"] = 100;

tx.add_output(output);
```

### Staking Operations

```cpp
midnight::blockchain::Transaction::Certificate cert;
cert.type = midnight::blockchain::Transaction::Certificate::Type::STAKE_ADDRESS_DELEGATION;
cert.stake_key_hash = "abc123...";
cert.pool_id = "pool1...";

tx.add_certificate(cert);
```

### Address Derivation (CIP1852)

```cpp
// Generate address at specific derivation path
std::string addr = wallet.generate_address(
    0,    // Account index
    0,    // Change (0=external, 1=internal)
    5     // Address index
);

// Explicitly derive from path
std::string custom = wallet.derive_key("m/1852'/1815'/0'/0/0");
```

## Memory Management

The cardano-c library uses reference counting:

```cpp
#ifdef ENABLE_CARDANO_REAL
// Objects auto-increment refcount on creation
// Must call *_unref() when done:
cardano_address_t* addr = ...;
// ... use addr ...
cardano_address_unref(&addr);
#endif
```

Midnight SDK's C++ wrappers handle this automatically using RAII principles.

## Testing

```bash
cd build

# Run unit tests
ctest

# Run specific tests
ctest -V  # Verbose output
```

## Troubleshooting

### cardano-c not found

```bash
# Ensure cardano-c is installed
pkg-config --cflags --libs cardano

# If not found, manually set CMake path
cmake -Dcardano_DIR=/path/to/cardano-c/lib/cmake/cardano ..
```

### Build errors with libsodium/libgmp

```bash
# macOS: ensure pkg-config can find them
pkg-config --cflags --libs libsodium libgmp

# Linux: install development headers
sudo apt-get install libsodium-dev libgmp-dev
```

### Runtime errors

Enable debug logging:

```cpp
midnight::g_logger->debug("Debug message here");
midnight::g_logger->trace("Trace level information");
```

## Network Configuration

### Preprod (Pre-Production Testnet)

```cpp
::midnight::blockchain::CardanoProtocolParams params;
params.min_fee_a = 44;
params.min_fee_b = 155381;

adapter.initialize("preprod", params);
adapter.connect("http://preprod-node.cardano-testnet.iohk.io:3001");
```

### Mainnet

```cpp
adapter.initialize("mainnet", params);
adapter.connect("https://cardano-mainnet-west.blockfrost.io");
```

## References

- [Cardano-C GitHub](https://github.com/Biglup/cardano-c)
- [Cardano-C Documentation](https://cardano-c.readthedocs.io/)
- [CIP-1852: Hierarchical Deterministic Wallets for Cardano](https://cips.cardano.org/cips/cip1852/)
- [CIP-116: JSON Serialization of Stake Pool Metadata](https://cips.cardano.org/cips/cip116/)
- [Cardano Developer Docs](https://developers.cardano.org/)

## License

Midnight SDK is licensed under the Apache License 2.0, compatible with cardano-c's Apache 2.0 license.
