#pragma once

#include "midnight/core/common_utils.hpp"
#include <algorithm>
#include <cctype>
#include <string>

namespace midnight::production::validation
{
    inline std::string normalized_hex(std::string value)
    {
        value = midnight::util::strip_hex_prefix(value);
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    inline bool is_fixed_hex(const std::string &value, size_t bytes)
    {
        const auto hex = normalized_hex(value);
        return hex.size() == bytes * 2 && midnight::util::is_hex_string(hex);
    }

    inline bool is_tx_hash(const std::string &value)
    {
        return is_fixed_hex(value, 32);
    }

    inline bool is_contract_address_hex(const std::string &value)
    {
        return is_fixed_hex(value, 32);
    }

    inline bool is_token_type_hex(const std::string &value)
    {
        return is_fixed_hex(value, 32);
    }

    inline bool is_night_token_type(const std::string &value)
    {
        return normalized_hex(value) ==
               "0000000000000000000000000000000000000000000000000000000000000000";
    }

    inline bool is_unshielded_night_address(const std::string &value)
    {
        return value.rfind("mn_addr_", 0) == 0;
    }

    inline bool is_dust_address(const std::string &value)
    {
        return value.rfind("mn_dust_", 0) == 0;
    }

    inline bool is_shielded_address(const std::string &value)
    {
        return value.rfind("mn_shield-addr_", 0) == 0;
    }

    inline bool is_u128_decimal(const std::string &value)
    {
        static constexpr const char *kMaxU128 =
            "340282366920938463463374607431768211455";

        if (value.empty())
        {
            return false;
        }
        if (!std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            }))
        {
            return false;
        }
        std::string normalized = value;
        normalized.erase(
            normalized.begin(),
            std::find_if(normalized.begin(), normalized.end(), [](char ch) {
                return ch != '0';
            }));
        if (normalized.empty())
        {
            return true;
        }
        const std::string max(kMaxU128);
        return normalized.size() < max.size() ||
               (normalized.size() == max.size() && normalized <= max);
    }

} // namespace midnight::production::validation
