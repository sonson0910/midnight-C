#pragma once
/**
 * @file common_utils.hpp
 * @brief Shared utility functions for the Midnight SDK
 *
 * Consolidates duplicate utility implementations that were previously
 * copy-pasted across multiple translation units (DRY principle).
 */

#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <algorithm>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace midnight::util
{

    // ============================================================================
    // Hex Utilities
    // ============================================================================

    /**
     * @brief Strip "0x" or "0X" prefix from hex string
     * @param value Input string potentially prefixed with 0x
     * @return String without hex prefix
     */
    inline std::string strip_hex_prefix(const std::string &value)
    {
        if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
        {
            return value.substr(2);
        }
        return value;
    }

    /**
     * @brief Check if a string contains only valid hex characters
     * @param value String to check (without 0x prefix)
     * @return true if valid hex string with even length
     */
    inline bool is_hex_string(const std::string &value)
    {
        if (value.empty() || (value.size() % 2) != 0)
        {
            return false;
        }

        for (char c : value)
        {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
            {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Convert a single hex character to its integer value (0-15)
     * @param c Hex character
     * @return Integer value, or -1 if invalid
     */
    inline int hex_nibble(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }

        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower >= 'a' && lower <= 'f')
        {
            return 10 + (lower - 'a');
        }

        return -1;
    }

    // ============================================================================
    // Shell Utilities
    // ============================================================================

    /**
     * @brief Safely quote a string for shell command interpolation
     * @param value String to quote
     * @return Shell-safe quoted string
     *
     * On Unix: uses single-quote wrapping with proper escaping
     * On Windows: uses double-quote wrapping with proper escaping
     */
    inline std::string quote_shell_arg(const std::string &value)
    {
#ifdef _WIN32
        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('"');
        for (char ch : value)
        {
            if (ch == '"')
            {
                escaped += "\\\"";
            }
            else
            {
                escaped.push_back(ch);
            }
        }
        escaped.push_back('"');
        return escaped;
#else
        std::string escaped = "'";
        for (char ch : value)
        {
            if (ch == '\'')
            {
                escaped += "'\\''";
            }
            else
            {
                escaped.push_back(ch);
            }
        }
        escaped.push_back('\'');
        return escaped;
#endif
    }

    /**
     * @brief Run a shell command and capture its stdout output
     * @param command Shell command to run
     * @param[out] exit_code Exit code of the command
     * @return Captured stdout as string
     */
    inline std::string run_command_capture(const std::string &command, int &exit_code)
    {
        std::string output;

#ifdef _WIN32
        FILE *pipe = _popen(command.c_str(), "r");
#else
        FILE *pipe = popen(command.c_str(), "r");
#endif
        if (pipe == nullptr)
        {
            exit_code = -1;
            return output;
        }

        std::array<char, 512> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            output += buffer.data();
        }

#ifdef _WIN32
        exit_code = _pclose(pipe);
#else
        int status = pclose(pipe);
        if (WIFEXITED(status))
        {
            exit_code = WEXITSTATUS(status);
        }
        else
        {
            exit_code = status;
        }
#endif

        return output;
    }

    // ============================================================================
    // JSON Utilities
    // ============================================================================

    /**
     * @brief Extract the first JSON object from raw command output
     *
     * Finds the first '{' and last '}' in the output string and returns
     * the substring between them (inclusive). Useful for parsing output
     * from bridge scripts that may include non-JSON preamble/postamble.
     *
     * @param raw_output Raw command output
     * @return Extracted JSON payload, or the original string if no braces found
     */
    inline std::string extract_json_payload(const std::string &raw_output)
    {
        const auto first = raw_output.find('{');
        const auto last = raw_output.rfind('}');
        if (first == std::string::npos || last == std::string::npos || first > last)
        {
            return raw_output;
        }
        return raw_output.substr(first, last - first + 1);
    }

    // ============================================================================
    // String Utilities
    // ============================================================================

    /**
     * @brief Copy a std::string into a fixed C buffer with null termination
     * @param input Source string
     * @param output Destination C buffer
     * @param capacity Size of destination buffer
     */
    inline void copy_string_to_c_buffer(const std::string &input, char *output, size_t capacity)
    {
        if (output == nullptr || capacity == 0)
        {
            return;
        }

        const size_t copy_len = std::min(input.size(), capacity - 1);
        std::memcpy(output, input.data(), copy_len);
        output[copy_len] = '\0';
    }

    /**
     * @brief Return a lowercased copy of a string
     * @param value Input string
     * @return Lowercased copy
     */
    inline std::string to_lower_copy(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char ch)
            { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    // ============================================================================
    // Bytes-to-Hex Encoding
    // ============================================================================

    /**
     * @brief Convert a byte vector to a lowercase hex string (no prefix)
     * @param bytes Input byte vector
     * @return Lowercase hex string without 0x prefix
     */
    inline std::string bytes_to_hex(const std::vector<uint8_t> &bytes)
    {
        static const char *kHex = "0123456789abcdef";
        std::string encoded;
        encoded.reserve(bytes.size() * 2);
        for (uint8_t byte : bytes)
        {
            encoded.push_back(kHex[(byte >> 4) & 0x0F]);
            encoded.push_back(kHex[byte & 0x0F]);
        }
        return encoded;
    }

    // ============================================================================
    // Bech32m Encoding (BIP-350)
    // ============================================================================

    namespace bech32m
    {
        constexpr uint32_t kBech32mConst = 0x2BC830A3;
        constexpr const char *kBech32Charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

        inline uint32_t polymod(const std::vector<uint8_t> &values)
        {
            static constexpr uint32_t gen[5] = {0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3};
            uint32_t chk = 1;
            for (auto value : values)
            {
                const uint32_t top = chk >> 25;
                chk = ((chk & 0x1FFFFFF) << 5) ^ value;
                for (int i = 0; i < 5; ++i)
                {
                    chk ^= ((top >> i) & 1) ? gen[i] : 0;
                }
            }
            return chk;
        }

        inline std::vector<uint8_t> hrp_expand(const std::string &hrp)
        {
            std::vector<uint8_t> ret;
            ret.reserve(hrp.size() * 2 + 1);
            for (unsigned char c : hrp)
            {
                ret.push_back(c >> 5);
            }
            ret.push_back(0);
            for (unsigned char c : hrp)
            {
                ret.push_back(c & 31);
            }
            return ret;
        }

        inline std::vector<uint8_t> convert_bits(const uint8_t *data, size_t data_len, int from_bits, int to_bits, bool pad)
        {
            int acc = 0;
            int bits = 0;
            const int maxv = (1 << to_bits) - 1;
            const int max_acc = (1 << (from_bits + to_bits - 1)) - 1;
            std::vector<uint8_t> ret;
            ret.reserve((data_len * from_bits + to_bits - 1) / to_bits);

            for (size_t i = 0; i < data_len; ++i)
            {
                const int value = data[i];
                acc = ((acc << from_bits) | value) & max_acc;
                bits += from_bits;
                while (bits >= to_bits)
                {
                    bits -= to_bits;
                    ret.push_back((acc >> bits) & maxv);
                }
            }

            if (pad)
            {
                if (bits != 0)
                {
                    ret.push_back(static_cast<uint8_t>((acc << (to_bits - bits)) & maxv));
                }
            }
            else if (bits >= from_bits || ((acc << (to_bits - bits)) & maxv) != 0)
            {
                return {};
            }

            return ret;
        }

        inline std::string encode(const std::string &hrp, const std::array<uint8_t, 32> &payload)
        {
            std::vector<uint8_t> data;
            auto converted = convert_bits(payload.data(), payload.size(), 8, 5, true);
            data.insert(data.end(), converted.begin(), converted.end());

            auto values = hrp_expand(hrp);
            values.insert(values.end(), data.begin(), data.end());
            values.insert(values.end(), 6, 0);
            const uint32_t pm = polymod(values) ^ kBech32mConst;

            std::array<uint8_t, 6> checksum{};
            for (int i = 0; i < 6; ++i)
            {
                checksum[i] = (pm >> (5 * (5 - i))) & 31;
            }

            std::string out = hrp + "1";
            out.reserve(hrp.size() + 1 + data.size() + checksum.size());
            for (auto v : data)
            {
                out.push_back(kBech32Charset[v]);
            }
            for (auto v : checksum)
            {
                out.push_back(kBech32Charset[v]);
            }
            return out;
        }

    } // namespace bech32m

    // ============================================================================
    // JSON Bridge Utilities
    // ============================================================================

    /**
     * @brief Convert nlohmann::json to Json::Value (jsoncpp)
     * @param input nlohmann::json value to convert
     * @return Equivalent Json::Value
     *
     * Requires both <nlohmann/json.hpp> and <json/json.h> to be included
     * by the translation unit. This is intentionally NOT included in
     * common_utils.hpp to avoid forcing all consumers to pull in both
     * JSON libraries. Include this header AND the JSON headers yourself.
     */

} // namespace midnight::util
