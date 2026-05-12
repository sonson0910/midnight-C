/**
 * Midnight Transaction Serialization
 *
 * Implements proper CBOR/SCALE serialization for Midnight transactions
 * that matches the official JS SDK and Rust implementation.
 *
 * Key differences from Cardano:
 * - Midnight uses CBOR for transaction body hashing
 * - Ed25519 signatures for unshielded transactions
 * - ZK proofs (128 bytes) for shielded operations
 * - Commitment-based amounts (Pedersen commitments)
 *
 * Transaction Pipeline (matching ledger-v8):
 *   UnsignedTransaction -> SignedTransaction -> FinalizedTransaction
 *
 * Reference: rustnight/crates/midnight-blockchain/src/transaction.rs
 * Reference: rustnight/crates/midnight-blockchain/src/ledger_types.rs
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <array>
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace midnight::tx
{

    /**
     * @brief Transaction serialization errors
     */
    enum class SerializationError
    {
        InvalidData = 1,
        UnexpectedEnd,
        InvalidArrayHeader,
        InvalidMapHeader,
        InvalidUtf8String,
        InvalidByteString,
        InvalidInteger,
        InvalidCompactInteger,
        InvalidNull,
        InvalidBoolean,
        UnsupportedType,
        BufferOverflow,
        InvalidFieldCount,
        InvalidWitness,
        InvalidOutput,
        InvalidInput,
        InvalidTokenType,
    };

    class SerializationException : public std::runtime_error
    {
    public:
        SerializationError code;
        size_t offset;

        SerializationException(SerializationError code, size_t offset, const std::string &msg)
            : std::runtime_error(msg), code(code), offset(offset)
        {
        }

        static SerializationException invalid_data(size_t offset, const std::string &msg)
        {
            return SerializationException(SerializationError::InvalidData, offset, msg);
        }

        static SerializationException unexpected_end(size_t offset)
        {
            return SerializationException(SerializationError::UnexpectedEnd, offset, "Unexpected end of data");
        }

        static SerializationException invalid_utf8_string(size_t offset)
        {
            return SerializationException(SerializationError::InvalidUtf8String, offset, "Invalid UTF-8 string");
        }

        static SerializationException invalid_compact_int(size_t offset)
        {
            return SerializationException(SerializationError::InvalidCompactInteger, offset, "Invalid compact integer encoding");
        }
    };

    /**
     * @brief CBOR deserializer for Midnight transactions
     *
     * Provides efficient streaming deserialization matching the Rust CBOR format.
     */
    class CborDeserializer
    {
    public:
        explicit CborDeserializer(const std::vector<uint8_t> &data)
            : data_(data), pos_(0)
        {
        }

        bool has_data() const { return pos_ < data_.size(); }
        size_t remaining() const { return data_.size() - pos_; }
        size_t position() const { return pos_; }

        // Get next byte or throw
        uint8_t next_byte()
        {
            if (pos_ >= data_.size())
            {
                throw SerializationException::unexpected_end(pos_);
            }
            return data_[pos_++];
        }

        // Peek at next byte without consuming
        uint8_t peek_byte() const
        {
            if (pos_ >= data_.size())
            {
                throw SerializationException::unexpected_end(pos_);
            }
            return data_[pos_];
        }

        // Skip n bytes
        void skip(size_t n)
        {
            if (pos_ + n > data_.size())
            {
                throw SerializationException::unexpected_end(pos_);
            }
            pos_ += n;
        }

        // Get remaining bytes
        std::vector<uint8_t> remaining_bytes() const
        {
            return std::vector<uint8_t>(data_.begin() + pos_, data_.end());
        }

        // Parse CBOR major type
        enum class MajorType : uint8_t
        {
            Uint = 0,
            NegInt = 1,
            ByteString = 2,
            Utf8String = 3,
            Array = 4,
            Map = 5,
            Tag = 6,
            Simple = 7,
        };

        MajorType get_major_type(uint8_t byte) const
        {
            return static_cast<MajorType>((byte >> 5) & 0x07);
        }

        uint8_t get_additional_info(uint8_t byte) const
        {
            return byte & 0x1F;
        }

        // Parse array header, return element count
        size_t parse_array_header()
        {
            uint8_t byte = next_byte();
            MajorType mt = get_major_type(byte);
            if (mt != MajorType::Array)
            {
                throw SerializationException::invalid_data(pos_ - 1,
                    "Expected array, got major type " + std::to_string(static_cast<int>(mt)));
            }
            return decode_array_count(byte);
        }

        // Parse map header, return pair count
        size_t parse_map_header()
        {
            uint8_t byte = next_byte();
            MajorType mt = get_major_type(byte);
            if (mt != MajorType::Map)
            {
                throw SerializationException::invalid_data(pos_ - 1,
                    "Expected map, got major type " + std::to_string(static_cast<int>(mt)));
            }
            return decode_array_count(byte);
        }

        size_t decode_array_count(uint8_t byte)
        {
            uint8_t ai = get_additional_info(byte);
            if (ai < 24)
            {
                return ai;
            }
            switch (ai)
            {
            case 24: return next_byte();
            case 25: return (static_cast<size_t>(next_byte()) << 8) | next_byte();
            case 26: {
                uint32_t val = 0;
                val = (val << 8) | next_byte();
                val = (val << 8) | next_byte();
                val = (val << 8) | next_byte();
                return val;
            }
            case 27: {
                uint64_t val = 0;
                for (int i = 0; i < 8; i++)
                {
                    val = (val << 8) | next_byte();
                }
                return val;
            }
            default:
                throw SerializationException::invalid_data(pos_ - 1, "Invalid array size encoding");
            }
        }

        // Parse compact-encoded u64 (SCALE-like encoding)
        uint64_t parse_compact_u64()
        {
            uint8_t first = next_byte();
            uint8_t mode = first & 0x03;
            uint64_t value = first >> 2;

            switch (mode)
            {
            case 0: // Single byte, value in bits 2-7
                return value;
            case 1: // Two bytes
            {
                uint8_t second = next_byte();
                value = (value << 8) | second;
                return value;
            }
            case 2: // Four bytes
            {
                uint8_t b1 = next_byte();
                uint8_t b2 = next_byte();
                uint8_t b3 = next_byte();
                value = (value << 24) | (static_cast<uint64_t>(b1) << 16) | (static_cast<uint64_t>(b2) << 8) | b3;
                return value;
            }
            case 3: // Big integer mode
            {
                size_t bytes_needed = value + 4;
                value = 0;
                for (size_t i = 0; i < bytes_needed; i++)
                {
                    value |= (static_cast<uint64_t>(next_byte()) << (i * 8));
                }
                return value;
            }
            default:
                throw SerializationException::invalid_compact_int(pos_ - 1);
            }
        }

        // Parse a UTF-8 string
        std::string parse_utf8_string()
        {
            uint8_t byte = next_byte();
            MajorType mt = get_major_type(byte);
            if (mt != MajorType::Utf8String)
            {
                throw SerializationException::invalid_data(pos_ - 1,
                    "Expected UTF-8 string, got major type " + std::to_string(static_cast<int>(mt)));
            }
            return decode_string_data(byte);
        }

        // Parse a byte string
        std::vector<uint8_t> parse_byte_string()
        {
            uint8_t byte = next_byte();
            MajorType mt = get_major_type(byte);
            if (mt != MajorType::ByteString)
            {
                throw SerializationException::invalid_data(pos_ - 1,
                    "Expected byte string, got major type " + std::to_string(static_cast<int>(mt)));
            }
            return decode_byte_string_data(byte);
        }

        std::string decode_string_data(uint8_t byte)
        {
            size_t len = decode_length(byte);
            if (pos_ + len > data_.size())
            {
                throw SerializationException::unexpected_end(pos_);
            }
            std::string result(reinterpret_cast<const char*>(&data_[pos_]), len);
            pos_ += len;
            return result;
        }

        std::vector<uint8_t> decode_byte_string_data(uint8_t byte)
        {
            size_t len = decode_length(byte);
            if (pos_ + len > data_.size())
            {
                throw SerializationException::unexpected_end(pos_);
            }
            std::vector<uint8_t> result(data_.begin() + pos_, data_.begin() + pos_ + len);
            pos_ += len;
            return result;
        }

        size_t decode_length(uint8_t byte)
        {
            uint8_t ai = get_additional_info(byte);
            if (ai < 24)
            {
                return ai;
            }
            switch (ai)
            {
            case 24: return next_byte();
            case 25: return (static_cast<size_t>(next_byte()) << 8) | next_byte();
            case 26: {
                uint32_t val = 0;
                val = (val << 8) | next_byte();
                val = (val << 8) | next_byte();
                val = (val << 8) | next_byte();
                return val;
            }
            case 27: {
                uint64_t val = 0;
                for (int i = 0; i < 8; i++)
                {
                    val = (val << 8) | next_byte();
                }
                return val;
            }
            default:
                throw SerializationException::invalid_data(pos_ - 1, "Invalid length encoding");
            }
        }

        // Parse null or optional value
        bool is_null() const
        {
            return peek_byte() == 0xF6;
        }

        void expect_null()
        {
            uint8_t byte = next_byte();
            if (byte != 0xF6)
            {
                throw SerializationException::invalid_data(pos_ - 1, "Expected null");
            }
        }

        // Parse a hex string from byte string
        std::string parse_hex_from_byte_string()
        {
            auto bytes = parse_byte_string();
            return bytes_to_hex(bytes);
        }

        // Skip a single CBOR element recursively (any type)
        // Returns the number of bytes skipped
        size_t skip_cbor_element()
        {
            uint8_t byte = next_byte();
            MajorType mt = get_major_type(byte);
            uint8_t ai = get_additional_info(byte);

            size_t bytes_skipped = 1;

            switch (mt)
            {
            case MajorType::Uint:
            case MajorType::NegInt:
                // Integer: check ai for bytes to read
                if (ai < 24) {
                    // Single byte integer, already read
                } else if (ai == 24) {
                    bytes_skipped += 1; next_byte();
                } else if (ai == 25) {
                    bytes_skipped += 2; next_byte(); next_byte();
                } else if (ai == 26) {
                    bytes_skipped += 4; next_byte(); next_byte(); next_byte(); next_byte();
                } else if (ai == 27) {
                    bytes_skipped += 8; for (int i = 0; i < 8; i++) next_byte();
                }
                break;

            case MajorType::ByteString:
            case MajorType::Utf8String: {
                size_t len = decode_length(byte);
                bytes_skipped += len;
                skip(len);
                break;
            }

            case MajorType::Array: {
                size_t count = decode_array_count(byte);
                for (size_t i = 0; i < count; i++) {
                    bytes_skipped += skip_cbor_element();
                }
                break;
            }

            case MajorType::Map: {
                size_t pairs = decode_array_count(byte);
                for (size_t i = 0; i < pairs; i++) {
                    bytes_skipped += skip_cbor_element(); // key
                    bytes_skipped += skip_cbor_element(); // value
                }
                break;
            }

            case MajorType::Tag:
                bytes_skipped += skip_cbor_element();
                break;

            case MajorType::Simple:
                // null (0xF6), true (0xF5), false (0xF4), undefined, etc.
                if (ai == 24) {
                    bytes_skipped += 1; next_byte(); // Simple value in next byte
                }
                break;
            }

            return bytes_skipped;
        }

    private:
        const std::vector<uint8_t> &data_;
        size_t pos_;

        static std::string bytes_to_hex(const std::vector<uint8_t> &data)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (uint8_t byte : data)
            {
                oss << std::setw(2) << static_cast<int>(byte);
            }
            return oss.str();
        }
    };

    /**
     * @brief Midnight Transaction Types
     *
     * Matches Rust ledger_types.rs transaction pipeline:
     * - TransactionInput: UTXO reference
     * - ColoredOutput: Multi-asset output
     * - TransactionWitness: Ed25519 signature
     * - TransactionBody: Core transaction data
     * - SignedTransaction: Complete signed transaction
     */

    /// Network identifier for transaction encoding
    enum class NetworkId : uint8_t
    {
        MainNet = 1,
        TestNet = 0,
    };

    /// Token type for multi-asset support
    struct TokenType
    {
        std::string name;

        static TokenType lovelace() { return {"lovelace"}; }
        static TokenType from_hex(const std::string &hex);
        bool is_lovelace() const { return name == "lovelace"; }
    };

    /// Asset transfer (multi-asset)
    struct AssetTransfer
    {
        TokenType token_type;
        int64_t amount;  // Can be negative for burn
    };

    /// Transaction validity interval
    struct TransactionValidityInterval
    {
        std::optional<uint64_t> lower_bound;
        std::optional<uint64_t> upper_bound;
    };

    /// Confidential meeting position (for private contracts)
    struct ConfidentialMeeting
    {
        std::string meeting_id;
        uint32_t position;
    };

    /// Offset data (applied after ZK proof generation)
    struct OffsetData
    {
        uint64_t offset_value;
        std::string signature;
    };

    /// UTXO data for transaction inputs
    struct UtxoData
    {
        std::string address;           // Hex-encoded address
        uint64_t amount;                // Lovelace amount
        TokenType token_type;          // Token type
        std::optional<std::string> datum_hash;
        std::optional<std::string> nullifier;   // For shielded UTXOs
        std::optional<std::string> commitment;  // For shielded UTXOs
        std::optional<uint64_t> mt_index;       // Merkle tree index
    };

    /// Transaction input (UTXO reference)
    struct TransactionInput
    {
        std::string utxo_id;           // "tx_hash#output_index" format
        uint32_t output_index;
        std::optional<UtxoData> utxo_data;
    };

    /// Colored output (multi-asset output)
    struct ColoredOutput
    {
        std::string address;
        uint64_t amount;                // Lovelace amount
        TokenType token_type;
        std::optional<std::string> datum_hash;
        std::vector<AssetTransfer> assets;  // Additional assets
    };

    /// Transaction witness (Ed25519 signature)
    struct TransactionWitness
    {
        uint8_t witness_type;    // 0=Ed25519, 1=BLS, 2=ZKProof, 3=Multisig
        std::string public_key;        // Hex-encoded Ed25519 public key (32 bytes)
        std::string signature;         // Hex-encoded Ed25519 signature (64 bytes)
        std::optional<std::string> chain_code;  // For HD wallets
    };

    /**
     * @brief Transaction body (matches Rust TransactionBody)
     *
     * This is the core data that gets hashed (CBOR-encoded) and signed.
     */
    struct TransactionBody
    {
        std::vector<TransactionInput> inputs;
        std::vector<ColoredOutput> outputs;
        uint64_t fee;                          // Fee in lovelace
        NetworkId network_id;
        TransactionValidityInterval validity_interval;

        // Colored (multi-asset) inputs/outputs
        std::vector<TransactionInput> colored_inputs;
        std::vector<ColoredOutput> colored_outputs;

        // Minted/burned assets
        std::vector<AssetTransfer> mint;

        // Reference inputs (read-only)
        std::vector<TransactionInput> reference_inputs;

        // Private contract data
        std::optional<std::string> confidential_contract_hash;
        std::optional<ConfidentialMeeting> confidential_meeting;

        // Offset (applied after ZK proof generation)
        std::optional<OffsetData> offset;
    };

    /**
     * @brief Signed transaction (matches Rust SignedTransaction)
     *
     * Complete transaction ready for submission.
     */
    struct SignedTransaction
    {
        TransactionBody body;
        std::vector<TransactionWitness> witnesses;
        std::optional<std::string> auxiliary_data;  // JSON metadata

        /// Serialize to CBOR hex for submission
        std::string to_cbor_hex() const;

        /// Serialize to JSON for debugging
        std::string to_json() const;
    };

    /**
     * @brief Transaction builder for Midnight
     *
     * Builds properly formatted transactions matching the official SDK.
     *
     * Example:
     * @code
     *   TransactionBuilder builder;
     *   builder.add_input(utxo)
     *          .add_output("mn_addr_preprod...", 1_000_000)
     *          .with_fee(200_000)
     *          .sign(signer_keypair);
     *   auto tx = builder.build();
     * @endcode
     */
    class TransactionBuilder
    {
    public:
        TransactionBuilder();

        /// Add a UTXO input
        TransactionBuilder &add_input(const TransactionInput &input);

        /// Add a colored (multi-asset) output
        TransactionBuilder &add_output(const ColoredOutput &output);

        /// Add a simple lovelace output
        TransactionBuilder &add_output(const std::string &address, uint64_t lovelace_amount);

        /// Add a colored input
        TransactionBuilder &add_colored_input(const TransactionInput &input);

        /// Add a colored output
        TransactionBuilder &add_colored_output(const ColoredOutput &output);

        /// Add mint/burn asset
        TransactionBuilder &add_mint(const TokenType &token_type, int64_t amount);

        /// Add reference input (read-only)
        TransactionBuilder &add_reference_input(const TransactionInput &input);

        /// Set network ID
        TransactionBuilder &with_network(NetworkId network_id);

        /// Set fee manually
        TransactionBuilder &with_fee(uint64_t fee);

        /// Set validity interval
        TransactionBuilder &with_validity_interval(const TransactionValidityInterval &interval);

        /// Set confidential contract hash
        TransactionBuilder &with_confidential_contract(const std::string &contract_hash);

        /// Add auxiliary data (metadata)
        TransactionBuilder &with_auxiliary_data(const std::string &aux_data);

        /// Attach witnesses (signatures)
        TransactionBuilder &with_witnesses(const std::vector<TransactionWitness> &witnesses);

        /// Add a single witness
        TransactionBuilder &add_witness(const TransactionWitness &witness);

        /// Build the signed transaction
        std::optional<SignedTransaction> build() const;

        /// Get the transaction body (unsigned)
        const TransactionBody &get_body() const { return body_; }

        /// Validate transaction inputs
        bool validate_inputs() const;

        /// Validate transaction outputs
        bool validate_outputs() const;

    private:
        TransactionBody body_;
        std::vector<TransactionWitness> witnesses_;
        std::optional<std::string> auxiliary_data_;
        std::optional<uint64_t> explicit_fee_;

        /// Validate fee
        bool validate_fee() const;

        /// Calculate total input lovelace
        uint64_t total_input_lovelace() const;

        /// Calculate total output lovelace
        uint64_t total_output_lovelace() const;
    };

    /**
     * @brief UTXO Transaction Serialization
     *
     * Provides CBOR serialization for Midnight UTXO transactions.
     * Matches the Rust serde_cbor serialization format.
     */
    class Utf8TransactionSerializer
    {
    public:
        /// Serialize transaction body to CBOR bytes
        static std::vector<uint8_t> serialize_body(const TransactionBody &body);

        /// Serialize signed transaction to CBOR bytes
        static std::vector<uint8_t> serialize_signed(const SignedTransaction &tx);

        /// Deserialize transaction body from CBOR bytes
        static std::optional<TransactionBody> deserialize_body(const std::vector<uint8_t> &data);

        /// Deserialize signed transaction from CBOR bytes
        static std::optional<SignedTransaction> deserialize_signed(const std::vector<uint8_t> &data);

        /// Convert CBOR bytes to hex string
        static std::string bytes_to_hex(const std::vector<uint8_t> &data);

        /// Convert hex string to CBOR bytes
        static std::vector<uint8_t> hex_to_bytes(const std::string &hex);
    };

    /**
     * @brief Transaction Hash Computation
     *
     * Computes transaction hash as CBOR(body) -> SHA256
     * Matches Rust: sha2::Sha256::digest(&serde_cbor::to_vec(&body)?)
     */
    class TransactionHasher
    {
    public:
        /// Compute transaction hash from body
        static std::string compute_hash(const TransactionBody &body);

        /// Verify transaction hash
        static bool verify_hash(const TransactionBody &body, const std::string &expected_hash);
    };

} // namespace midnight::tx
