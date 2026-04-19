#include "midnight/blockchain/official_sdk_bridge.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace midnight::blockchain
{
    namespace
    {
        std::string quote_shell_arg(const std::string &value)
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

        std::string run_command_capture(const std::string &command, int &exit_code)
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

        void copy_string_to_c_buffer(const std::string &input, char *output, size_t capacity)
        {
            if (output == nullptr || capacity == 0)
            {
                return;
            }

            const size_t copy_len = std::min(input.size(), capacity - 1);
            std::memcpy(output, input.data(), copy_len);
            output[copy_len] = '\0';
        }

        std::string extract_json_payload(const std::string &raw_output)
        {
            const auto first = raw_output.find('{');
            const auto last = raw_output.rfind('}');
            if (first == std::string::npos || last == std::string::npos || first > last)
            {
                return raw_output;
            }
            return raw_output.substr(first, last - first + 1);
        }
    } // namespace

    OfficialAddressResult generate_official_wallet_addresses(
        const std::string &network,
        const std::string &seed_hex,
        uint32_t account,
        uint32_t address_index,
        bool register_dust,
        const std::string &proof_server,
        uint32_t sync_timeout_seconds,
        const std::string &bridge_script)
    {
        OfficialAddressResult result;

        std::ostringstream cmd;
        cmd << "node " << quote_shell_arg(bridge_script)
            << " --network " << quote_shell_arg(network)
            << " --account " << account
            << " --index " << address_index;

        if (!seed_hex.empty())
        {
            cmd << " --seed " << quote_shell_arg(seed_hex);
        }

        if (register_dust)
        {
            cmd << " --register-dust";
            cmd << " --sync-timeout " << sync_timeout_seconds;
            if (!proof_server.empty())
            {
                cmd << " --proof-server " << quote_shell_arg(proof_server);
            }
        }

        cmd << " 2>&1";

        int exit_code = 0;
        const std::string raw_output = run_command_capture(cmd.str(), exit_code);
        if (raw_output.empty())
        {
            result.error = "No output from official wallet bridge script";
            return result;
        }

        try
        {
            const std::string json_payload = extract_json_payload(raw_output);
            auto j = nlohmann::json::parse(json_payload);

            result.success = j.value("success", false);
            if (!result.success)
            {
                result.error = j.value("error", "Official wallet bridge failed");
                if (exit_code != 0)
                {
                    result.error += " (exit code " + std::to_string(exit_code) + ")";
                }
                return result;
            }

            result.unshielded_address = j.value("unshieldedAddress", "");
            if (result.unshielded_address.empty())
            {
                result.error = "Official wallet bridge did not return unshieldedAddress";
                return result;
            }

            result.shielded_address = j.value("shieldedAddress", "");
            result.dust_address = j.value("dustAddress", "");
            result.seed_hex = j.value("seedHex", "");
            result.dust_registration_requested = j.value("dustRegistrationRequested", false);
            result.dust_registration_attempted = j.value("dustRegistrationAttempted", false);
            result.dust_registration_success = j.value("dustRegistrationSuccess", false);
            result.dust_registration_message = j.value("dustRegistrationMessage", "");
            result.dust_registration_txid = j.value("dustRegistrationTxId", "");
        }
        catch (const nlohmann::json::exception &e)
        {
            result.error = "Failed to parse JSON response: " + std::string(e.what()) + ". Raw output: " + raw_output;
        }

        return result;
    }

    OfficialAddressResult generate_official_unshielded_address(
        const std::string &network,
        const std::string &seed_hex,
        uint32_t account,
        uint32_t address_index,
        const std::string &bridge_script)
    {
        return generate_official_wallet_addresses(
            network,
            seed_hex,
            account,
            address_index,
            false,
            "",
            120,
            bridge_script);
    }

    OfficialTransferResult transfer_official_night(
        const std::string &seed_hex,
        const std::string &to_address,
        const std::string &amount,
        const std::string &network,
        const std::string &proof_server,
        uint32_t sync_timeout_seconds,
        const std::string &bridge_script)
    {
        OfficialTransferResult result;
        result.network = network;
        result.destination_address = to_address;
        result.amount = amount;

        if (seed_hex.empty())
        {
            result.error = "seed_hex is required for official transfer";
            return result;
        }

        if (to_address.empty())
        {
            result.error = "to_address is required for official transfer";
            return result;
        }

        if (amount.empty())
        {
            result.error = "amount is required for official transfer";
            return result;
        }

        std::ostringstream cmd;
        cmd << "node " << quote_shell_arg(bridge_script)
            << " --network " << quote_shell_arg(network)
            << " --seed " << quote_shell_arg(seed_hex)
            << " --to " << quote_shell_arg(to_address)
            << " --amount " << quote_shell_arg(amount)
            << " --sync-timeout " << sync_timeout_seconds;

        if (!proof_server.empty())
        {
            cmd << " --proof-server " << quote_shell_arg(proof_server);
        }

        cmd << " 2>&1";

        int exit_code = 0;
        const std::string raw_output = run_command_capture(cmd.str(), exit_code);
        if (raw_output.empty())
        {
            result.error = "No output from official transfer bridge script";
            return result;
        }

        try
        {
            const std::string json_payload = extract_json_payload(raw_output);
            auto j = nlohmann::json::parse(json_payload);

            result.success = j.value("success", false);

            result.network = j.value("network", result.network);
            result.source_address = j.value("sourceAddress", "");
            result.destination_address = j.value("destinationAddress", result.destination_address);
            result.amount = j.value("amount", result.amount);
            result.synced = j.value("synced", false);
            result.sync_error = j.value("syncError", "");
            result.txid = j.value("txId", "");

            result.unshielded_balance_before = j.value("unshieldedBalanceBefore", j.value("unshieldedBalance", ""));
            result.dust_balance_before = j.value("dustBalanceBefore", j.value("dustBalance", ""));

            result.error = j.value("error", "");

            if (!result.success && result.error.empty())
            {
                result.error = "Official transfer bridge failed";
            }
            if (!result.success && exit_code != 0)
            {
                result.error += " (exit code " + std::to_string(exit_code) + ")";
            }
        }
        catch (const nlohmann::json::exception &e)
        {
            result.error = "Failed to parse JSON response: " + std::string(e.what()) + ". Raw output: " + raw_output;
            result.success = false;
        }

        return result;
    }

} // namespace midnight::blockchain

extern "C"
{
    int midnight_generate_official_unshielded_address(
        const char *network,
        const char *seed_hex,
        char *out_address,
        size_t out_address_capacity,
        char *out_seed_hex,
        size_t out_seed_hex_capacity,
        char *out_error,
        size_t out_error_capacity)
    {
        const std::string network_value = (network != nullptr && network[0] != '\0') ? network : "preprod";
        const std::string seed_value = (seed_hex != nullptr) ? seed_hex : "";

        const auto result = midnight::blockchain::generate_official_unshielded_address(
            network_value,
            seed_value,
            0,
            0,
            "tools/official-wallet-address.mjs");

        if (result.success)
        {
            midnight::blockchain::copy_string_to_c_buffer(result.unshielded_address, out_address, out_address_capacity);
            midnight::blockchain::copy_string_to_c_buffer(result.seed_hex, out_seed_hex, out_seed_hex_capacity);
            midnight::blockchain::copy_string_to_c_buffer("", out_error, out_error_capacity);
            return 0;
        }

        midnight::blockchain::copy_string_to_c_buffer("", out_address, out_address_capacity);
        midnight::blockchain::copy_string_to_c_buffer("", out_seed_hex, out_seed_hex_capacity);
        midnight::blockchain::copy_string_to_c_buffer(result.error, out_error, out_error_capacity);
        return 1;
    }

    int midnight_transfer_official_night(
        const char *network,
        const char *seed_hex,
        const char *to_address,
        const char *amount,
        const char *proof_server,
        uint32_t sync_timeout_seconds,
        char *out_txid,
        size_t out_txid_capacity,
        char *out_error,
        size_t out_error_capacity)
    {
        const std::string network_value = (network != nullptr && network[0] != '\0') ? network : "preprod";
        const std::string seed_value = (seed_hex != nullptr) ? seed_hex : "";
        const std::string to_value = (to_address != nullptr) ? to_address : "";
        const std::string amount_value = (amount != nullptr) ? amount : "";
        const std::string proof_value = (proof_server != nullptr) ? proof_server : "";
        const uint32_t timeout_value = (sync_timeout_seconds == 0U) ? 120U : sync_timeout_seconds;

        const auto result = midnight::blockchain::transfer_official_night(
            seed_value,
            to_value,
            amount_value,
            network_value,
            proof_value,
            timeout_value,
            "tools/official-transfer-night.mjs");

        if (result.success)
        {
            midnight::blockchain::copy_string_to_c_buffer(result.txid, out_txid, out_txid_capacity);
            midnight::blockchain::copy_string_to_c_buffer("", out_error, out_error_capacity);
            return 0;
        }

        midnight::blockchain::copy_string_to_c_buffer("", out_txid, out_txid_capacity);
        midnight::blockchain::copy_string_to_c_buffer(result.error, out_error, out_error_capacity);
        return 1;
    }
}
