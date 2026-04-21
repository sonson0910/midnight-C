#include "midnight/blockchain/official_sdk_bridge.hpp"
#include "midnight/blockchain/wallet_manager.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
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

        uint64_t read_json_uint64(const nlohmann::json &value, uint64_t fallback = 0)
        {
            try
            {
                if (value.is_number_unsigned())
                {
                    return value.get<uint64_t>();
                }

                if (value.is_number_integer())
                {
                    const auto as_signed = value.get<int64_t>();
                    if (as_signed >= 0)
                    {
                        return static_cast<uint64_t>(as_signed);
                    }
                    return fallback;
                }

                if (value.is_string())
                {
                    const std::string raw = value.get<std::string>();
                    if (raw.empty())
                    {
                        return fallback;
                    }

                    size_t parsed = 0;
                    const unsigned long long parsed_value = std::stoull(raw, &parsed, 10);
                    if (parsed != raw.size())
                    {
                        return fallback;
                    }

                    if (parsed_value > static_cast<unsigned long long>(std::numeric_limits<uint64_t>::max()))
                    {
                        return fallback;
                    }

                    return static_cast<uint64_t>(parsed_value);
                }
            }
            catch (...)
            {
                return fallback;
            }

            return fallback;
        }

        uint64_t read_json_uint64_field(const nlohmann::json &object, const char *key, uint64_t fallback = 0)
        {
            if (!object.is_object() || !object.contains(key))
            {
                return fallback;
            }

            return read_json_uint64(object.at(key), fallback);
        }

        bool read_json_bool(const nlohmann::json &value, bool fallback = false)
        {
            try
            {
                if (value.is_boolean())
                {
                    return value.get<bool>();
                }

                if (value.is_number_integer())
                {
                    return value.get<int64_t>() != 0;
                }

                if (value.is_number_unsigned())
                {
                    return value.get<uint64_t>() != 0;
                }

                if (value.is_string())
                {
                    std::string raw = value.get<std::string>();
                    std::transform(raw.begin(), raw.end(), raw.begin(),
                                   [](unsigned char ch)
                                   { return static_cast<char>(std::tolower(ch)); });

                    if (raw == "true" || raw == "1" || raw == "yes")
                    {
                        return true;
                    }
                    if (raw == "false" || raw == "0" || raw == "no")
                    {
                        return false;
                    }
                }
            }
            catch (...)
            {
                return fallback;
            }

            return fallback;
        }

        bool read_json_bool_field(const nlohmann::json &object, const char *key, bool fallback = false)
        {
            if (!object.is_object() || !object.contains(key))
            {
                return fallback;
            }

            return read_json_bool(object.at(key), fallback);
        }

        std::string read_json_string_field(const nlohmann::json &object, const char *key, const std::string &fallback = "")
        {
            if (!object.is_object() || !object.contains(key))
            {
                return fallback;
            }

            const auto &value = object.at(key);
            if (value.is_string())
            {
                return value.get<std::string>();
            }

            return fallback;
        }

        std::string read_env_or_default(const char *env_name, const char *fallback)
        {
            if (env_name != nullptr)
            {
                const char *env_value = std::getenv(env_name);
                if (env_value != nullptr && env_value[0] != '\0')
                {
                    return std::string(env_value);
                }
            }

            return (fallback != nullptr) ? std::string(fallback) : std::string();
        }

        bool normalize_seed_hex(const std::string &seed_hex, std::string &normalized)
        {
            std::string raw = seed_hex;
            if (raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'))
            {
                raw = raw.substr(2);
            }

            if (raw.size() != 64)
            {
                return false;
            }

            for (char &ch : raw)
            {
                if (!std::isxdigit(static_cast<unsigned char>(ch)))
                {
                    return false;
                }

                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }

            normalized = std::move(raw);
            return true;
        }

        class ScopedTempFile
        {
        public:
            explicit ScopedTempFile(std::filesystem::path path)
                : path_(std::move(path))
            {
            }

            ~ScopedTempFile()
            {
                std::error_code ec;
                if (!path_.empty())
                {
                    std::filesystem::remove(path_, ec);
                }
            }

            ScopedTempFile(const ScopedTempFile &) = delete;
            ScopedTempFile &operator=(const ScopedTempFile &) = delete;

            ScopedTempFile(ScopedTempFile &&other) noexcept
                : path_(std::move(other.path_))
            {
                other.path_.clear();
            }

            ScopedTempFile &operator=(ScopedTempFile &&other) noexcept
            {
                if (this != &other)
                {
                    std::error_code ec;
                    if (!path_.empty())
                    {
                        std::filesystem::remove(path_, ec);
                    }
                    path_ = std::move(other.path_);
                    other.path_.clear();
                }
                return *this;
            }

            const std::filesystem::path &path() const
            {
                return path_;
            }

        private:
            std::filesystem::path path_;
        };

        bool write_seed_hex_temp_file(const std::string &seed_hex, std::filesystem::path &seed_file_path, std::string &error)
        {
            std::string normalized_seed;
            if (!normalize_seed_hex(seed_hex, normalized_seed))
            {
                error = "Seed must be exactly 32 bytes (64 hex chars, optional 0x prefix)";
                return false;
            }

            std::error_code ec;
            std::filesystem::path temp_dir = std::filesystem::temp_directory_path(ec);
            if (ec)
            {
                ec.clear();
                temp_dir = std::filesystem::current_path(ec);
                if (ec)
                {
                    error = "Failed to resolve temporary directory for seed file";
                    return false;
                }
            }

            const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            bool written = false;

            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const std::filesystem::path candidate =
                    temp_dir / ("midnight-seed-" + std::to_string(now) + "-" + std::to_string(attempt) + ".tmp");

                if (std::filesystem::exists(candidate, ec))
                {
                    ec.clear();
                    continue;
                }

                std::ofstream out(candidate, std::ios::out | std::ios::trunc | std::ios::binary);
                if (!out)
                {
                    continue;
                }

                out << normalized_seed;
                if (!out.good())
                {
                    out.close();
                    std::filesystem::remove(candidate, ec);
                    error = "Failed to write seed to temporary file";
                    return false;
                }
                out.close();

#ifndef _WIN32
                std::filesystem::permissions(
                    candidate,
                    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::replace,
                    ec);
                if (ec)
                {
                    std::filesystem::remove(candidate, ec);
                    error = "Failed to set secure permissions on temporary seed file";
                    return false;
                }
#endif

                seed_file_path = candidate;
                written = true;
                break;
            }

            if (!written)
            {
                error = "Failed to create temporary seed file";
                return false;
            }

            error.clear();
            return true;
        }

        std::optional<std::string> load_seed_hex_from_wallet_alias(
            const std::string &wallet_alias,
            const std::string &network,
            const std::string &wallet_store_dir,
            std::string &error)
        {
            std::string manager_error;
            const auto wallet_entry = WalletManager::load_wallet(wallet_alias, wallet_store_dir, &manager_error);
            if (!wallet_entry.has_value())
            {
                error = manager_error.empty() ? ("Wallet alias not found: " + wallet_alias) : manager_error;
                return std::nullopt;
            }

            if (!network.empty() && !wallet_entry->network.empty() && wallet_entry->network != network)
            {
                error = "Wallet alias '" + wallet_alias + "' is bound to network '" + wallet_entry->network +
                        "' but requested network is '" + network + "'";
                return std::nullopt;
            }

            error.clear();
            return wallet_entry->seed_hex;
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

        std::filesystem::path seed_file_path;
        if (!write_seed_hex_temp_file(seed_hex, seed_file_path, result.error))
        {
            return result;
        }
        const ScopedTempFile seed_file_guard(seed_file_path);

        std::ostringstream cmd;
        cmd << "node " << quote_shell_arg(bridge_script)
            << " --network " << quote_shell_arg(network)
            << " --seed-file " << quote_shell_arg(seed_file_guard.path().string())
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

    bool register_wallet_seed_hex(
        const std::string &wallet_alias,
        const std::string &seed_hex,
        const std::string &network,
        const std::string &wallet_store_dir,
        std::string *error)
    {
        return WalletManager::save_seed_hex(wallet_alias, seed_hex, network, wallet_store_dir, error);
    }

    OfficialTransferResult transfer_official_night_with_wallet(
        const std::string &wallet_alias,
        const std::string &to_address,
        const std::string &amount,
        const std::string &network,
        const std::string &proof_server,
        uint32_t sync_timeout_seconds,
        const std::string &wallet_store_dir,
        const std::string &bridge_script)
    {
        OfficialTransferResult result;
        result.network = network;
        result.destination_address = to_address;
        result.amount = amount;

        std::string seed_error;
        const auto seed_hex = load_seed_hex_from_wallet_alias(wallet_alias, network, wallet_store_dir, seed_error);
        if (!seed_hex.has_value())
        {
            result.error = seed_error.empty() ? "Failed to resolve wallet alias" : seed_error;
            return result;
        }

        return transfer_official_night(
            *seed_hex,
            to_address,
            amount,
            network,
            proof_server,
            sync_timeout_seconds,
            bridge_script);
    }

    OfficialWalletStateResult query_official_wallet_state(
        const std::string &seed_hex,
        const std::string &network,
        const std::string &proof_server,
        const std::string &node_url,
        const std::string &indexer_url,
        const std::string &indexer_ws_url,
        uint32_t sync_timeout_seconds,
        const std::string &bridge_script)
    {
        OfficialWalletStateResult result;
        result.network = network;

        if (seed_hex.empty())
        {
            result.error = "seed_hex is required for official wallet state query";
            return result;
        }

        std::filesystem::path seed_file_path;
        if (!write_seed_hex_temp_file(seed_hex, seed_file_path, result.error))
        {
            return result;
        }
        const ScopedTempFile seed_file_guard(seed_file_path);

        std::ostringstream cmd;
        cmd << "node " << quote_shell_arg(bridge_script)
            << " --state-only"
            << " --network " << quote_shell_arg(network)
            << " --seed-file " << quote_shell_arg(seed_file_guard.path().string())
            << " --sync-timeout " << sync_timeout_seconds
            << " --no-auto-register-dust"
            << " --submit-retries 0"
            << " --dust-deregister-retries 0";

        if (!proof_server.empty())
        {
            cmd << " --proof-server " << quote_shell_arg(proof_server);
        }
        if (!node_url.empty())
        {
            cmd << " --node-url " << quote_shell_arg(node_url);
        }
        if (!indexer_url.empty())
        {
            cmd << " --indexer-url " << quote_shell_arg(indexer_url);
        }
        if (!indexer_ws_url.empty())
        {
            cmd << " --indexer-ws-url " << quote_shell_arg(indexer_ws_url);
        }

        cmd << " 2>&1";

        int exit_code = 0;
        const std::string raw_output = run_command_capture(cmd.str(), exit_code);
        if (raw_output.empty())
        {
            result.error = "No output from official wallet state bridge script";
            return result;
        }

        try
        {
            const std::string json_payload = extract_json_payload(raw_output);
            auto j = nlohmann::json::parse(json_payload);

            result.success = j.value("success", false);
            result.network = j.value("network", result.network);
            result.source_address = j.value("sourceAddress", "");
            result.synced = j.value("synced", false);
            result.sync_error = j.value("syncError", "");
            result.unshielded_balance = j.value("unshieldedBalance", "");
            result.pending_unshielded_balance = j.value("pendingUnshieldedBalance", "");
            result.dust_balance = j.value("dustBalance", "");
            result.dust_balance_error = j.value("dustBalanceError", "");
            result.dust_pending_balance = j.value("dustPendingBalance", "");
            result.dust_available_coins = read_json_uint64_field(j, "dustAvailableCoins", 0);
            result.dust_pending_coins = read_json_uint64_field(j, "dustPendingCoins", 0);
            result.available_utxo_count = read_json_uint64_field(j, "availableUtxoCount", 0);
            result.registered_utxo_count = read_json_uint64_field(j, "registeredUtxoCount", 0);
            result.unregistered_utxo_count = read_json_uint64_field(j, "unregisteredUtxoCount", 0);
            result.pending_utxo_count = read_json_uint64_field(j, "pendingUtxoCount", 0);
            result.likely_dust_designation_lock = j.value("likelyDustDesignationLock", false);
            result.likely_pending_utxo_lock = j.value("likelyPendingUtxoLock", false);

            result.error = j.value("error", "");
            if (!result.success && result.error.empty())
            {
                result.error = "Official wallet state bridge failed";
            }

            if (!result.success && exit_code != 0)
            {
                if (!result.error.empty())
                {
                    result.error += " ";
                }
                result.error += "(exit code " + std::to_string(exit_code) + ")";
            }
        }
        catch (const nlohmann::json::exception &e)
        {
            result.error = "Failed to parse JSON response: " + std::string(e.what()) + ". Raw output: " + raw_output;
            result.success = false;
        }

        return result;
    }

    OfficialWalletStateResult query_official_wallet_state_with_wallet(
        const std::string &wallet_alias,
        const std::string &network,
        const std::string &proof_server,
        const std::string &node_url,
        const std::string &indexer_url,
        const std::string &indexer_ws_url,
        uint32_t sync_timeout_seconds,
        const std::string &wallet_store_dir,
        const std::string &bridge_script)
    {
        OfficialWalletStateResult result;
        result.network = network;

        std::string seed_error;
        const auto seed_hex = load_seed_hex_from_wallet_alias(wallet_alias, network, wallet_store_dir, seed_error);
        if (!seed_hex.has_value())
        {
            result.error = seed_error.empty() ? "Failed to resolve wallet alias" : seed_error;
            return result;
        }

        return query_official_wallet_state(
            *seed_hex,
            network,
            proof_server,
            node_url,
            indexer_url,
            indexer_ws_url,
            sync_timeout_seconds,
            bridge_script);
    }

    OfficialIndexerSyncResult query_official_indexer_sync_status(
        const std::string &seed_hex,
        const std::string &network,
        const std::string &proof_server,
        const std::string &node_url,
        const std::string &indexer_url,
        const std::string &indexer_ws_url,
        uint32_t sync_timeout_seconds,
        const std::string &bridge_script)
    {
        OfficialIndexerSyncResult result;
        result.network = network;
        result.indexer_url = indexer_url;
        result.indexer_ws_url = indexer_ws_url;

        if (seed_hex.empty())
        {
            result.error = "seed_hex is required for official indexer sync query";
            return result;
        }

        std::filesystem::path seed_file_path;
        if (!write_seed_hex_temp_file(seed_hex, seed_file_path, result.error))
        {
            return result;
        }
        const ScopedTempFile seed_file_guard(seed_file_path);

        std::ostringstream cmd;
        cmd << "node " << quote_shell_arg(bridge_script)
            << " --state-only"
            << " --network " << quote_shell_arg(network)
            << " --seed-file " << quote_shell_arg(seed_file_guard.path().string())
            << " --sync-timeout " << sync_timeout_seconds
            << " --no-auto-register-dust"
            << " --submit-retries 0"
            << " --dust-deregister-retries 0";

        if (!proof_server.empty())
        {
            cmd << " --proof-server " << quote_shell_arg(proof_server);
        }
        if (!node_url.empty())
        {
            cmd << " --node-url " << quote_shell_arg(node_url);
        }
        if (!indexer_url.empty())
        {
            cmd << " --indexer-url " << quote_shell_arg(indexer_url);
        }
        if (!indexer_ws_url.empty())
        {
            cmd << " --indexer-ws-url " << quote_shell_arg(indexer_ws_url);
        }

        cmd << " 2>&1";

        int exit_code = 0;
        const std::string raw_output = run_command_capture(cmd.str(), exit_code);
        if (raw_output.empty())
        {
            result.error = "No output from official indexer sync bridge script";
            return result;
        }

        try
        {
            const std::string json_payload = extract_json_payload(raw_output);
            auto j = nlohmann::json::parse(json_payload);

            result.success = j.value("success", false);
            result.network = j.value("network", result.network);
            result.source_address = j.value("sourceAddress", "");
            result.sync_error = j.value("syncError", "");
            result.error = j.value("error", "");

            const nlohmann::json sync_diagnostics =
                (j.contains("syncDiagnostics") && j.at("syncDiagnostics").is_object())
                    ? j.at("syncDiagnostics")
                    : nlohmann::json::object();

            const nlohmann::json shielded =
                (sync_diagnostics.contains("shielded") && sync_diagnostics.at("shielded").is_object())
                    ? sync_diagnostics.at("shielded")
                    : nlohmann::json::object();

            const nlohmann::json unshielded =
                (sync_diagnostics.contains("unshielded") && sync_diagnostics.at("unshielded").is_object())
                    ? sync_diagnostics.at("unshielded")
                    : nlohmann::json::object();

            const nlohmann::json dust =
                (sync_diagnostics.contains("dust") && sync_diagnostics.at("dust").is_object())
                    ? sync_diagnostics.at("dust")
                    : nlohmann::json::object();

            result.facade_is_synced = read_json_bool_field(sync_diagnostics, "facadeIsSynced", false);

            result.shielded_connected = read_json_bool_field(shielded, "isConnected", false);
            result.unshielded_connected = read_json_bool_field(unshielded, "isConnected", false);
            result.dust_connected = read_json_bool_field(dust, "isConnected", false);

            result.shielded_complete_within_50 = read_json_bool_field(shielded, "isCompleteWithin50", false);
            result.unshielded_complete_within_50 = read_json_bool_field(unshielded, "isCompleteWithin50", false);
            result.dust_complete_within_50 = read_json_bool_field(dust, "isCompleteWithin50", false);

            result.shielded_apply_lag = read_json_string_field(shielded, "applyLag", "");
            result.unshielded_apply_lag = read_json_string_field(unshielded, "applyLag", "");
            result.dust_apply_lag = read_json_string_field(dust, "applyLag", "");

            result.all_connected = result.shielded_connected && result.unshielded_connected && result.dust_connected;

            if (!result.success && result.error.empty())
            {
                result.error = "Official indexer sync bridge failed";
            }

            if (!result.success && exit_code != 0)
            {
                if (!result.error.empty())
                {
                    result.error += " ";
                }
                result.error += "(exit code " + std::to_string(exit_code) + ")";
            }
        }
        catch (const nlohmann::json::exception &e)
        {
            result.error = "Failed to parse JSON response: " + std::string(e.what()) + ". Raw output: " + raw_output;
            result.success = false;
        }

        return result;
    }

    OfficialIndexerSyncResult query_official_indexer_sync_status_with_wallet(
        const std::string &wallet_alias,
        const std::string &network,
        const std::string &proof_server,
        const std::string &node_url,
        const std::string &indexer_url,
        const std::string &indexer_ws_url,
        uint32_t sync_timeout_seconds,
        const std::string &wallet_store_dir,
        const std::string &bridge_script)
    {
        OfficialIndexerSyncResult result;
        result.network = network;
        result.indexer_url = indexer_url;
        result.indexer_ws_url = indexer_ws_url;

        std::string seed_error;
        const auto seed_hex = load_seed_hex_from_wallet_alias(wallet_alias, network, wallet_store_dir, seed_error);
        if (!seed_hex.has_value())
        {
            result.error = seed_error.empty() ? "Failed to resolve wallet alias" : seed_error;
            return result;
        }

        return query_official_indexer_sync_status(
            *seed_hex,
            network,
            proof_server,
            node_url,
            indexer_url,
            indexer_ws_url,
            sync_timeout_seconds,
            bridge_script);
    }

    OfficialContractDeployResult deploy_official_compact_contract(
        const std::string &contract,
        const std::string &network,
        const std::string &seed_hex,
        uint64_t fee_bps,
        const std::string &initial_nonce_hex,
        const std::string &proof_server,
        const std::string &node_url,
        const std::string &indexer_url,
        const std::string &indexer_ws_url,
        uint32_t sync_timeout_seconds,
        uint32_t dust_wait_seconds,
        uint32_t deploy_timeout_seconds,
        uint32_t dust_utxo_limit,
        const std::string &contract_module,
        const std::string &contract_export,
        const std::string &private_state_store,
        const std::string &private_state_id,
        const std::string &constructor_args_json,
        const std::string &constructor_args_file,
        const std::string &zk_config_path,
        const std::string &bridge_script)
    {
        OfficialContractDeployResult result;
        result.contract = contract;
        result.network = network;

        std::optional<ScopedTempFile> seed_file_guard;
        if (!seed_hex.empty())
        {
            std::filesystem::path seed_file_path;
            if (!write_seed_hex_temp_file(seed_hex, seed_file_path, result.error))
            {
                return result;
            }
            seed_file_guard.emplace(seed_file_path);
        }

        std::ostringstream cmd;
        cmd << "node " << quote_shell_arg(bridge_script)
            << " --contract " << quote_shell_arg(contract)
            << " --network " << quote_shell_arg(network)
            << " --fee-bps " << fee_bps
            << " --sync-timeout " << sync_timeout_seconds
            << " --dust-wait " << dust_wait_seconds
            << " --deploy-timeout " << deploy_timeout_seconds
            << " --dust-utxo-limit " << dust_utxo_limit;

        if (seed_file_guard.has_value())
        {
            cmd << " --seed-file " << quote_shell_arg(seed_file_guard->path().string());
        }
        if (!initial_nonce_hex.empty())
        {
            cmd << " --initial-nonce " << quote_shell_arg(initial_nonce_hex);
        }
        if (!proof_server.empty())
        {
            cmd << " --proof-server " << quote_shell_arg(proof_server);
        }
        if (!node_url.empty())
        {
            cmd << " --node-url " << quote_shell_arg(node_url);
        }
        if (!indexer_url.empty())
        {
            cmd << " --indexer-url " << quote_shell_arg(indexer_url);
        }
        if (!indexer_ws_url.empty())
        {
            cmd << " --indexer-ws-url " << quote_shell_arg(indexer_ws_url);
        }
        if (!contract_module.empty())
        {
            cmd << " --contract-module " << quote_shell_arg(contract_module);
        }
        if (!contract_export.empty())
        {
            cmd << " --contract-export " << quote_shell_arg(contract_export);
        }
        if (!private_state_store.empty())
        {
            cmd << " --private-state-store " << quote_shell_arg(private_state_store);
        }
        if (!private_state_id.empty())
        {
            cmd << " --private-state-id " << quote_shell_arg(private_state_id);
        }
        if (!constructor_args_json.empty())
        {
            cmd << " --constructor-args-json " << quote_shell_arg(constructor_args_json);
        }
        if (!constructor_args_file.empty())
        {
            cmd << " --constructor-args-file " << quote_shell_arg(constructor_args_file);
        }
        if (!zk_config_path.empty())
        {
            cmd << " --zk-config-path " << quote_shell_arg(zk_config_path);
        }

        cmd << " 2>&1";

        int exit_code = 0;
        const std::string raw_output = run_command_capture(cmd.str(), exit_code);
        if (raw_output.empty())
        {
            result.error = "No output from official compact deploy bridge script";
            return result;
        }

        try
        {
            const std::string json_payload = extract_json_payload(raw_output);
            auto j = nlohmann::json::parse(json_payload);

            result.success = j.value("success", false);
            result.network = j.value("network", result.network);
            result.contract = j.value("contract", result.contract);
            result.contract_export = j.value("contractExport", "");
            result.contract_module = j.value("contractModule", "");
            result.seed_hex = j.value("seedHex", "");
            result.source_address = j.value("sourceAddress", "");
            result.fee_bps = j.value("feeBps", "");
            result.constructor_nonce_hex = j.value("constructorNonceHex", "");
            result.contract_address = j.value("contractAddress", "");
            result.txid = j.value("txId", "");
            result.unshielded_balance = j.value("unshieldedBalance", "");
            result.dust_balance = j.value("dustBalance", "");

            if (j.contains("dustRegistration") && j.at("dustRegistration").is_object())
            {
                const auto &dust = j.at("dustRegistration");
                result.dust_registration_attempted = dust.value("attempted", false);
                result.dust_registration_success = dust.value("success", false);
                result.dust_registration_registered_utxos = read_json_uint64_field(dust, "registeredUtxos", 0);
                result.dust_registration_message = dust.value("message", "");
            }

            if (j.contains("endpoints") && j.at("endpoints").is_object())
            {
                const auto &endpoints = j.at("endpoints");
                result.endpoints_node_url = endpoints.value("nodeUrl", "");
                result.endpoints_indexer_url = endpoints.value("indexerUrl", "");
                result.endpoints_indexer_ws_url = endpoints.value("indexerWsUrl", "");
                result.endpoints_proof_server = endpoints.value("proofServer", "");
            }

            result.error = j.value("error", "");
            if (!result.success && result.error.empty())
            {
                result.error = "Official compact deploy bridge failed";
            }

            if (result.success && result.contract_address.empty())
            {
                result.success = false;
                result.error = "Official compact deploy bridge did not return contractAddress";
            }

            if (!result.success && exit_code != 0)
            {
                if (!result.error.empty())
                {
                    result.error += " ";
                }
                result.error += "(exit code " + std::to_string(exit_code) + ")";
            }
        }
        catch (const nlohmann::json::exception &e)
        {
            result.error = "Failed to parse JSON response: " + std::string(e.what()) + ". Raw output: " + raw_output;
            result.success = false;
        }

        return result;
    }

    OfficialContractDeployResult deploy_official_compact_contract_with_wallet(
        const std::string &wallet_alias,
        const std::string &contract,
        const std::string &network,
        uint64_t fee_bps,
        const std::string &initial_nonce_hex,
        const std::string &proof_server,
        const std::string &node_url,
        const std::string &indexer_url,
        const std::string &indexer_ws_url,
        uint32_t sync_timeout_seconds,
        uint32_t dust_wait_seconds,
        uint32_t deploy_timeout_seconds,
        uint32_t dust_utxo_limit,
        const std::string &contract_module,
        const std::string &contract_export,
        const std::string &private_state_store,
        const std::string &private_state_id,
        const std::string &constructor_args_json,
        const std::string &constructor_args_file,
        const std::string &zk_config_path,
        const std::string &wallet_store_dir,
        const std::string &bridge_script)
    {
        OfficialContractDeployResult result;
        result.contract = contract;
        result.network = network;

        std::string seed_error;
        const auto seed_hex = load_seed_hex_from_wallet_alias(wallet_alias, network, wallet_store_dir, seed_error);
        if (!seed_hex.has_value())
        {
            result.error = seed_error.empty() ? "Failed to resolve wallet alias" : seed_error;
            return result;
        }

        return deploy_official_compact_contract(
            contract,
            network,
            *seed_hex,
            fee_bps,
            initial_nonce_hex,
            proof_server,
            node_url,
            indexer_url,
            indexer_ws_url,
            sync_timeout_seconds,
            dust_wait_seconds,
            deploy_timeout_seconds,
            dust_utxo_limit,
            contract_module,
            contract_export,
            private_state_store,
            private_state_id,
            constructor_args_json,
            constructor_args_file,
            zk_config_path,
            bridge_script);
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
        const std::string bridge_script = midnight::blockchain::read_env_or_default(
            "MIDNIGHT_OFFICIAL_WALLET_ADDRESS_SCRIPT",
            "tools/official-wallet-address.mjs");

        const auto result = midnight::blockchain::generate_official_unshielded_address(
            network_value,
            seed_value,
            0,
            0,
            bridge_script);

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
        const std::string bridge_script = midnight::blockchain::read_env_or_default(
            "MIDNIGHT_OFFICIAL_TRANSFER_SCRIPT",
            "tools/official-transfer-night.mjs");

        const auto result = midnight::blockchain::transfer_official_night(
            seed_value,
            to_value,
            amount_value,
            network_value,
            proof_value,
            timeout_value,
            bridge_script);

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

    int midnight_query_official_indexer_sync_status(
        const char *network,
        const char *seed_hex,
        const char *proof_server,
        const char *node_url,
        const char *indexer_url,
        const char *indexer_ws_url,
        uint32_t sync_timeout_seconds,
        int *out_facade_synced,
        int *out_all_connected,
        char *out_source_address,
        size_t out_source_address_capacity,
        char *out_sync_error,
        size_t out_sync_error_capacity,
        char *out_diagnostics_json,
        size_t out_diagnostics_json_capacity,
        char *out_error,
        size_t out_error_capacity)
    {
        const std::string network_value = (network != nullptr && network[0] != '\0') ? network : "preprod";
        const std::string seed_value = (seed_hex != nullptr) ? seed_hex : "";
        const std::string proof_value = (proof_server != nullptr) ? proof_server : "";
        const std::string node_value = (node_url != nullptr) ? node_url : "";
        const std::string indexer_value = (indexer_url != nullptr) ? indexer_url : "";
        const std::string indexer_ws_value = (indexer_ws_url != nullptr) ? indexer_ws_url : "";
        const uint32_t timeout_value = (sync_timeout_seconds == 0U) ? 120U : sync_timeout_seconds;
        const std::string bridge_script = midnight::blockchain::read_env_or_default(
            "MIDNIGHT_OFFICIAL_TRANSFER_SCRIPT",
            "tools/official-transfer-night.mjs");

        const auto result = midnight::blockchain::query_official_indexer_sync_status(
            seed_value,
            network_value,
            proof_value,
            node_value,
            indexer_value,
            indexer_ws_value,
            timeout_value,
            bridge_script);

        if (out_facade_synced != nullptr)
        {
            *out_facade_synced = result.facade_is_synced ? 1 : 0;
        }

        if (out_all_connected != nullptr)
        {
            *out_all_connected = result.all_connected ? 1 : 0;
        }

        if (result.success)
        {
            nlohmann::json diagnostics = {
                {"network", result.network},
                {"sourceAddress", result.source_address},
                {"syncError", result.sync_error},
                {"indexerUrl", result.indexer_url},
                {"indexerWsUrl", result.indexer_ws_url},
                {"facadeIsSynced", result.facade_is_synced},
                {"allConnected", result.all_connected},
                {"shielded", {
                                 {"isConnected", result.shielded_connected},
                                 {"isCompleteWithin50", result.shielded_complete_within_50},
                                 {"applyLag", result.shielded_apply_lag},
                             }},
                {"unshielded", {
                                   {"isConnected", result.unshielded_connected},
                                   {"isCompleteWithin50", result.unshielded_complete_within_50},
                                   {"applyLag", result.unshielded_apply_lag},
                               }},
                {"dust", {
                             {"isConnected", result.dust_connected},
                             {"isCompleteWithin50", result.dust_complete_within_50},
                             {"applyLag", result.dust_apply_lag},
                         }},
            };

            midnight::blockchain::copy_string_to_c_buffer(result.source_address, out_source_address, out_source_address_capacity);
            midnight::blockchain::copy_string_to_c_buffer(result.sync_error, out_sync_error, out_sync_error_capacity);
            midnight::blockchain::copy_string_to_c_buffer(diagnostics.dump(), out_diagnostics_json, out_diagnostics_json_capacity);
            midnight::blockchain::copy_string_to_c_buffer("", out_error, out_error_capacity);
            return 0;
        }

        midnight::blockchain::copy_string_to_c_buffer("", out_source_address, out_source_address_capacity);
        midnight::blockchain::copy_string_to_c_buffer("", out_sync_error, out_sync_error_capacity);
        midnight::blockchain::copy_string_to_c_buffer("", out_diagnostics_json, out_diagnostics_json_capacity);
        midnight::blockchain::copy_string_to_c_buffer(result.error, out_error, out_error_capacity);
        return 1;
    }

    int midnight_query_official_wallet_state(
        const char *network,
        const char *seed_hex,
        const char *proof_server,
        const char *node_url,
        const char *indexer_url,
        const char *indexer_ws_url,
        uint32_t sync_timeout_seconds,
        int *out_synced,
        uint64_t *out_available_utxo_count,
        uint64_t *out_registered_utxo_count,
        char *out_source_address,
        size_t out_source_address_capacity,
        char *out_state_json,
        size_t out_state_json_capacity,
        char *out_error,
        size_t out_error_capacity)
    {
        const std::string network_value = (network != nullptr && network[0] != '\0') ? network : "preprod";
        const std::string seed_value = (seed_hex != nullptr) ? seed_hex : "";
        const std::string proof_value = (proof_server != nullptr) ? proof_server : "";
        const std::string node_value = (node_url != nullptr) ? node_url : "";
        const std::string indexer_value = (indexer_url != nullptr) ? indexer_url : "";
        const std::string indexer_ws_value = (indexer_ws_url != nullptr) ? indexer_ws_url : "";
        const uint32_t timeout_value = (sync_timeout_seconds == 0U) ? 120U : sync_timeout_seconds;
        const std::string bridge_script = midnight::blockchain::read_env_or_default(
            "MIDNIGHT_OFFICIAL_TRANSFER_SCRIPT",
            "tools/official-transfer-night.mjs");

        const auto result = midnight::blockchain::query_official_wallet_state(
            seed_value,
            network_value,
            proof_value,
            node_value,
            indexer_value,
            indexer_ws_value,
            timeout_value,
            bridge_script);

        if (out_synced != nullptr)
        {
            *out_synced = result.synced ? 1 : 0;
        }

        if (out_available_utxo_count != nullptr)
        {
            *out_available_utxo_count = result.available_utxo_count;
        }

        if (out_registered_utxo_count != nullptr)
        {
            *out_registered_utxo_count = result.registered_utxo_count;
        }

        if (result.success)
        {
            nlohmann::json state_json = {
                {"network", result.network},
                {"sourceAddress", result.source_address},
                {"synced", result.synced},
                {"syncError", result.sync_error},
                {"unshieldedBalance", result.unshielded_balance},
                {"pendingUnshieldedBalance", result.pending_unshielded_balance},
                {"dustBalance", result.dust_balance},
                {"dustBalanceError", result.dust_balance_error},
                {"dustPendingBalance", result.dust_pending_balance},
                {"dustAvailableCoins", result.dust_available_coins},
                {"dustPendingCoins", result.dust_pending_coins},
                {"availableUtxoCount", result.available_utxo_count},
                {"registeredUtxoCount", result.registered_utxo_count},
                {"unregisteredUtxoCount", result.unregistered_utxo_count},
                {"pendingUtxoCount", result.pending_utxo_count},
                {"likelyDustDesignationLock", result.likely_dust_designation_lock},
                {"likelyPendingUtxoLock", result.likely_pending_utxo_lock},
            };

            midnight::blockchain::copy_string_to_c_buffer(result.source_address, out_source_address, out_source_address_capacity);
            midnight::blockchain::copy_string_to_c_buffer(state_json.dump(), out_state_json, out_state_json_capacity);
            midnight::blockchain::copy_string_to_c_buffer("", out_error, out_error_capacity);
            return 0;
        }

        midnight::blockchain::copy_string_to_c_buffer("", out_source_address, out_source_address_capacity);
        midnight::blockchain::copy_string_to_c_buffer("", out_state_json, out_state_json_capacity);
        midnight::blockchain::copy_string_to_c_buffer(result.error, out_error, out_error_capacity);
        return 1;
    }

    int midnight_deploy_official_compact_contract(
        const char *contract,
        const char *network,
        const char *seed_hex,
        uint64_t fee_bps,
        const char *proof_server,
        const char *node_url,
        const char *indexer_url,
        const char *indexer_ws_url,
        uint32_t sync_timeout_seconds,
        uint32_t dust_wait_seconds,
        uint32_t deploy_timeout_seconds,
        char *out_contract_address,
        size_t out_contract_address_capacity,
        char *out_txid,
        size_t out_txid_capacity,
        char *out_deploy_json,
        size_t out_deploy_json_capacity,
        char *out_error,
        size_t out_error_capacity)
    {
        const std::string contract_value = (contract != nullptr && contract[0] != '\0') ? contract : "FaucetAMM";
        const std::string network_value = (network != nullptr && network[0] != '\0') ? network : "undeployed";
        const std::string seed_value = (seed_hex != nullptr) ? seed_hex : "";
        const std::string proof_value = (proof_server != nullptr) ? proof_server : "";
        const std::string node_value = (node_url != nullptr) ? node_url : "";
        const std::string indexer_value = (indexer_url != nullptr) ? indexer_url : "";
        const std::string indexer_ws_value = (indexer_ws_url != nullptr) ? indexer_ws_url : "";
        const uint32_t sync_timeout_value = (sync_timeout_seconds == 0U) ? 120U : sync_timeout_seconds;
        const uint32_t dust_wait_value = (dust_wait_seconds == 0U) ? 120U : dust_wait_seconds;
        const uint32_t deploy_timeout_value = (deploy_timeout_seconds == 0U) ? 300U : deploy_timeout_seconds;
        const std::string bridge_script = midnight::blockchain::read_env_or_default(
            "MIDNIGHT_OFFICIAL_DEPLOY_SCRIPT",
            "tools/official-deploy-contract.mjs");

        const auto result = midnight::blockchain::deploy_official_compact_contract(
            contract_value,
            network_value,
            seed_value,
            fee_bps,
            "",
            proof_value,
            node_value,
            indexer_value,
            indexer_ws_value,
            sync_timeout_value,
            dust_wait_value,
            deploy_timeout_value,
            1,
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            bridge_script);

        if (result.success)
        {
            nlohmann::json deploy_json = {
                {"network", result.network},
                {"contract", result.contract},
                {"contractExport", result.contract_export},
                {"contractModule", result.contract_module},
                {"sourceAddress", result.source_address},
                {"contractAddress", result.contract_address},
                {"txId", result.txid},
                {"unshieldedBalance", result.unshielded_balance},
                {"dustBalance", result.dust_balance},
                {"dustRegistration", {
                                         {"attempted", result.dust_registration_attempted},
                                         {"success", result.dust_registration_success},
                                         {"registeredUtxos", result.dust_registration_registered_utxos},
                                         {"message", result.dust_registration_message},
                                     }},
                {"endpoints", {
                                  {"nodeUrl", result.endpoints_node_url},
                                  {"indexerUrl", result.endpoints_indexer_url},
                                  {"indexerWsUrl", result.endpoints_indexer_ws_url},
                                  {"proofServer", result.endpoints_proof_server},
                              }},
            };

            midnight::blockchain::copy_string_to_c_buffer(result.contract_address, out_contract_address, out_contract_address_capacity);
            midnight::blockchain::copy_string_to_c_buffer(result.txid, out_txid, out_txid_capacity);
            midnight::blockchain::copy_string_to_c_buffer(deploy_json.dump(), out_deploy_json, out_deploy_json_capacity);
            midnight::blockchain::copy_string_to_c_buffer("", out_error, out_error_capacity);
            return 0;
        }

        midnight::blockchain::copy_string_to_c_buffer("", out_contract_address, out_contract_address_capacity);
        midnight::blockchain::copy_string_to_c_buffer("", out_txid, out_txid_capacity);
        midnight::blockchain::copy_string_to_c_buffer("", out_deploy_json, out_deploy_json_capacity);
        midnight::blockchain::copy_string_to_c_buffer(result.error, out_error, out_error_capacity);
        return 1;
    }
}
