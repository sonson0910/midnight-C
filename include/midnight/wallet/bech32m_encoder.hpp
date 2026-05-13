#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace midnight::wallet
{
    /**
     * Midnight Bech32m address encoder - matches @midnight-ntwrk/wallet-sdk-address-format
     */
    class Bech32mEncoder
    {
    public:
        static std::vector<uint8_t> bytes_to_words(const std::vector<uint8_t>& data)
        {
            std::vector<uint8_t> words;
            int buffer = 0, bits = 0;
            for (auto byte : data) {
                buffer = (buffer << 8) | byte;
                bits += 8;
                while (bits >= 5) {
                    bits -= 5;
                    words.push_back(static_cast<uint8_t>((buffer >> bits) & 0x1F));
                }
            }
            if (bits > 0) words.push_back(static_cast<uint8_t>((buffer << (5 - bits)) & 0x1F));
            return words;
        }

        static uint32_t polymod(const std::vector<uint8_t>& values)
        {
            // Fixed: Match bech32m.cpp reference implementation exactly
            // polymod takes uint8_t values (5-bit words), not uint32_t
            static const uint32_t GEN[5] = {0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3};
            uint32_t chk = 1;
            for (auto v : values) {
                uint8_t top = static_cast<uint8_t>(chk >> 25);
                chk = ((chk & 0x1FFFFFF) << 5) ^ v;
                if (top & 1) chk ^= GEN[0];
                if (top & 2) chk ^= GEN[1];
                if (top & 4) chk ^= GEN[2];
                if (top & 8) chk ^= GEN[3];
                if (top & 16) chk ^= GEN[4];
            }
            return chk;
        }

        static std::vector<uint8_t> expand_hrp(const std::string& hrp)
        {
            // Fixed: Return uint8_t to match bech32m.cpp reference
            std::vector<uint8_t> ret;
            for (unsigned char c : hrp) ret.push_back(c >> 5);
            ret.push_back(0);
            for (unsigned char c : hrp) ret.push_back(c & 31);
            return ret;
        }

        // Bech32m encode: hrp + data (5-bit words already converted)
        static std::string encode_raw(const std::string& hrp, const std::vector<uint8_t>& words_in)
        {
            // Fixed: Use uint8_t consistently to match bech32m.cpp reference
            std::vector<uint8_t> values = expand_hrp(hrp);
            std::vector<uint8_t> all_values = values;
            for (auto w : words_in) all_values.push_back(w);
            all_values.insert(all_values.end(), {0, 0, 0, 0, 0, 0});

            uint32_t checksum = polymod(all_values) ^ 0x2BC830A3UL;

            std::vector<uint8_t> result_words = words_in;
            for (int i = 0; i < 6; ++i)
                result_words.push_back(static_cast<uint8_t>((checksum >> (5 * (5 - i))) & 31));

            static const char* CHARS = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
            std::string result = hrp + "1";
            for (auto w : result_words) result += CHARS[w];
            return result;
        }

        // Main encode: takes raw bytes
        static std::string encode(const std::string& hrp, const std::vector<uint8_t>& data)
        {
            return encode_raw(hrp, bytes_to_words(data));
        }

        // SCALE compact encoding for BigInt - matches @subsquid/scale-codec ByteSink.compact()
        // Input: big-endian byte representation of the BigInt value
        static std::vector<uint8_t> scale_bigint_encode_be(const std::vector<uint8_t>& bigint_be)
        {
            // Find actual byte length (strip leading zeros)
            size_t actual = bigint_be.size();
            while (actual > 1 && bigint_be[bigint_be.size() - actual] == 0) --actual;
            if (actual == 0) actual = 1;

            std::vector<uint8_t> result;

            if (actual <= 4) {
                // Small integer: parse BE bytes to uint64
                uint64_t val = 0;
                for (size_t i = 0; i < actual; ++i) {
                    val = (val << 8) | bigint_be[bigint_be.size() - actual + i];
                }
                // Compact encode
                if (val < 0x40) {
                    result.push_back(static_cast<uint8_t>(val << 2));
                } else if (val < 0x4000) {
                    result.push_back(static_cast<uint8_t>((val >> 8) << 2 | 0x01));
                    result.push_back(static_cast<uint8_t>(val & 0xFF));
                } else if (val < 0x40000000) {
                    result.push_back(static_cast<uint8_t>((val >> 24) << 2 | 0x02));
                    result.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
                    result.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                    result.push_back(static_cast<uint8_t>(val & 0xFF));
                } else {
                    result.push_back(static_cast<uint8_t>(((actual - 4) << 2) | 0x03));
                    for (size_t i = 0; i < actual; ++i)
                        result.push_back(bigint_be[bigint_be.size() - actual + i]);
                }
            } else {
                // BigInteger mode: header = ((len - 4) << 2) | 3, then BE bytes
                result.push_back(static_cast<uint8_t>(((actual - 4) << 2) | 0x03));
                for (size_t i = 0; i < actual; ++i)
                    result.push_back(bigint_be[bigint_be.size() - actual + i]);
            }

            return result;
        }

        // Convert 32-byte scalar (little-endian, as stored in BIP32 HD key)
        // to big-endian representation for SCALE encoding
        static std::vector<uint8_t> scalar_le_to_be_32(const std::vector<uint8_t>& le)
        {
            std::vector<uint8_t> be(32);
            for (int i = 0; i < 32; ++i) be[i] = le[32 - 1 - i];
            return be;
        }

        // Unshielded address: BIP-340 x-only public key (32 bytes)
        static std::string encode_unshielded(const std::vector<uint8_t>& xonly_pubkey,
                                             const std::string& network)
        {
            std::string hrp = "mn_addr" + (network.empty() || network == "mainnet" ? "" : "_" + network);
            return encode(hrp, xonly_pubkey);
        }

        // Encode dust from 32-byte scalar (BIP32 HD key)
        // Note: DustSecretKey.fromSeed applies ledger-specific key transforms.
        // Prefer addresses derived by the official Midnight wallet/toolkit when
        // starting from a mnemonic or seed phrase.
        static std::string encode_dust_from_scalar(const std::vector<uint8_t>& scalar_le,
                                                  const std::string& network)
        {
            auto be = scalar_le_to_be_32(scalar_le);
            auto scale = scale_bigint_encode_be(be);
            std::string hrp = "mn_dust" + (network.empty() || network == "mainnet" ? "" : "_" + network);
            return encode(hrp, scale);
        }

        // Encode dust from raw bigint value (BE bytes, pre-computed)
        static std::string encode_dust_from_bigint_be(const std::vector<uint8_t>& bigint_be,
                                                      const std::string& network)
        {
            auto scale = scale_bigint_encode_be(bigint_be);
            std::string hrp = "mn_dust" + (network.empty() || network == "mainnet" ? "" : "_" + network);
            return encode(hrp, scale);
        }

        static std::string encode_shielded(const std::vector<uint8_t>& pubkey,
                                           const std::string& network)
        {
            std::string hrp = "mn_shield-addr" + (network.empty() || network == "mainnet" ? "" : "_" + network);
            return encode(hrp, pubkey);
        }
    };
} // namespace midnight::wallet
