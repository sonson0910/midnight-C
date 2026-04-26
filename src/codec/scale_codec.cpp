#include "midnight/codec/scale_codec.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace midnight::codec
{

    // ─── ScaleEncoder ─────────────────────────────────────────

    void ScaleEncoder::encode_compact(uint64_t value)
    {
        if (value <= 0x3F)
        {
            buffer_.push_back(static_cast<uint8_t>((value << 2) | 0x00));
        }
        else if (value <= 0x3FFF)
        {
            uint16_t encoded = static_cast<uint16_t>((value << 2) | 0x01);
            buffer_.push_back(static_cast<uint8_t>(encoded & 0xFF));
            buffer_.push_back(static_cast<uint8_t>((encoded >> 8) & 0xFF));
        }
        else if (value <= 0x3FFFFFFF)
        {
            uint32_t encoded = static_cast<uint32_t>((value << 2) | 0x02);
            buffer_.push_back(static_cast<uint8_t>(encoded & 0xFF));
            buffer_.push_back(static_cast<uint8_t>((encoded >> 8) & 0xFF));
            buffer_.push_back(static_cast<uint8_t>((encoded >> 16) & 0xFF));
            buffer_.push_back(static_cast<uint8_t>((encoded >> 24) & 0xFF));
        }
        else
        {
            // Big-integer mode: determine how many bytes needed
            uint8_t bytes_needed = 0;
            uint64_t temp = value;
            while (temp > 0)
            {
                bytes_needed++;
                temp >>= 8;
            }
            if (bytes_needed < 4)
                bytes_needed = 4;

            buffer_.push_back(static_cast<uint8_t>(((bytes_needed - 4) << 2) | 0x03));
            for (uint8_t i = 0; i < bytes_needed; i++)
            {
                buffer_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
            }
        }
    }

    void ScaleEncoder::encode_u8(uint8_t value)
    {
        buffer_.push_back(value);
    }

    void ScaleEncoder::encode_u16_le(uint16_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void ScaleEncoder::encode_u32_le(uint32_t value)
    {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void ScaleEncoder::encode_u64_le(uint64_t value)
    {
        for (int i = 0; i < 8; i++)
        {
            buffer_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
        }
    }

    void ScaleEncoder::encode_u128_le(const uint8_t bytes[16])
    {
        buffer_.insert(buffer_.end(), bytes, bytes + 16);
    }

    void ScaleEncoder::encode_bool(bool value)
    {
        buffer_.push_back(value ? 0x01 : 0x00);
    }

    void ScaleEncoder::encode_bytes(const std::vector<uint8_t> &data)
    {
        encode_compact(data.size());
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void ScaleEncoder::encode_raw(const std::vector<uint8_t> &data)
    {
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    void ScaleEncoder::encode_string(const std::string &str)
    {
        encode_compact(str.size());
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    void ScaleEncoder::encode_option_none()
    {
        buffer_.push_back(0x00);
    }

    void ScaleEncoder::encode_option_some_prefix()
    {
        buffer_.push_back(0x01);
    }

    void ScaleEncoder::encode_era_immortal()
    {
        buffer_.push_back(0x00);
    }

    void ScaleEncoder::encode_era_mortal(uint64_t current_block, uint64_t period)
    {
        // Period must be power of 2, clamped to [4, 65536]
        uint64_t clamped = std::max(uint64_t(4), std::min(uint64_t(65536), period));

        // Round to nearest power of 2
        uint64_t p = 4;
        while (p < clamped)
            p <<= 1;

        // quantize_factor = max(1, period / 4096)
        uint64_t quantize_factor = std::max(uint64_t(1), p / 4096);
        uint64_t phase = (current_block % p) / quantize_factor * quantize_factor;

        // calced_period_log2
        uint16_t period_log2 = 0;
        uint64_t tmp = p;
        while (tmp > 1)
        {
            period_log2++;
            tmp >>= 1;
        }

        // Encoded as u16 LE: low nibble = period_log2 - 1, high 12 bits = phase / quantize_factor
        uint16_t encoded = static_cast<uint16_t>(
            std::min(uint16_t(15), static_cast<uint16_t>(period_log2 - 1)) |
            ((phase / quantize_factor) << 4));

        encode_u16_le(encoded);
    }

    void ScaleEncoder::encode_multi_address_id(const std::vector<uint8_t> &account_id_32)
    {
        if (account_id_32.size() != 32)
            throw std::runtime_error("AccountId must be 32 bytes");

        buffer_.push_back(0x00); // MultiAddress::Id variant
        buffer_.insert(buffer_.end(), account_id_32.begin(), account_id_32.end());
    }

    std::vector<uint8_t> ScaleEncoder::finalize_with_length() const
    {
        ScaleEncoder length_encoder;
        length_encoder.encode_compact(buffer_.size());
        std::vector<uint8_t> result = length_encoder.data();
        result.insert(result.end(), buffer_.begin(), buffer_.end());
        return result;
    }

    // ─── ScaleDecoder ─────────────────────────────────────────

    ScaleDecoder::ScaleDecoder(const std::vector<uint8_t> &data) : data_(data) {}

    void ScaleDecoder::check_remaining(size_t needed) const
    {
        if (offset_ + needed > data_.size())
            throw std::runtime_error("SCALE decode: insufficient data");
    }

    uint64_t ScaleDecoder::decode_compact()
    {
        check_remaining(1);
        uint8_t first = data_[offset_];
        uint8_t mode = first & 0x03;

        if (mode == 0x00)
        {
            offset_++;
            return first >> 2;
        }
        else if (mode == 0x01)
        {
            check_remaining(2);
            uint16_t val = static_cast<uint16_t>(data_[offset_]) |
                           (static_cast<uint16_t>(data_[offset_ + 1]) << 8);
            offset_ += 2;
            return val >> 2;
        }
        else if (mode == 0x02)
        {
            check_remaining(4);
            uint32_t val = static_cast<uint32_t>(data_[offset_]) |
                           (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                           (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
                           (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
            offset_ += 4;
            return val >> 2;
        }
        else
        { // mode == 0x03 big-integer
            uint8_t bytes_needed = (first >> 2) + 4;
            offset_++;
            check_remaining(bytes_needed);
            uint64_t val = 0;
            for (uint8_t i = 0; i < bytes_needed && i < 8; i++)
            {
                val |= static_cast<uint64_t>(data_[offset_ + i]) << (i * 8);
            }
            offset_ += bytes_needed;
            return val;
        }
    }

    uint8_t ScaleDecoder::decode_u8()
    {
        check_remaining(1);
        return data_[offset_++];
    }

    uint16_t ScaleDecoder::decode_u16_le()
    {
        check_remaining(2);
        uint16_t val = static_cast<uint16_t>(data_[offset_]) |
                       (static_cast<uint16_t>(data_[offset_ + 1]) << 8);
        offset_ += 2;
        return val;
    }

    uint32_t ScaleDecoder::decode_u32_le()
    {
        check_remaining(4);
        uint32_t val = static_cast<uint32_t>(data_[offset_]) |
                       (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                       (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
                       (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
        offset_ += 4;
        return val;
    }

    uint64_t ScaleDecoder::decode_u64_le()
    {
        check_remaining(8);
        uint64_t val = 0;
        for (int i = 0; i < 8; i++)
        {
            val |= static_cast<uint64_t>(data_[offset_ + i]) << (i * 8);
        }
        offset_ += 8;
        return val;
    }

    bool ScaleDecoder::decode_bool()
    {
        return decode_u8() != 0;
    }

    std::vector<uint8_t> ScaleDecoder::decode_bytes()
    {
        uint64_t len = decode_compact();
        check_remaining(static_cast<size_t>(len));
        std::vector<uint8_t> result(data_.begin() + offset_,
                                    data_.begin() + offset_ + len);
        offset_ += static_cast<size_t>(len);
        return result;
    }

    std::vector<uint8_t> ScaleDecoder::decode_raw(size_t len)
    {
        check_remaining(len);
        std::vector<uint8_t> result(data_.begin() + offset_,
                                    data_.begin() + offset_ + len);
        offset_ += len;
        return result;
    }

    std::string ScaleDecoder::decode_string()
    {
        auto bytes = decode_bytes();
        return {bytes.begin(), bytes.end()};
    }

    // ─── Utilities ────────────────────────────────────────────

    namespace util
    {
        std::string bytes_to_hex(const std::vector<uint8_t> &data)
        {
            std::ostringstream oss;
            oss << "0x";
            for (auto b : data)
                oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
            return oss.str();
        }

        std::vector<uint8_t> hex_to_bytes(const std::string &hex)
        {
            std::string clean = hex;
            if (clean.substr(0, 2) == "0x" || clean.substr(0, 2) == "0X")
                clean = clean.substr(2);

            if (clean.size() % 2 != 0)
                throw std::runtime_error("hex_to_bytes: odd length");

            std::vector<uint8_t> result;
            result.reserve(clean.size() / 2);
            for (size_t i = 0; i < clean.size(); i += 2)
            {
                auto byte_str = clean.substr(i, 2);
                result.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
            }
            return result;
        }
    }

} // namespace midnight::codec
