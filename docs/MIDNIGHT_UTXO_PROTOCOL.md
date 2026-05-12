# Midnight UTXO Transaction Protocol Documentation

## Overview
File: `src/utxo-transactions/utxo_transactions.cpp`
Reference: `rustnight/crates/midnight-blockchain/src/transaction.rs`

## Transaction Format

### CBOR Encoding Structure

Transaction body được encode bằng CBOR với cấu trúc:
```
[
  version (4 bytes, big-endian u32),
  nonce (compact-encoded u64),
  inputs (array),
  outputs (array),
  fees (compact-encoded u64),
  witnesses (array, optional),
  proof_references (array, optional)
]
```

### Input Format
```
[
  outpoint_hash (byte string - 32 bytes),
  output_index (compact-encoded u32),
  amount_commitment (byte string - 32 bytes, hex-encoded)
]
```

### Output Format
```
[
  address (text string - Bech32m UTF-8),
  amount_commitment (byte string - 32 bytes, hex-encoded),
  lock_height (compact-encoded u64)
]
```

### Compact Encoding (Unsigned Integer)
| Range | Encoding |
|-------|----------|
| 0-63 | `val << 2` |
| 64-16383 | 2 bytes: `(val << 2) | 0x01` |
| 16384-1073741823 | 4 bytes: `(val << 2) | 0x02` |
| >1073741823 | Variable: `((bytes-4) << 2) | 0x03` + raw bytes |

### Array Encoding
| Count | Encoding |
|-------|----------|
| 0-23 | `0x80 + count` (major type 4) |
| 24-255 | `0x98` + 1-byte length |
| >255 | `0x99` + 2-byte length |

## Hash Algorithm

**Blake2b-256** (NOT SHA256)

```cpp
uint8_t hash[32];
crypto_generichash(hash, 32, tx_bytes.data(), tx_bytes.size(), nullptr, 0);
```

## Address Format

- **Bech32m** format (UTF-8 strings)
- Prefix: `midnight1`
- NOT hex-encoded

## Key Types

### UtxoInput
```cpp
struct UtxoInput {
    std::string outpoint;      // tx_hash:output_index format
    uint32_t output_index;      // VLA index
    std::string address;       // Bech32m address
    std::string amount_commitment; // "0x" + 64 hex chars
};
```

### UtxoOutput
```cpp
struct UtxoOutput {
    std::string address;           // Bech32m address
    std::string amount_commitment; // "0x" + 64 hex chars
    uint64_t lock_height;         // Finality depth
};
```

### Transaction
```cpp
struct Transaction {
    uint32_t version;
    uint64_t nonce;
    uint64_t timestamp;
    uint64_t fees;
    std::string hash;
    std::string signature;
    std::vector<UtxoInput> inputs;
    std::vector<UtxoOutput> outputs;
    WitnessBundle witness_bundle;
};
```

## Security Fixes Applied

### 1. Double-Spend Prevention (BUG FIXED)
**Issue**: Transaction builder có thể thêm duplicate inputs

**Fix**: Thêm `has_duplicate_input()` kiểm tra trùng lặp:
```cpp
bool TransactionBuilder::has_duplicate_input(const UtxoInput &input) const {
    std::string input_outpoint = midnight::util::strip_hex_prefix(input.outpoint);
    uint32_t input_index = input.output_index;
    for (const auto &existing : tx_.inputs) {
        std::string existing_outpoint = midnight::util::strip_hex_prefix(existing.outpoint);
        if (existing_outpoint == input_outpoint && existing.output_index == input_index) {
            return true;
        }
    }
    return false;
}
```

**Location**: Lines 649-666

### 2. UTXO Existence Verification (BUG FIXED)
**Issue**: Không verify UTXO tồn tại và chưa spent trước khi thêm input

**Fix**: Thêm `verify_input_utxo()` trong `add_input()`:
```cpp
void TransactionBuilder::add_input(const UtxoInput &input) {
    if (has_duplicate_input(input)) {
        throw std::invalid_argument("Duplicate input rejected");
    }
    if (utxo_set_manager_) {
        if (!verify_input_utxo(input)) {
            throw std::invalid_argument("Invalid input: UTXO not found or already spent");
        }
    }
    tx_.inputs.push_back(input);
}
```

**Location**: Lines 622-647

### 3. is_spent Return Type Fix (BUG FIXED)
**Issue**: `is_spent()` trả `true` cho UTXO không tồn tại

**Fix**: Đổi sang `std::optional<bool>`:
```cpp
std::optional<bool> UtxoSetManager::is_spent(const std::string &tx_hash, uint32_t output_index) {
    auto utxo = query_utxo(tx_hash, output_index);
    if (!utxo) {
        return std::nullopt; // Distinguish from "is spent"
    }
    return utxo->is_spent;
}
```

**Location**: Lines 371-381

### 4. Race Condition Prevention (BUG FIXED)
**Issue**: Query UTXO nhưng không reserve, có thể bị double-spend

**Fix**: Thêm reservation mechanism với TTL:
```cpp
bool UtxoSetManager::reserve_utxos(const std::vector<UtxoInput> &inputs,
                                    const std::string &tx_hash,
                                    uint64_t ttl_ms) {
    uint64_t now = get_current_time_ms();
    uint64_t expiry = now + ttl_ms;
    for (const auto &input : inputs) {
        std::string outpoint_key = input.outpoint + ":" + std::to_string(input.output_index);
        if (is_reserved(outpoint_key, tx_hash)) {
            return false; // Already reserved
        }
        reserved_utxos_[outpoint_key] = {tx_hash, expiry};
    }
    return true;
}

bool UtxoSetManager::is_reserved(const std::string &outpoint, 
                                   const std::string &exclude_tx_hash) const {
    auto it = reserved_utxos_.find(outpoint);
    if (it == reserved_utxos_.end()) return false;
    
    uint64_t now = get_current_time_ms();
    if (now > it->second.second) {
        const_cast<UtxoSetManager*>(this)->reserved_utxos_.erase(it);
        return false; // Expired
    }
    
    if (!exclude_tx_hash.empty() && it->second.first == exclude_tx_hash) {
        return false; // Reserved by this tx
    }
    return true;
}
```

**Location**: Lines 432-515

### 5. CBOR Deserialization Fix (BUG FIXED)
**Issue**: `skip()` dùng hardcoded approximation `count * 10` bytes - không chính xác

**Fix**: Thêm `skip_cbor_element()` method đệ quy để skip đúng CBOR element:
```cpp
// Skip a single CBOR element recursively (any type)
size_t skip_cbor_element() {
    uint8_t byte = next_byte();
    MajorType mt = get_major_type(byte);
    // ... recursively skip based on type
}
```

**Location**: `include/midnight/tx/midnight_transaction.hpp`

### 6. Thread-Safety Fix (BUG FIXED)
**Issue**: `FacadeState` không có mutex protection - race condition khi multi-thread access

**Fix**: Thêm mutex protection cho `WalletFacade`:
```cpp
mutable std::mutex state_mutex_;  // Protects state_ for thread-safe access
mutable std::mutex pending_mutex_;  // Protects pending operations

void set_available_coins(const std::vector<UtxoWithMeta>& coins) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.unshielded.available_coins = coins;
}
```

**Location**: `include/midnight/wallet/wallet_facade.hpp`

### 7. SR25519 Verification Note
**Status**: Verified - NOT a bug

Code SR25519 verification đúng logic:
```cpp
// Returns 0 if valid, -1 if invalid
if (crypto_core_ristretto255_is_valid_point(parsed_public_key.data()) != 0) {
    return false;  // Invalid point
}
bool result = midnight::crypto::Sr25519Signer::verify(...);
return result;  // Returns true only if signature is valid
```

**Note**: libsodium sử dụng Ristretto255 simulation thay vì SR25519 native. Đây là development fallback, không phải bug.

## Additional Validators

### TransactionValidator
- `validate_inputs()` - Kiểm tra duplicate, existence, spent status, reservation
- `validate_outputs()` - Kiểm tra định dạng outputs
- `validate_dust_accounting()` - Kiểm tra balance
- `validate_proofs()` - Kiểm tra proofs format
- `validate_signature()` - Kiểm tra signature format

### Fee Estimation
```cpp
uint64_t FeeEstimator::estimate_fee(uint32_t input_count, 
                                    uint32_t output_count,
                                    size_t proof_size,
                                    const TransactionBuilderContext &context) {
    uint64_t base = input_count * context.protocol_params.min_fee_a + 
                     output_count * context.protocol_params.min_fee_b;
    uint64_t proof_fee = proof_size * context.protocol_params.utxo_cost_per_byte;
    return base + proof_fee;
}
```

## API Reference

### UtxoSetManager
```cpp
std::optional<Utxo> query_utxo(const std::string &tx_hash, uint32_t output_index);
std::optional<std::vector<Utxo>> query_unspent_utxos(const std::string &address);
std::optional<bool> is_spent(const std::string &tx_hash, uint32_t output_index);
bool verify_inputs_unspent(const std::vector<UtxoInput> &inputs);
bool reserve_utxos(const std::vector<UtxoInput> &inputs, const std::string &tx_hash, uint64_t ttl_ms);
void release_reserved_utxos(const std::string &tx_hash);
```

### TransactionBuilder
```cpp
TransactionBuilder();
TransactionBuilder(const TransactionBuilderContext &context);
TransactionBuilder(const TransactionBuilderContext &context, std::shared_ptr<UtxoSetManager> utxo_set_manager);
void add_input(const UtxoInput &input);
void add_output(const UtxoOutput &output);
void set_fees(uint64_t fee_amount);
void add_signature(const std::string &signature);
std::optional<Transaction> build();
bool validate();
```

## Constants
```cpp
constexpr size_t COMMITMENT_HEX_SIZE = 66;  // "0x" + 64 hex chars
constexpr uint32_t kIndexerRpcTimeoutMs = 8000;
constexpr size_t kMaxTxInputs = 256;
constexpr size_t kMaxTxOutputs = 256;
```

## Notes
- Midnight uses Bech32m addresses (NOT hex)
- Transaction hash uses Blake2b-256 (NOT SHA256)
- All amounts are in lovelace (1 NIGHT = 1,000,000 lovelace)
- UTXO format: `tx_hash:output_index`
