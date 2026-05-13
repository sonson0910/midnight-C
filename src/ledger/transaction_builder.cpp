#include "midnight/ledger/transaction_builder.hpp"
#include "midnight/core/common_utils.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace midnight::ledger
{
    namespace
    {
        namespace fs = std::filesystem;
        using json = nlohmann::json;

        bool is_decimal_u128(const std::string &value)
        {
            if (value.empty())
            {
                return false;
            }
            for (char c : value)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
            }
            const auto first_non_zero = value.find_first_not_of('0');
            const std::string normalized =
                first_non_zero == std::string::npos ? "0" : value.substr(first_non_zero);
            constexpr const char *kMaxU128 = "340282366920938463463374607431768211455";
            if (normalized.size() != std::char_traits<char>::length(kMaxU128))
            {
                return normalized.size() < std::char_traits<char>::length(kMaxU128);
            }
            return normalized <= kMaxU128;
        }

        bool is_hex_32(const std::string &input)
        {
            const std::string hex = midnight::util::strip_hex_prefix(input);
            if (hex.size() != 64)
            {
                return false;
            }
            for (char c : hex)
            {
                if (midnight::util::hex_nibble(c) < 0)
                {
                    return false;
                }
            }
            return true;
        }

        std::string shell_quote(const std::string &value)
        {
            std::string out = "'";
            for (char c : value)
            {
                if (c == '\'')
                {
                    out += "'\\''";
                }
                else
                {
                    out.push_back(c);
                }
            }
            out += "'";
            return out;
        }

        std::string make_temp_output(const ToolkitConfig &config, const std::string &prefix)
        {
            fs::path dir = config.temp_directory.empty()
                ? fs::temp_directory_path()
                : fs::path(config.temp_directory);
            std::error_code ec;
            fs::create_directories(dir, ec);

            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            fs::path path = dir / (prefix + "-" + std::to_string(stamp) + ".mn");
            return path.string();
        }

        std::string read_all_text(const std::string &path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                throw std::runtime_error("Cannot open transaction file: " + path);
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::string run_command_capture(const std::string &command, int &exit_code)
        {
            std::array<char, 4096> buffer{};
            std::string output;
#ifdef _WIN32
            FILE *pipe = _popen((command + " 2>&1").c_str(), "r");
#else
            FILE *pipe = popen((command + " 2>&1").c_str(), "r");
#endif
            if (pipe == nullptr)
            {
                throw std::runtime_error("Failed to start command");
            }
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                output += buffer.data();
            }
#ifdef _WIN32
            exit_code = _pclose(pipe);
#else
            exit_code = pclose(pipe);
#endif
            return output;
        }

        std::vector<uint8_t> bytes_from_hex_checked(const std::string &hex_input)
        {
            const std::string hex = midnight::util::strip_hex_prefix(hex_input);
            if (hex.empty() || (hex.size() % 2) != 0)
            {
                throw std::runtime_error("Invalid transaction hex in toolkit output");
            }
            std::vector<uint8_t> bytes;
            bytes.reserve(hex.size() / 2);
            for (size_t i = 0; i < hex.size(); i += 2)
            {
                const int hi = midnight::util::hex_nibble(hex[i]);
                const int lo = midnight::util::hex_nibble(hex[i + 1]);
                if (hi < 0 || lo < 0)
                {
                    throw std::runtime_error("Invalid transaction hex in toolkit output");
                }
                bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
            }
            return bytes;
        }

        void append_arg(std::ostringstream &cmd, const std::string &arg)
        {
            cmd << ' ' << shell_quote(arg);
        }

        void append_flag_value(std::ostringstream &cmd, const std::string &flag, const std::string &value)
        {
            if (!value.empty())
            {
                append_arg(cmd, flag);
                append_arg(cmd, value);
            }
        }

        void append_source_args(std::ostringstream &cmd, const SourceConfig &source)
        {
            if (!source.src_files.empty())
            {
                for (const auto &file : source.src_files)
                {
                    append_flag_value(cmd, "--src-file", file);
                }
            }
            else if (!source.src_url.empty())
            {
                append_flag_value(cmd, "--src-url", source.src_url);
            }

            if (source.fetch_only_cached)
            {
                append_arg(cmd, "--fetch-only-cached");
            }
            if (source.ignore_block_context)
            {
                append_arg(cmd, "--ignore-block-context");
            }
            if (source.dust_warp)
            {
                append_arg(cmd, "--dust-warp");
            }
            append_flag_value(cmd, "--fetch-cache", source.fetch_cache);
            append_flag_value(cmd, "--ledger-state-db", source.ledger_state_db);
        }

        void validate_seed_present(const std::string &name, const std::string &seed)
        {
            if (seed.empty())
            {
                throw std::invalid_argument(name + " is required");
            }
        }
    }

    TransactionBuilder::TransactionBuilder(ToolkitConfig config)
        : config_(std::move(config))
    {
        if (config_.toolkit_binary.empty())
        {
            throw std::invalid_argument("toolkit_binary is required");
        }
    }

    BuildResult TransactionBuilder::load_serialized_transaction_file(const std::string &path)
    {
        BuildResult result;
        result.output_file = path;
        try
        {
            const auto parsed = json::parse(read_all_text(path));
            json tx_value;
            if (parsed.contains("tx"))
            {
                tx_value = parsed.at("tx");
            }
            else if (parsed.contains("batches"))
            {
                const auto &batches = parsed.at("batches");
                if (!batches.is_array() || batches.empty() ||
                    !batches.at(0).is_array() || batches.at(0).empty())
                {
                    throw std::runtime_error("SerializedTxBatches has no transaction");
                }
                tx_value = batches.at(0).at(0).at("tx");
            }
            else
            {
                throw std::runtime_error("Toolkit output is neither SerializedTx nor SerializedTxBatches");
            }

            if (!tx_value.is_object() || !tx_value.contains("Midnight"))
            {
                throw std::runtime_error("Toolkit output does not contain a Midnight transaction");
            }

            result.transaction_hex = tx_value.at("Midnight").get<std::string>();
            result.transaction_bytes = bytes_from_hex_checked(result.transaction_hex);
            if (parsed.contains("tx_hash") && parsed.at("tx_hash").is_string())
            {
                result.transaction_hash = parsed.at("tx_hash").get<std::string>();
            }
            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        return result;
    }

    BuildResult TransactionBuilder::run_generate_txs(
        const SourceConfig &source,
        const std::vector<std::string> &builder_args) const
    {
        BuildResult result;
        result.output_file = make_temp_output(config_, "midnight-tx");

        std::ostringstream cmd;
        if (!config_.working_directory.empty())
        {
            cmd << "cd " << shell_quote(config_.working_directory) << " && ";
        }
        cmd << shell_quote(config_.toolkit_binary) << " generate-txs";
        append_source_args(cmd, source);
        append_flag_value(cmd, "--dest-file", result.output_file);
        append_flag_value(cmd, "--proof-server", config_.proof_server_url);
        for (const auto &arg : builder_args)
        {
            append_arg(cmd, arg);
        }

        result.command = cmd.str();
        try
        {
            int exit_code = 0;
            result.log = run_command_capture(result.command, exit_code);
            if (exit_code != 0)
            {
                result.error = "midnight-node-toolkit generate-txs failed: " + result.log;
                return result;
            }

            auto loaded = load_serialized_transaction_file(result.output_file);
            loaded.command = result.command;
            loaded.log = result.log;
            if (!config_.keep_artifacts)
            {
                std::error_code ec;
                fs::remove(result.output_file, ec);
            }
            return loaded;
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            return result;
        }
    }

    BuildResult TransactionBuilder::build_transfer_night(const TransferNightParams &params) const
    {
        try
        {
            validate_seed_present("source_seed", params.source_seed);
            if (params.destination_addresses.empty())
            {
                throw std::invalid_argument("At least one destination address is required");
            }
            if (!is_decimal_u128(params.amount))
            {
                throw std::invalid_argument("amount must be a u128 decimal string");
            }
            if (!is_hex_32(params.token_type))
            {
                throw std::invalid_argument("token_type must be 32-byte hex");
            }
            if (params.rng_seed && !is_hex_32(*params.rng_seed))
            {
                throw std::invalid_argument("rng_seed must be 32-byte hex");
            }

            std::vector<std::string> args = {
                "single-tx",
                "--source-seed", params.source_seed,
                "--unshielded-amount", params.amount,
                "--unshielded-token-type", midnight::util::strip_hex_prefix(params.token_type),
                "--coin-selection", params.coin_selection,
            };
            if (params.funding_seed && !params.funding_seed->empty())
            {
                args.push_back("--funding-seed");
                args.push_back(*params.funding_seed);
            }
            for (const auto &address : params.destination_addresses)
            {
                args.push_back("--destination-address");
                args.push_back(address);
            }
            for (const auto &utxo : params.input_utxos)
            {
                args.push_back("--input-utxo");
                args.push_back(utxo);
            }
            if (params.rng_seed)
            {
                args.push_back("--rng-seed");
                args.push_back(midnight::util::strip_hex_prefix(*params.rng_seed));
            }
            return run_generate_txs(params.source, args);
        }
        catch (const std::exception &e)
        {
            return {.success = false, .error = e.what()};
        }
    }

    BuildResult TransactionBuilder::build_register_dust(const RegisterDustParams &params) const
    {
        try
        {
            validate_seed_present("wallet_seed", params.wallet_seed);
            if (params.rng_seed && !is_hex_32(*params.rng_seed))
            {
                throw std::invalid_argument("rng_seed must be 32-byte hex");
            }

            std::vector<std::string> args = {
                "register-dust-address",
                "--wallet-seed", params.wallet_seed,
            };
            if (params.funding_seed && !params.funding_seed->empty())
            {
                args.push_back("--funding-seed");
                args.push_back(*params.funding_seed);
            }
            if (params.destination_dust_address && !params.destination_dust_address->empty())
            {
                args.push_back("--destination-dust");
                args.push_back(*params.destination_dust_address);
            }
            if (params.rng_seed)
            {
                args.push_back("--rng-seed");
                args.push_back(midnight::util::strip_hex_prefix(*params.rng_seed));
            }
            return run_generate_txs(params.source, args);
        }
        catch (const std::exception &e)
        {
            return {.success = false, .error = e.what()};
        }
    }

    BuildResult TransactionBuilder::build_deregister_dust(const DeregisterDustParams &params) const
    {
        try
        {
            validate_seed_present("wallet_seed", params.wallet_seed);
            validate_seed_present("funding_seed", params.funding_seed);
            if (params.rng_seed && !is_hex_32(*params.rng_seed))
            {
                throw std::invalid_argument("rng_seed must be 32-byte hex");
            }

            std::vector<std::string> args = {
                "deregister-dust-address",
                "--wallet-seed", params.wallet_seed,
                "--funding-seed", params.funding_seed,
            };
            if (params.rng_seed)
            {
                args.push_back("--rng-seed");
                args.push_back(midnight::util::strip_hex_prefix(*params.rng_seed));
            }
            return run_generate_txs(params.source, args);
        }
        catch (const std::exception &e)
        {
            return {.success = false, .error = e.what()};
        }
    }

    BuildResult TransactionBuilder::build_simple_contract_deploy(
        const SimpleContractDeployParams &params) const
    {
        try
        {
            validate_seed_present("funding_seed", params.funding_seed);
            if (params.rng_seed && !is_hex_32(*params.rng_seed))
            {
                throw std::invalid_argument("rng_seed must be 32-byte hex");
            }

            std::vector<std::string> args = {
                "contract-simple",
                "deploy",
                "--funding-seed", params.funding_seed,
            };
            for (const auto &seed : params.authority_seeds)
            {
                args.push_back("--authority-seed");
                args.push_back(seed);
            }
            if (params.authority_threshold)
            {
                args.push_back("--authority-threshold");
                args.push_back(std::to_string(*params.authority_threshold));
            }
            if (params.rng_seed)
            {
                args.push_back("--rng-seed");
                args.push_back(midnight::util::strip_hex_prefix(*params.rng_seed));
            }
            return run_generate_txs(params.source, args);
        }
        catch (const std::exception &e)
        {
            return {.success = false, .error = e.what()};
        }
    }

    BuildResult TransactionBuilder::build_simple_contract_call(
        const SimpleContractCallParams &params) const
    {
        try
        {
            validate_seed_present("funding_seed", params.funding_seed);
            if (!is_hex_32(params.contract_address))
            {
                throw std::invalid_argument("contract_address must be 32-byte hex");
            }
            if (!is_decimal_u128(params.fee))
            {
                throw std::invalid_argument("fee must be a u128 decimal string");
            }
            if (params.rng_seed && !is_hex_32(*params.rng_seed))
            {
                throw std::invalid_argument("rng_seed must be 32-byte hex");
            }

            std::vector<std::string> args = {
                "contract-simple",
                "call",
                "--funding-seed", params.funding_seed,
                "--call-key", params.call_key,
                "--contract-address", midnight::util::strip_hex_prefix(params.contract_address),
                "--fee", params.fee,
            };
            if (params.rng_seed)
            {
                args.push_back("--rng-seed");
                args.push_back(midnight::util::strip_hex_prefix(*params.rng_seed));
            }
            return run_generate_txs(params.source, args);
        }
        catch (const std::exception &e)
        {
            return {.success = false, .error = e.what()};
        }
    }

    BuildResult TransactionBuilder::run_send_intent(
        const CustomContractIntentParams &params) const
    {
        BuildResult result;
        result.output_file = make_temp_output(config_, "midnight-contract");
        std::ostringstream cmd;
        if (!config_.working_directory.empty())
        {
            cmd << "cd " << shell_quote(config_.working_directory) << " && ";
        }
        cmd << shell_quote(config_.toolkit_binary) << " send-intent";
        append_source_args(cmd, params.source);
        append_flag_value(cmd, "--dest-file", result.output_file);
        append_flag_value(cmd, "--proof-server", config_.proof_server_url);
        append_flag_value(cmd, "--funding-seed", params.funding_seed);
        for (const auto &dir : params.compiled_contract_dirs)
        {
            append_flag_value(cmd, "--compiled-contract-dir", dir);
        }
        for (const auto &intent : params.intent_files)
        {
            append_flag_value(cmd, "--intent-file", intent);
        }
        for (const auto &utxo : params.input_utxos)
        {
            append_flag_value(cmd, "--input-utxo", utxo);
        }
        if (params.zswap_state_file)
        {
            append_flag_value(cmd, "--zswap-state-file", *params.zswap_state_file);
        }
        for (const auto &destination : params.shielded_destinations)
        {
            append_flag_value(cmd, "--shielded-destination", destination);
        }
        if (params.rng_seed)
        {
            append_flag_value(cmd, "--rng-seed", midnight::util::strip_hex_prefix(*params.rng_seed));
        }

        result.command = cmd.str();
        try
        {
            int exit_code = 0;
            result.log = run_command_capture(result.command, exit_code);
            if (exit_code != 0)
            {
                result.error = "midnight-node-toolkit send-intent failed: " + result.log;
                return result;
            }

            auto loaded = load_serialized_transaction_file(result.output_file);
            loaded.command = result.command;
            loaded.log = result.log;
            if (!config_.keep_artifacts)
            {
                std::error_code ec;
                fs::remove(result.output_file, ec);
            }
            return loaded;
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            return result;
        }
    }

    BuildResult TransactionBuilder::build_custom_contract_transaction(
        const CustomContractIntentParams &params) const
    {
        try
        {
            validate_seed_present("funding_seed", params.funding_seed);
            if (params.intent_files.empty())
            {
                throw std::invalid_argument("At least one intent_file is required");
            }
            if (params.compiled_contract_dirs.empty())
            {
                throw std::invalid_argument("At least one compiled_contract_dir is required");
            }
            if (params.rng_seed && !is_hex_32(*params.rng_seed))
            {
                throw std::invalid_argument("rng_seed must be 32-byte hex");
            }
            return run_send_intent(params);
        }
        catch (const std::exception &e)
        {
            return {.success = false, .error = e.what()};
        }
    }

    IntentResult TransactionBuilder::generate_custom_deploy_intent(
        const CustomDeployIntentParams &params) const
    {
        IntentResult result;
        result.output_intent = params.output_intent;
        result.output_private_state = params.output_private_state;
        result.output_zswap_state = params.output_zswap_state;
        try
        {
            if (params.toolkit_js_path.empty())
            {
                throw std::invalid_argument("toolkit_js_path is required");
            }
            if (params.config_path.empty())
            {
                throw std::invalid_argument("config_path is required");
            }
            if (params.coin_public.empty())
            {
                throw std::invalid_argument("coin_public is required");
            }
            if (params.output_intent.empty() ||
                params.output_private_state.empty() ||
                params.output_zswap_state.empty())
            {
                throw std::invalid_argument("output_intent, output_private_state, and output_zswap_state are required");
            }

            std::ostringstream cmd;
            if (!config_.working_directory.empty())
            {
                cmd << "cd " << shell_quote(config_.working_directory) << " && ";
            }
            cmd << shell_quote(config_.toolkit_binary) << " generate-intent deploy";
            append_flag_value(cmd, "--toolkit-js-path", params.toolkit_js_path);
            append_flag_value(cmd, "--config", params.config_path);
            append_flag_value(cmd, "--network", params.network);
            append_flag_value(cmd, "--coin-public", params.coin_public);
            if (params.authority_seed)
            {
                append_flag_value(cmd, "--authority-seed", *params.authority_seed);
            }
            append_flag_value(cmd, "--output-intent", params.output_intent);
            append_flag_value(cmd, "--output-private-state", params.output_private_state);
            append_flag_value(cmd, "--output-zswap-state", params.output_zswap_state);
            for (const auto &arg : params.constructor_args)
            {
                append_arg(cmd, arg);
            }

            result.command = cmd.str();
            int exit_code = 0;
            result.log = run_command_capture(result.command, exit_code);
            if (exit_code != 0)
            {
                result.error = "midnight-node-toolkit generate-intent deploy failed: " + result.log;
                return result;
            }
            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        return result;
    }

    IntentResult TransactionBuilder::generate_custom_circuit_intent(
        const CustomCircuitIntentParams &params) const
    {
        IntentResult result;
        result.output_intent = params.output_intent;
        result.output_private_state = params.output_private_state;
        result.output_zswap_state = params.output_zswap_state;
        result.output_onchain_state = params.output_onchain_state;
        result.output_result = params.output_result;
        try
        {
            if (params.toolkit_js_path.empty() ||
                params.config_path.empty() ||
                params.contract_address.empty() ||
                params.coin_public.empty() ||
                params.input_onchain_state.empty() ||
                params.input_private_state.empty() ||
                params.output_intent.empty() ||
                params.output_private_state.empty() ||
                params.output_zswap_state.empty() ||
                params.circuit_id.empty())
            {
                throw std::invalid_argument("Missing required custom circuit intent parameter");
            }
            if (!is_hex_32(params.contract_address))
            {
                throw std::invalid_argument("contract_address must be 32-byte hex");
            }

            std::ostringstream cmd;
            if (!config_.working_directory.empty())
            {
                cmd << "cd " << shell_quote(config_.working_directory) << " && ";
            }
            cmd << shell_quote(config_.toolkit_binary) << " generate-intent circuit";
            append_source_args(cmd, params.source);
            if (params.wallet_seed)
            {
                append_flag_value(cmd, "--wallet-seed", *params.wallet_seed);
            }
            append_flag_value(cmd, "--toolkit-js-path", params.toolkit_js_path);
            append_flag_value(cmd, "--config", params.config_path);
            append_flag_value(cmd, "--contract-address", midnight::util::strip_hex_prefix(params.contract_address));
            append_flag_value(cmd, "--network", params.network);
            append_flag_value(cmd, "--coin-public", params.coin_public);
            append_flag_value(cmd, "--input-onchain-state", params.input_onchain_state);
            append_flag_value(cmd, "--input-private-state", params.input_private_state);
            if (params.input_zswap_state)
            {
                append_flag_value(cmd, "--input-zswap-state", *params.input_zswap_state);
            }
            append_flag_value(cmd, "--output-intent", params.output_intent);
            if (params.output_onchain_state)
            {
                append_flag_value(cmd, "--output-onchain-state", *params.output_onchain_state);
            }
            append_flag_value(cmd, "--output-private-state", params.output_private_state);
            append_flag_value(cmd, "--output-zswap-state", params.output_zswap_state);
            if (params.output_result)
            {
                append_flag_value(cmd, "--output-result", *params.output_result);
            }
            if (params.custom_ledger_parameters)
            {
                append_flag_value(cmd, "--custom-ledger-parameters", *params.custom_ledger_parameters);
            }
            append_arg(cmd, params.circuit_id);
            for (const auto &arg : params.call_args)
            {
                append_arg(cmd, arg);
            }

            result.command = cmd.str();
            int exit_code = 0;
            result.log = run_command_capture(result.command, exit_code);
            if (exit_code != 0)
            {
                result.error = "midnight-node-toolkit generate-intent circuit failed: " + result.log;
                return result;
            }
            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        return result;
    }

} // namespace midnight::ledger
