#pragma once

#include "midnight/core/common_utils.hpp"
#include "midnight/wallet/bech32m.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

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
        try
        {
            midnight::wallet::address::decode_unshielded(value);
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    inline bool is_dust_address(const std::string &value)
    {
        try
        {
            midnight::wallet::address::decode_dust(value);
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    inline bool is_shielded_address(const std::string &value)
    {
        try
        {
            midnight::wallet::address::decode_shielded(value);
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
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

    inline bool has_source_input(
        const std::string &src_url,
        const std::vector<std::string> &src_files)
    {
        return !src_url.empty() || !src_files.empty();
    }

    inline bool is_wallet_seed_or_mnemonic(const std::string &value)
    {
        const auto hex = normalized_hex(value);
        if ((hex.size() == 32 || hex.size() == 64 || hex.size() == 128) &&
            midnight::util::is_hex_string(hex))
        {
            return true;
        }

        std::istringstream words(value);
        std::string word;
        size_t count = 0;
        while (words >> word)
        {
            if (!std::all_of(word.begin(), word.end(), [](unsigned char ch) {
                    return std::islower(ch) != 0;
                }))
            {
                return false;
            }
            ++count;
        }
        return count == 12 || count == 15 || count == 18 || count == 21 || count == 24;
    }

} // namespace midnight::production::validation
