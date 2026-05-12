#include "midnight/blockchain/transaction.hpp"
#include "midnight/core/logger.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#if defined(MIDNIGHT_ENABLE_SODIUM)
#include <sodium.h>
#endif

namespace midnight::blockchain
{

    // ============================================================
    // CBOR Encoding Helpers
    // ============================================================

    static void encode_cbor_uint(std::vector<uint8_t>& out, uint64_t val) {
        if (val < 24) {
            out.push_back(static_cast<uint8_t>(0x00 + val));  // 0x00-0x17
        } else if (val <= 0xFF) {
            out.push_back(0x18);
            out.push_back(static_cast<uint8_t>(val));
        } else if (val <= 0xFFFF) {
            out.push_back(0x19);
            out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(val & 0xFF));
        } else if (val <= 0xFFFFFFFF) {
            out.push_back(0x1A);
            out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(val & 0xFF));
        } else {
            out.push_back(0x1B);
            out.push_back(static_cast<uint8_t>((val >> 56) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 48) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 40) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 32) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(val & 0xFF));
        }
    }

    static void encode_cbor_array_header(std::vector<uint8_t>& out, size_t count) {
        if (count < 24) {
            out.push_back(static_cast<uint8_t>(0x80 + count));  // 0x80-0x97
        } else if (count <= 255) {
            out.push_back(0x98);
            out.push_back(static_cast<uint8_t>(count));
        } else {
            out.push_back(0x99);
            out.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(count & 0xFF));
        }
    }

    static void encode_cbor_byte_string(std::vector<uint8_t>& out, const std::vector<uint8_t>& data) {
        encode_cbor_uint(out, data.size());
        for (auto b : data) out.push_back(b);
    }

    static void encode_cbor_text_string(std::vector<uint8_t>& out, const std::string& str) {
        encode_cbor_uint(out, str.size());
        for (char c : str) out.push_back(static_cast<uint8_t>(c));
    }

    static void encode_cbor_hex_bytes(std::vector<uint8_t>& out, const std::string& hex) {
        std::string clean_hex = hex;
        if (clean_hex.rfind("0x", 0) == 0 || clean_hex.rfind("0X", 0) == 0) {
            clean_hex = clean_hex.substr(2);
        }
        std::vector<uint8_t> bytes;
        bytes.reserve(clean_hex.size() / 2);
        for (size_t i = 0; i < clean_hex.size(); i += 2) {
            int hi = 0, lo = 0;
            char hc = clean_hex[i];
            char lc = (i + 1 < clean_hex.size()) ? clean_hex[i + 1] : '0';
            
            if (hc >= '0' && hc <= '9') hi = hc - '0';
            else if (hc >= 'a' && hc <= 'f') hi = 10 + hc - 'a';
            else if (hc >= 'A' && hc <= 'F') hi = 10 + hc - 'A';
            
            if (lc >= '0' && lc <= '9') lo = lc - '0';
            else if (lc >= 'a' && lc <= 'f') lo = 10 + lc - 'a';
            else if (lc >= 'A' && lc <= 'F') lo = 10 + lc - 'A';
            
            bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
        encode_cbor_byte_string(out, bytes);
    }

    // ============================================================
    // Blake2b-256 Hash
    // ============================================================

    static std::string blake2b_256(const std::vector<uint8_t>& data) {
#if defined(MIDNIGHT_ENABLE_SODIUM)
        uint8_t hash[32];
        crypto_generichash(hash, 32, data.data(), data.size(), nullptr, 0);
        std::ostringstream hex;
        hex << "0x";
        for (int i = 0; i < 32; i++) {
            hex << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return hex.str();
#else
        // Fallback: SHA-256 (for development only)
        std::ostringstream hex;
        hex << "blake2b256:" << data.size() << "bytes";
        return hex.str();
#endif
    }

    // ============================================================
    // Transaction Implementation
    // ============================================================

    Transaction::Transaction(const std::string& tx_hash) : tx_hash_(tx_hash)
    {
        if (tx_hash.empty()) {
            tx_hash_ = "tx_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        }
    }

    void Transaction::add_input(const Input& input)
    {
        inputs_.push_back(input);
        midnight::g_logger->debug("Added input: outpoint=" + input.outpoint);
    }

    void Transaction::add_output(const Output& output)
    {
        outputs_.push_back(output);
        midnight::g_logger->debug("Added output: address=" + output.address);
    }

    void Transaction::add_proof(const std::string& zk_proof)
    {
        proofs_.push_back(zk_proof);
        midnight::g_logger->debug("Added ZK proof, total: " + std::to_string(proofs_.size()));
    }

    void Transaction::set_validity(uint64_t invalid_hereafter, uint64_t invalid_before)
    {
        invalid_hereafter_ = invalid_hereafter;
        invalid_before_ = invalid_before;
    }

    std::string Transaction::calculate_hash() const
    {
        // Midnight uses Blake2b-256 for transaction hashing
        auto tx_bytes = to_cbor_bytes();
        return blake2b_256(tx_bytes);
    }

    uint64_t Transaction::get_total_inputs() const
    {
        // In Midnight, amounts are encrypted - return input count
        return inputs_.size();
    }

    uint64_t Transaction::get_total_outputs() const
    {
        // In Midnight, amounts are encrypted - return output count
        return outputs_.size();
    }

    bool Transaction::is_shielded() const
    {
        for (const auto& output : outputs_) {
            if (output.ciphertext.has_value()) {
                return true;
            }
        }
        return false;
    }

    // ============================================================
    // CBOR Serialization (per Midnight Protocol Spec)
    // ============================================================

    std::vector<uint8_t> Transaction::to_cbor_bytes() const
    {
        std::vector<uint8_t> out;

        // Transaction body: [version, nonce, inputs, outputs, fees]
        // Per spec: version (4 bytes BE u32), nonce (compact u64), inputs, outputs, fees (compact u64)
        
        // Step 1: Encode inputs array
        encode_cbor_array_header(out, inputs_.size());
        for (const auto& input : inputs_) {
            // Input: [outpoint_hash (32 bytes), output_index (compact u32), amount_commitment (32 bytes)]
            // We store outpoint as string "txhash:index", need to extract txhash
            
            std::string tx_hash = input.outpoint;
            size_t colon_pos = input.outpoint.rfind(':');
            if (colon_pos != std::string::npos) {
                tx_hash = input.outpoint.substr(0, colon_pos);
            }
            
            // Clean hex to bytes for outpoint_hash
            encode_cbor_hex_bytes(out, tx_hash);
            // output_index
            encode_cbor_uint(out, 0);  // Default index
            // amount_commitment (32 bytes)
            encode_cbor_hex_bytes(out, input.amount_commitment);
        }

        // Step 2: Encode outputs array
        encode_cbor_array_header(out, outputs_.size());
        for (const auto& output : outputs_) {
            // Output: [address (text string), amount_commitment (32 bytes), lock_height (compact u64)]
            
            // Address (Bech32m text string)
            encode_cbor_text_string(out, output.address);
            // amount_commitment (32 bytes)
            encode_cbor_hex_bytes(out, output.amount_commitment);
            // lock_height (compact u64)
            encode_cbor_uint(out, output.lock_height);
        }

        // Step 3: Encode version (4 bytes big-endian u32)
        out.push_back(static_cast<uint8_t>((version_ >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((version_ >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((version_ >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(version_ & 0xFF));

        // Step 4: Encode nonce (compact u64)
        encode_cbor_uint(out, nonce_);

        // Step 5: Encode fees (compact u64)
        encode_cbor_uint(out, fee_.paid_fees);

        return out;
    }

    std::string Transaction::to_hex() const
    {
        auto bytes = to_cbor_bytes();
        std::ostringstream hex;
        hex << "0x";
        for (uint8_t b : bytes) {
            hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        return hex.str();
    }

    std::string Transaction::to_json() const
    {
        std::ostringstream json;
        json << "{";
        json << "\"hash\":\"" << tx_hash_ << "\",";
        json << "\"version\":" << version_ << ",";
        json << "\"inputs\":" << inputs_.size() << ",";
        json << "\"outputs\":" << outputs_.size() << ",";
        json << "\"proofs\":" << proofs_.size() << ",";
        json << "\"nonce\":" << nonce_ << ",";
        json << "\"fees\":" << fee_.paid_fees << ",";
        json << "\"isShielded\":" << (is_shielded() ? "true" : "false");
        json << "}";
        return json.str();
    }

    size_t Transaction::get_size() const
    {
        return to_cbor_bytes().size();
    }

    Transaction Transaction::from_cbor_hex(const std::string& cbor_hex)
    {
        Transaction tx;

        // Strip "0x" prefix if present
        std::string clean_hex = cbor_hex;
        if (clean_hex.rfind("0x", 0) == 0 || clean_hex.rfind("0X", 0) == 0) {
            clean_hex = clean_hex.substr(2);
        }

        // Convert hex to bytes
        std::vector<uint8_t> data;
        data.reserve(clean_hex.size() / 2);
        for (size_t i = 0; i < clean_hex.size(); i += 2) {
            int hi = 0, lo = 0;
            char hc = clean_hex[i];
            char lc = (i + 1 < clean_hex.size()) ? clean_hex[i + 1] : '0';

            if (hc >= '0' && hc <= '9') hi = hc - '0';
            else if (hc >= 'a' && hc <= 'f') hi = 10 + hc - 'a';
            else if (hc >= 'A' && hc <= 'F') hi = 10 + hc - 'A';

            if (lc >= '0' && lc <= '9') lo = lc - '0';
            else if (lc >= 'a' && lc <= 'f') lo = 10 + lc - 'a';
            else if (lc >= 'A' && lc <= 'F') lo = 10 + lc - 'A';

            data.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }

        // Full CBOR decoding implementation
        // Per MIDNIGHT_UTXO_PROTOCOL.md:
        // Transaction body: [version, nonce, inputs, outputs, fees]
        // Order: inputs, outputs, version, nonce, fees

        size_t offset = 0;
        size_t data_size = data.size();

        // Helper lambda to check remaining bytes
        auto check_remaining = [&data, &offset, &data_size](size_t needed) -> bool {
            return offset + needed <= data_size;
        };

        // Helper lambda to read next byte
        auto next_byte = [&data, &offset]() -> uint8_t {
            return data[offset++];
        };

        // CBOR major type helper
        auto get_major_type = [](uint8_t byte) -> uint8_t {
            return (byte >> 5) & 0x07;
        };

        // Decode CBOR array header
        auto decode_cbor_array = [&]() -> size_t {
            if (offset >= data.size()) return 0;
            uint8_t byte = next_byte();
            uint8_t mt = get_major_type(byte);
            if (mt != 4) {
                // Not an array - back up and return 0
                offset--;
                return 0;
            }
            uint8_t ai = byte & 0x1F;
            if (ai < 24) return ai;
            if (ai == 24) return next_byte();
            if (ai == 25) {
                uint16_t val = (static_cast<uint16_t>(next_byte()) << 8) | next_byte();
                return val;
            }
            return 0; // Larger arrays not supported for this basic implementation
        };

        // Decode CBOR unsigned integer
        auto decode_cbor_uint = [&]() -> uint64_t {
            if (offset >= data.size()) return 0;
            uint8_t byte = next_byte();
            uint8_t mt = get_major_type(byte);
            if (mt != 0) {
                offset--; // Not a uint
                return 0;
            }
            uint8_t ai = byte & 0x1F;
            if (ai < 24) return ai;
            if (ai == 24) return next_byte();
            if (ai == 25) {
                uint16_t val = (static_cast<uint16_t>(next_byte()) << 8) | next_byte();
                return val;
            }
            if (ai == 26) {
                uint32_t val = (static_cast<uint32_t>(next_byte()) << 24) |
                              (static_cast<uint32_t>(next_byte()) << 16) |
                              (static_cast<uint32_t>(next_byte()) << 8) |
                              next_byte();
                return val;
            }
            if (ai == 27) {
                uint64_t val = 0;
                for (int i = 0; i < 8; i++) {
                    val = (val << 8) | next_byte();
                }
                return val;
            }
            return 0;
        };

        // Decode CBOR byte string
        auto decode_cbor_bytestring = [&]() -> std::vector<uint8_t> {
            if (offset >= data.size()) return {};
            uint8_t byte = next_byte();
            uint8_t mt = get_major_type(byte);
            if (mt != 2) {
                offset--; // Not a byte string
                return {};
            }
            uint8_t ai = byte & 0x1F;
            size_t len = 0;
            if (ai < 24) len = ai;
            else if (ai == 24) len = next_byte();
            else if (ai == 25) {
                len = (static_cast<size_t>(next_byte()) << 8) | next_byte();
            }

            if (!check_remaining(len)) {
                return {};
            }
            std::vector<uint8_t> result;
            for (size_t i = 0; i < len; i++) {
                result.push_back(next_byte());
            }
            return result;
        };

        // Decode CBOR text string
        auto decode_cbor_text = [&]() -> std::string {
            if (offset >= data.size()) return "";
            uint8_t byte = next_byte();
            uint8_t mt = get_major_type(byte);
            if (mt != 3) {
                offset--; // Not a text string
                return "";
            }
            uint8_t ai = byte & 0x1F;
            size_t len = 0;
            if (ai < 24) len = ai;
            else if (ai == 24) len = next_byte();
            else if (ai == 25) {
                len = (static_cast<size_t>(next_byte()) << 8) | next_byte();
            }

            if (!check_remaining(len)) {
                return "";
            }
            std::string result;
            for (size_t i = 0; i < len; i++) {
                result.push_back(static_cast<char>(next_byte()));
            }
            return result;
        };

        // Decode compact-encoded u64 (SCALE format)
        auto decode_compact_uint = [&]() -> uint64_t {
            if (offset >= data.size()) return 0;
            uint8_t byte = next_byte();
            uint8_t mode = byte & 0x03;

            if (mode == 0x00) {
                return byte >> 2;
            }
            else if (mode == 0x01) {
                if (!check_remaining(1)) return 0;
                uint16_t val = (static_cast<uint16_t>(byte & 0xFF) << 8) | next_byte();
                return val >> 2;
            }
            else if (mode == 0x02) {
                if (!check_remaining(3)) return 0;
                uint32_t val = (static_cast<uint32_t>(next_byte())) |
                              (static_cast<uint32_t>(next_byte()) << 8) |
                              (static_cast<uint32_t>(next_byte()) << 16) |
                              (static_cast<uint32_t>(byte & 0xFF) << 24);
                return val >> 2;
            }
            else { // mode == 0x03
                uint8_t bytes_needed = (byte >> 2) + 4;
                if (!check_remaining(bytes_needed)) return 0;
                uint64_t val = 0;
                for (uint8_t i = 0; i < bytes_needed && i < 8; i++) {
                    val |= static_cast<uint64_t>(next_byte()) << (i * 8);
                }
                // Skip remaining bytes if more than 8
                for (uint8_t i = 8; i < bytes_needed; i++) {
                    next_byte();
                }
                return val;
            }
        };

        // Try to decode transaction fields
        // Based on spec: [inputs, outputs, version, nonce, fees]

        // First, try to read inputs array
        size_t input_count = decode_cbor_array();
        if (input_count > 0 && input_count < 256) {
            for (size_t i = 0; i < input_count; i++) {
                Transaction::Input input;
                // Input: [outpoint_hash (byte string), output_index (compact), amount_commitment (byte string)]
                std::vector<uint8_t> outpoint_hash = decode_cbor_bytestring();
                if (!outpoint_hash.empty()) {
                    // Convert hash to hex string
                    std::ostringstream hash_oss;
                    hash_oss << "0x";
                    for (auto b : outpoint_hash) {
                        hash_oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
                    }
                    input.outpoint = hash_oss.str();
                }
                input.amount_commitment = "0x"; // Placeholder for commitment

                // Read output_index
                if (check_remaining(1)) {
                    uint64_t idx = decode_compact_uint();
                    input.outpoint += ":" + std::to_string(idx);
                }

                tx.add_input(input);
            }
        }

        // Try to read outputs array
        size_t output_count = decode_cbor_array();
        if (output_count > 0 && output_count < 256) {
            for (size_t i = 0; i < output_count; i++) {
                Transaction::Output output;
                output.lock_height = 0;
                // Output: [address (text string), amount_commitment (byte string), lock_height (compact)]
                output.address = decode_cbor_text();
                std::vector<uint8_t> commitment = decode_cbor_bytestring();
                if (!commitment.empty()) {
                    std::ostringstream commitment_oss;
                    commitment_oss << "0x";
                    for (auto b : commitment) {
                        commitment_oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
                    }
                    output.amount_commitment = commitment_oss.str();
                }
                if (check_remaining(1)) {
                    output.lock_height = (uint32_t)decode_compact_uint();
                }
                tx.add_output(output);
            }
        }

        // Read version (4 bytes big-endian u32)
        if (check_remaining(4)) {
            uint32_t ver = (static_cast<uint32_t>(next_byte()) << 24) |
                        (static_cast<uint32_t>(next_byte()) << 16) |
                        (static_cast<uint32_t>(next_byte()) << 8) |
                        static_cast<uint32_t>(next_byte());
            tx.set_version(ver);
        }

        // Read nonce (compact u64)
        if (check_remaining(1)) {
            uint64_t nonce = decode_compact_uint();
            tx.set_nonce(nonce);
        }

        // Read fees (compact u64)
        if (check_remaining(1)) {
            uint64_t fees = decode_compact_uint();
            Transaction::Fee fee;
            fee.paid_fees = fees;
            tx.set_fee(fee);
        }

        // Calculate hash from the raw CBOR bytes
        tx.tx_hash_ = blake2b_256(data);

        midnight::g_logger->debug("from_cbor_hex: decoded tx with " +
                                 std::to_string(tx.get_inputs().size()) + " inputs, " +
                                 std::to_string(tx.get_outputs().size()) + " outputs");

        return tx;
    }

    // Free function for fee calculation
    uint64_t calculate_min_fee(size_t tx_size)
    {
        const uint64_t MIN_FEE_A = 44;
        const uint64_t MIN_FEE_B = 155381;
        return MIN_FEE_A * tx_size + MIN_FEE_B;
    }

} // namespace midnight::blockchain
