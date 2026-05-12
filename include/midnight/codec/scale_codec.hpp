#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace midnight::codec
{

    // u128 type for balance storage (little-endian)
    struct u128 {
        uint64_t lo = 0;  // lower 64 bits
        uint64_t hi = 0;  // upper 64 bits

        u128() = default;
        u128(uint64_t low) : lo(low), hi(0) {}
        u128(uint64_t low, uint64_t high) : lo(low), hi(high) {}
        u128& operator=(uint64_t val) { lo = val; hi = 0; return *this; }

        bool operator==(const u128& other) const { return lo == other.lo && hi == other.hi; }
        bool operator!=(const u128& other) const { return !(*this == other); }
        bool operator<(const u128& other) const {
            if (hi != other.hi) return hi < other.hi;
            return lo < other.lo;
        }
        bool operator>(const u128& other) const { return other < *this; }
        bool operator<=(const u128& other) const { return !(other < *this); }
        bool operator>=(const u128& other) const { return !(*this < other); }

        std::string to_decimal() const;
    };

    /**
     * @brief SCALE (Simple Concatenated Aggregate Little-Endian) encoder
     *
     * Implements the Substrate/Polkadot binary serialization format.
     * All multi-byte integers are little-endian.
     *
     * Compact integer encoding:
     *   0..63         → 1 byte:  (value << 2) | 0b00
     *   64..16383     → 2 bytes: (value << 2) | 0b01  (LE)
     *   16384..2^30-1 → 4 bytes: (value << 2) | 0b10  (LE)
     *   >= 2^30       → big-int: (bytes_needed-4 << 2) | 0b11 + LE bytes
     */
    class ScaleEncoder
    {
    public:
        void encode_compact(uint64_t value);

        void encode_u8(uint8_t value);
        void encode_u16_le(uint16_t value);
        void encode_u32_le(uint32_t value);
        void encode_u64_le(uint64_t value);
        void encode_u128_le(const uint8_t bytes[16]);
        void encode_bool(bool value);

        /// Length-prefixed bytes (compact length + raw data)
        void encode_bytes(const std::vector<uint8_t> &data);

        /// Raw bytes without length prefix
        void encode_raw(const std::vector<uint8_t> &data);

        /// SCALE string = compact-length + UTF-8 bytes
        void encode_string(const std::string &str);

        /// Option<T> = None
        void encode_option_none();

        /// Option<T> = Some(data) — caller must encode inner data
        void encode_option_some_prefix();

        /// Immortal era (0x00)
        void encode_era_immortal();

        /// Mortal era: period must be power-of-2 in [4..65536]
        void encode_era_mortal(uint64_t current_block, uint64_t period = 64);

        /// MultiAddress::Id(AccountId32) — prefix 0x00 + 32 bytes
        void encode_multi_address_id(const std::vector<uint8_t> &account_id_32);

        const std::vector<uint8_t> &data() const { return buffer_; }
        size_t size() const { return buffer_.size(); }
        void clear() { buffer_.clear(); }

        /// Prepend compact-encoded length to existing data
        std::vector<uint8_t> finalize_with_length() const;

    private:
        std::vector<uint8_t> buffer_;
    };

    /**
     * @brief SCALE decoder
     */
    class ScaleDecoder
    {
    public:
        explicit ScaleDecoder(const std::vector<uint8_t> &data);

        uint64_t decode_compact();
        uint8_t decode_u8();
        uint16_t decode_u16_le();
        uint32_t decode_u32_le();
        uint64_t decode_u64_le();
        u128 decode_u128_le();
        bool decode_bool();
        std::vector<uint8_t> decode_bytes();
        std::vector<uint8_t> decode_raw(size_t len);
        std::string decode_string();

        size_t offset() const { return offset_; }
        size_t remaining() const { return data_.size() - offset_; }
        bool empty() const { return offset_ >= data_.size(); }

    private:
        const std::vector<uint8_t> &data_;
        size_t offset_ = 0;

        void check_remaining(size_t needed) const;
    };

    namespace util
    {
        std::string bytes_to_hex(const std::vector<uint8_t> &data);
        std::vector<uint8_t> hex_to_bytes(const std::string &hex);
    }

} // namespace midnight::codec
