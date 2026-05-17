#include "midnight/production/midnight_client.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/production/validation.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>

namespace midnight::production
{
    namespace
    {
        json block_info_to_json(const network::BlockInfo &block)
        {
            json value = json::object();
            value["hash"] = block.hash;
            value["height"] = block.height;
            value["timestamp"] = block.timestamp;
            value["transactions"] = block.transactions;
            if (!block.error.empty())
            {
                value["error"] = block.error;
            }
            return value;
        }

        uint64_t parse_json_u64(const json &value)
        {
            if (value.is_number_unsigned())
            {
                return value.get<uint64_t>();
            }
            if (value.is_number_integer())
            {
                const auto signed_value = value.get<int64_t>();
                return signed_value < 0 ? 0 : static_cast<uint64_t>(signed_value);
            }
            if (value.is_string())
            {
                return std::stoull(value.get<std::string>());
            }
            return 0;
        }

        std::string env_value(const char *name)
        {
            const char *value = std::getenv(name);
            return value && *value ? std::string(value) : std::string();
        }

        uint32_t env_u32(const char *name, uint32_t fallback)
        {
            const auto value = env_value(name);
            if (value.empty())
            {
                return fallback;
            }
            try
            {
                return static_cast<uint32_t>(std::stoul(value));
            }
            catch (...)
            {
                return fallback;
            }
        }

        ConfirmationResult confirmation_from_transaction(
            const std::string &hash,
            const json &tx)
        {
            ConfirmationResult result;
            result.transaction_hash = hash;
            result.found = !tx.empty() && tx.is_object();
            result.success = result.found;
            result.transaction = tx;
            if (!result.found)
            {
                return result;
            }

            if (tx.contains("id") && !tx["id"].is_null())
            {
                try
                {
                    result.transaction_id = parse_json_u64(tx["id"]);
                }
                catch (...)
                {
                }
            }
            if (tx.contains("hash") && tx["hash"].is_string())
            {
                result.transaction_hash = tx["hash"].get<std::string>();
            }
            if (tx.contains("block") && tx["block"].is_object())
            {
                const auto &block = tx["block"];
                if (block.contains("height") && !block["height"].is_null())
                {
                    try
                    {
                        result.block_height = parse_json_u64(block["height"]);
                    }
                    catch (...)
                    {
                    }
                }
                if (block.contains("hash") && block["hash"].is_string())
                {
                    result.block_hash = block["hash"].get<std::string>();
                }
            }
            return result;
        }

        SdkError make_error(ErrorCode code, std::string message, std::string detail = {})
        {
            return {.code = code, .message = std::move(message), .detail = std::move(detail)};
        }

        std::string confirmation_hash_for(const BuildSubmitResult &tx)
        {
            if (!tx.build.transaction_hash.empty())
            {
                return tx.build.transaction_hash;
            }
            return tx.submit.extrinsic_hash;
        }

        PipelineResult pipeline_from_build_submit(
            MidnightClient &client,
            const PipelineOptions &options,
            const std::string &operation,
            BuildSubmitResult tx)
        {
            PipelineResult result;
            result.transaction = std::move(tx);

            if (options.save_artifacts)
            {
                ArtifactManager artifacts(options.artifacts);
                result.artifacts = artifacts.save_build_result(
                    operation,
                    result.transaction.build);
            }

            if (!result.transaction.success)
            {
                result.error = result.transaction.error_info
                                   ? result.transaction.error_info
                                   : make_error(ErrorCode::SubmissionRejected,
                                                result.transaction.error.empty()
                                                    ? "Transaction submission failed"
                                                    : result.transaction.error);
                return result;
            }

            if (options.wait_for_confirmation)
            {
                result.confirmation = client.wait_for_transaction(
                    confirmation_hash_for(result.transaction),
                    options.confirmation);
                result.success = result.confirmation.success;
                if (!result.success)
                {
                    result.error = result.confirmation.error;
                }
            }
            else
            {
                result.success = true;
            }

            return result;
        }

        zk::ProofServerClient::Config proof_config_from_url(
            const std::string &url,
            uint32_t timeout_ms)
        {
            zk::ProofServerClient::Config config;
            config.timeout_ms = std::chrono::milliseconds(timeout_ms);

            std::string rest = url;
            if (rest.rfind("https://", 0) == 0)
            {
                config.use_https = true;
                rest = rest.substr(8);
                config.port = 443;
            }
            else if (rest.rfind("http://", 0) == 0)
            {
                config.use_https = false;
                rest = rest.substr(7);
                config.port = 80;
            }

            const auto slash = rest.find('/');
            if (slash != std::string::npos)
            {
                rest = rest.substr(0, slash);
            }

            const auto colon = rest.rfind(':');
            if (colon != std::string::npos)
            {
                config.host = rest.substr(0, colon);
                config.port = static_cast<uint16_t>(
                    std::stoul(rest.substr(colon + 1)));
            }
            else
            {
                config.host = rest;
                if (config.port == 80 && !config.use_https)
                {
                    config.port = 6300;
                }
            }

            if (config.host.empty())
            {
                throw std::runtime_error("Invalid proof server URL");
            }
            return config;
        }

        uint64_t parse_header_height(const json &header)
        {
            if (!header.is_object() || !header.contains("number"))
            {
                return 0;
            }
            try
            {
                return parse_json_u64(header["number"]);
            }
            catch (...)
            {
                return 0;
            }
        }

        std::string normalize_hash(const std::string &hash)
        {
            auto value = midnight::util::strip_hex_prefix(hash);
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        json rpc_post(network::NetworkClient &client, const std::string &method, const json &params)
        {
            json request = {
                {"jsonrpc", "2.0"},
                {"method", method},
                {"params", params},
                {"id", 1}};
            auto response = client.post_json("/", request);
            if (response.contains("error") && !response["error"].is_null())
            {
                const auto &error = response["error"];
                throw std::runtime_error(error.value("message", "JSON-RPC error"));
            }
            if (!response.contains("result"))
            {
                throw std::runtime_error("JSON-RPC response missing result");
            }
            return response["result"];
        }

        BuildSubmitResult validation_failure(ErrorCode code, std::string message, std::string detail = {})
        {
            BuildSubmitResult result;
            result.error = message;
            result.error_info = make_error(code, std::move(message), std::move(detail));
            result.build.error = result.error_info.message;
            return result;
        }

        bool is_bounded_source(const ledger::SourceConfig &source)
        {
            return source.from_block.has_value() || source.to_block.has_value();
        }

        bool cache_path_has_data(const std::string &path)
        {
            if (path.empty() || path.find("://") != std::string::npos)
            {
                return true;
            }
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
            {
                return false;
            }
            if (std::filesystem::is_regular_file(path, ec))
            {
                const auto size = std::filesystem::file_size(path, ec);
                return !ec && size > 0;
            }
            if (!std::filesystem::is_directory(path, ec))
            {
                return false;
            }
            for (const auto &entry : std::filesystem::recursive_directory_iterator(path, ec))
            {
                if (ec)
                {
                    return false;
                }
                if (entry.is_regular_file(ec) && entry.file_size(ec) > 0)
                {
                    return true;
                }
            }
            return false;
        }

        ledger::SourceConfig normalize_source_for_transaction_build(
            ledger::SourceConfig source)
        {
            switch (source.source_mode)
            {
            case ledger::SourceMode::LocalCache:
                source.fetch_only_cached = true;
                break;
            case ledger::SourceMode::ColdSync:
                source.fetch_only_cached = false;
                break;
            case ledger::SourceMode::Bounded:
                source.fetch_only_cached = false;
                break;
            case ledger::SourceMode::Auto:
            default:
                if (is_bounded_source(source))
                {
                    source.source_mode = ledger::SourceMode::Bounded;
                }
                else if (source.src_files.empty())
                {
                    source.source_mode = ledger::SourceMode::LocalCache;
                    source.fetch_only_cached = true;
                }
                break;
            }
            return source;
        }

        ledger::SourceConfig normalize_source_for_sync(ledger::SourceConfig source)
        {
            if (source.source_mode == ledger::SourceMode::LocalCache)
            {
                source.fetch_only_cached = true;
            }
            else if (source.source_mode == ledger::SourceMode::ColdSync)
            {
                source.fetch_only_cached = false;
            }
            return source;
        }

        template <typename Params>
        Params normalize_params_for_transaction_build(const Params &params)
        {
            Params copy = params;
            copy.source = normalize_source_for_transaction_build(params.source);
            return copy;
        }

        ledger::BuildResult build_validation_failure(
            const BuildSubmitResult &validation)
        {
            ledger::BuildResult result;
            result.error = validation.error_info.detail.empty()
                ? validation.error_info.message
                : validation.error_info.message + ": " + validation.error_info.detail;
            return result;
        }

        BuildSubmitResult validate_source(const ledger::SourceConfig &source)
        {
            if (!validation::has_source_input(source.src_url, source.src_files))
            {
                return validation_failure(
                    ErrorCode::LedgerBuildError,
                    "Ledger build source requires src_url or src_files");
            }
            if (source.source_mode == ledger::SourceMode::Bounded &&
                (!source.from_block || !source.to_block))
            {
                return validation_failure(
                    ErrorCode::LedgerBuildError,
                    "Bounded ledger source mode requires from_block and to_block");
            }
            if ((source.from_block && !source.to_block) || (!source.from_block && source.to_block))
            {
                return validation_failure(
                    ErrorCode::LedgerBuildError,
                    "Ledger bounded source requires both from_block and to_block");
            }
            if (source.from_block && source.to_block && *source.from_block > *source.to_block)
            {
                return validation_failure(
                    ErrorCode::LedgerBuildError,
                    "Ledger bounded source from_block must be <= to_block");
            }
            for (const auto &hash : source.transaction_hashes)
            {
                if (!validation::is_tx_hash(hash))
                {
                    return validation_failure(
                        ErrorCode::InvalidHash,
                        "Ledger source transaction_hashes must contain 32-byte transaction hashes",
                        hash);
                }
            }
            if (source.require_ledger_state_cache &&
                source.fetch_only_cached &&
                source.source_mode == ledger::SourceMode::LocalCache &&
                !cache_path_has_data(source.ledger_state_db))
            {
                return validation_failure(
                    ErrorCode::LedgerBuildError,
                    "Local ledger state cache is not synced",
                    "Run sync_ledger_state first, import a cache snapshot, or set source_mode=ColdSync to opt into a full RPC source fetch");
            }
            return {};
        }

        std::shared_ptr<IStateBackend> make_state_backend_for_config(
            const ClientConfig &config,
            const std::shared_ptr<ledger::ILedgerBackend> &ledger_backend)
        {
            if (config.state_backend_mode == StateBackendMode::Managed)
            {
                return make_managed_state_backend(config.managed_state);
            }
            return make_local_state_backend(ledger_backend, config.state_backend_mode);
        }

        std::optional<uint64_t> best_effort_remote_tip(network::MidnightNodeRPC *node)
        {
            if (node == nullptr)
            {
                return std::nullopt;
            }
            try
            {
                return node->get_chain_tip().first;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        BuildSubmitResult validate_seed(
            const std::string &seed,
            const std::string &field)
        {
            if (!validation::is_wallet_seed_or_mnemonic(seed))
            {
                return validation_failure(
                    ErrorCode::InvalidAddress,
                    field + " must be a Midnight WalletSeed hex value or BIP39 mnemonic");
            }
            return {};
        }

        bool all_unshielded_addresses(const std::vector<std::string> &addresses)
        {
            return std::all_of(
                addresses.begin(),
                addresses.end(),
                [](const std::string &address) {
                    return validation::is_unshielded_night_address(address);
                });
        }

        BuildSubmitResult validate_transfer_params(const ledger::TransferNightParams &params)
        {
            if (auto invalid = validate_source(params.source); invalid.error_info)
            {
                return invalid;
            }
            if (auto invalid = validate_seed(params.source_seed, "Transfer source_seed"); invalid.error_info)
            {
                return invalid;
            }
            if (params.funding_seed)
            {
                if (auto invalid = validate_seed(*params.funding_seed, "Transfer funding_seed"); invalid.error_info)
                {
                    return invalid;
                }
            }
            if (params.destination_addresses.empty() || !all_unshielded_addresses(params.destination_addresses))
            {
                return validation_failure(
                    ErrorCode::InvalidAddress,
                    "NIGHT transfer destinations must be mn_addr_* unshielded addresses");
            }
            if (!validation::is_u128_decimal(params.amount))
            {
                return validation_failure(
                    ErrorCode::InvalidAmount,
                    "NIGHT transfer amount must be a u128 decimal string",
                    params.amount);
            }
            if (!validation::is_token_type_hex(params.token_type))
            {
                return validation_failure(
                    ErrorCode::InvalidTokenType,
                    "Token type must be a 32-byte hex value",
                    params.token_type);
            }
            if (params.rng_seed && !validation::is_fixed_hex(*params.rng_seed, 32))
            {
                return validation_failure(ErrorCode::InvalidHash, "rng_seed must be 32-byte hex", *params.rng_seed);
            }
            return {};
        }

        BuildSubmitResult validate_register_dust_params(const ledger::RegisterDustParams &params)
        {
            if (auto invalid = validate_source(params.source); invalid.error_info)
            {
                return invalid;
            }
            if (auto invalid = validate_seed(params.wallet_seed, "DUST registration wallet_seed"); invalid.error_info)
            {
                return invalid;
            }
            if (params.funding_seed)
            {
                if (auto invalid = validate_seed(*params.funding_seed, "DUST registration funding_seed"); invalid.error_info)
                {
                    return invalid;
                }
            }
            if (params.destination_dust_address && !validation::is_dust_address(*params.destination_dust_address))
            {
                return validation_failure(
                    ErrorCode::InvalidAddress,
                    "DUST registration destination must be an mn_dust_* address",
                    *params.destination_dust_address);
            }
            if (params.rng_seed && !validation::is_fixed_hex(*params.rng_seed, 32))
            {
                return validation_failure(ErrorCode::InvalidHash, "rng_seed must be 32-byte hex", *params.rng_seed);
            }
            return {};
        }

        BuildSubmitResult validate_deregister_dust_params(const ledger::DeregisterDustParams &params)
        {
            if (auto invalid = validate_source(params.source); invalid.error_info)
            {
                return invalid;
            }
            if (auto invalid = validate_seed(params.wallet_seed, "DUST deregistration wallet_seed"); invalid.error_info)
            {
                return invalid;
            }
            if (auto invalid = validate_seed(params.funding_seed, "DUST deregistration funding_seed"); invalid.error_info)
            {
                return invalid;
            }
            if (params.rng_seed && !validation::is_fixed_hex(*params.rng_seed, 32))
            {
                return validation_failure(ErrorCode::InvalidHash, "rng_seed must be 32-byte hex", *params.rng_seed);
            }
            return {};
        }

        BuildSubmitResult validate_deploy_params(const ledger::SimpleContractDeployParams &params)
        {
            if (auto invalid = validate_source(params.source); invalid.error_info)
            {
                return invalid;
            }
            if (auto invalid = validate_seed(params.funding_seed, "Contract deploy funding_seed"); invalid.error_info)
            {
                return invalid;
            }
            for (const auto &seed : params.authority_seeds)
            {
                if (auto invalid = validate_seed(seed, "Contract deploy authority_seed"); invalid.error_info)
                {
                    return invalid;
                }
            }
            if (params.authority_seeds.empty())
            {
                return validation_failure(ErrorCode::InvalidAddress, "Contract deploy requires at least one authority seed");
            }
            if (params.authority_threshold && *params.authority_threshold > params.authority_seeds.size())
            {
                return validation_failure(
                    ErrorCode::InvalidAmount,
                    "Contract authority threshold cannot exceed authority seed count");
            }
            if (params.rng_seed && !validation::is_fixed_hex(*params.rng_seed, 32))
            {
                return validation_failure(ErrorCode::InvalidHash, "rng_seed must be 32-byte hex", *params.rng_seed);
            }
            return {};
        }

        BuildSubmitResult validate_call_params(const ledger::SimpleContractCallParams &params)
        {
            if (auto invalid = validate_source(params.source); invalid.error_info)
            {
                return invalid;
            }
            if (auto invalid = validate_seed(params.funding_seed, "Contract call funding_seed"); invalid.error_info)
            {
                return invalid;
            }
            if (!validation::is_contract_address_hex(params.contract_address))
            {
                return validation_failure(
                    ErrorCode::InvalidContractAddress,
                    "Contract call requires a 32-byte contract hex address",
                    params.contract_address);
            }
            if (!validation::is_u128_decimal(params.fee))
            {
                return validation_failure(ErrorCode::InvalidAmount, "Contract call fee must be a u128 decimal string", params.fee);
            }
            if (params.rng_seed && !validation::is_fixed_hex(*params.rng_seed, 32))
            {
                return validation_failure(ErrorCode::InvalidHash, "rng_seed must be 32-byte hex", *params.rng_seed);
            }
            return {};
        }

        BuildSubmitResult validate_custom_contract_params(const ledger::CustomContractIntentParams &params)
        {
            if (auto invalid = validate_source(params.source); invalid.error_info)
            {
                return invalid;
            }
            if (auto invalid = validate_seed(params.funding_seed, "Custom contract funding_seed"); invalid.error_info)
            {
                return invalid;
            }
            if (params.intent_files.empty())
            {
                return validation_failure(ErrorCode::LedgerBuildError, "Custom contract transaction requires intent files");
            }
            if (params.rng_seed && !validation::is_fixed_hex(*params.rng_seed, 32))
            {
                return validation_failure(ErrorCode::InvalidHash, "rng_seed must be 32-byte hex", *params.rng_seed);
            }
            return {};
        }
    }

    ClientConfig ClientConfig::for_network(network::NetworkConfig::Network network)
    {
        ClientConfig config;
        config.network_name = network::NetworkConfig::network_name(network);
        config.node_url = network::NetworkConfig::get_rpc_endpoint(network);
        config.indexer_url = network::NetworkConfig::get_graphql_endpoint(network);
        config.proof_server_url = "http://127.0.0.1:6300";
        config.ledger_ffi_library = env_value("MIDNIGHT_LEDGER_FFI_LIBRARY");
        return config;
    }

    ClientConfig ClientConfig::from_environment()
    {
        const auto network_name = env_value("MIDNIGHT_NETWORK");
        auto config = for_network(network::NetworkConfig::from_string(
            network_name.empty() ? "preview" : network_name));
        if (!network_name.empty())
        {
            config.network_name = network::NetworkConfig::network_name(
                network::NetworkConfig::from_string(network_name));
        }

        if (auto value = env_value("MIDNIGHT_NODE_URL"); !value.empty())
        {
            config.node_url = value;
        }
        if (auto value = env_value("MIDNIGHT_INDEXER_URL"); !value.empty())
        {
            config.indexer_url = value;
        }
        if (auto value = env_value("MIDNIGHT_PROOF_SERVER_URL"); !value.empty())
        {
            config.proof_server_url = value;
        }
        if (auto value = env_value("MIDNIGHT_LEDGER_FFI_LIBRARY"); !value.empty())
        {
            config.ledger_ffi_library = value;
        }
        if (auto value = env_value("MIDNIGHT_STATE_MODE"); !value.empty())
        {
            config.state_backend_mode = state_backend_mode_from_string(value);
        }
        if (auto value = env_value("MIDNIGHT_STATE_SERVICE_URL"); !value.empty())
        {
            config.managed_state.base_url = value;
        }
        if (auto value = env_value("MIDNIGHT_STATE_SERVICE_API_KEY"); !value.empty())
        {
            config.managed_state.api_key = value;
        }
        config.node_timeout_ms = env_u32("MIDNIGHT_NODE_TIMEOUT_MS", config.node_timeout_ms);
        config.proof_timeout_ms = env_u32("MIDNIGHT_PROOF_TIMEOUT_MS", config.proof_timeout_ms);
        config.managed_state.timeout_ms = env_u32(
            "MIDNIGHT_STATE_SERVICE_TIMEOUT_MS",
            config.managed_state.timeout_ms);
        return config;
    }

    MidnightClient::MidnightClient(const ClientConfig &config)
        : MidnightClient(config, ledger::make_ffi_ledger_backend(config.ledger_ffi_library))
    {
    }

    MidnightClient::MidnightClient(
        const ClientConfig &config,
        std::shared_ptr<ledger::ILedgerBackend> ledger_backend)
        : config_(config),
          ledger_backend_(std::move(ledger_backend))
    {
        if (config_.node_url.empty())
        {
            throw std::runtime_error("Midnight node URL is required");
        }
        if (config_.indexer_url.empty())
        {
            throw std::runtime_error("Midnight indexer URL is required");
        }

        node_ = std::make_unique<network::MidnightNodeRPC>(
            config_.node_url,
            config_.node_timeout_ms);
        indexer_ = std::make_unique<network::IndexerClient>(config_.indexer_url);

        if (!config_.proof_server_url.empty())
        {
            auto proof_config = proof_config_from_url(
                config_.proof_server_url,
                config_.proof_timeout_ms);
            proof_server_ = std::make_unique<zk::ProofServerClient>(proof_config);
        }

        if (!ledger_backend_)
        {
            ledger_backend_ = ledger::make_unavailable_ledger_backend();
        }
        state_backend_ = make_state_backend_for_config(config_, ledger_backend_);
    }

    json MidnightClient::health_check()
    {
        json status = json::object();

        status["config"] = {
            {"network", config_.network_name},
            {"node_url", config_.node_url},
            {"indexer_url", config_.indexer_url},
            {"proof_server_url", config_.proof_server_url.empty() ? json(nullptr) : json(config_.proof_server_url)},
            {"ledger_ffi_library", config_.ledger_ffi_library.empty() ? json(nullptr) : json(config_.ledger_ffi_library)},
            {"state_backend_mode", state_backend_mode_to_string(config_.state_backend_mode)},
            {"managed_state_service_url", config_.managed_state.base_url.empty() ? json(nullptr) : json(config_.managed_state.base_url)}};

        try
        {
            const auto tip = node_->get_chain_tip();
            status["node"] = {
                {"status", "ok"},
                {"height", tip.first},
                {"hash", tip.second}};
        }
        catch (const std::exception &e)
        {
            status["node"] = {{"status", "error"}, {"error", e.what()}};
        }

        try
        {
            status["indexer"] = {
                {"status", "ok"},
                {"height", indexer_->get_block_height()},
                {"synced", indexer_->is_synced()}};
        }
        catch (const std::exception &e)
        {
            status["indexer"] = {{"status", "error"}, {"error", e.what()}};
        }

        if (proof_server_)
        {
            try
            {
                status["proof_server"] = {
                    {"status", proof_server_->is_connected() ? "ok" : "error"},
                    {"info", proof_server_->get_server_status()}};
            }
            catch (const std::exception &e)
            {
                status["proof_server"] = {{"status", "error"}, {"error", e.what()}};
            }
        }
        else
        {
            status["proof_server"] = {{"status", "not_configured"}};
        }

        if (auto *ffi = dynamic_cast<ledger::FfiLedgerBackend *>(ledger_backend_.get()))
        {
            status["ledger_backend"] = {
                {"status", ffi->is_available() ? "ok" : "error"},
                {"wallet_derivation", ffi->can_derive_wallet_addresses()},
                {"backend_info", ffi->can_report_backend_info()},
                {"native_validation", ffi->can_validate()},
                {"transaction_inspection", ffi->can_inspect_transactions()},
                {"error", ffi->last_error()}};
            if (ffi->can_report_backend_info())
            {
                const auto info = ffi->backend_info();
                status["ledger_backend"]["abi_version"] = info.abi_version;
                status["ledger_backend"]["ffi_version"] = info.ffi_version;
                status["ledger_backend"]["ledger"] = info.ledger;
                status["ledger_backend"]["zswap"] = info.zswap;
                status["ledger_backend"]["onchain_runtime"] = info.onchain_runtime;
                status["ledger_backend"]["indexer_schema"] = info.indexer_schema;
                status["ledger_backend"]["capabilities"] = info.capabilities;
                status["ledger_backend"]["warnings"] = info.warnings;
                if (!info.error.empty())
                {
                    status["ledger_backend"]["info_error"] = info.error;
                }
            }
        }
        else
        {
            status["ledger_backend"] = {{"status", "custom_or_unavailable"}};
        }

        return status;
    }

    ledger::LedgerBackendInfo MidnightClient::ledger_backend_info()
    {
        ledger::LedgerBackendInfo result;
        auto *ffi = dynamic_cast<ledger::FfiLedgerBackend *>(ledger_backend_.get());
        if (ffi == nullptr || !ffi->can_report_backend_info())
        {
            result.error =
                "Ledger backend info requires libmidnight_ledger_ffi with "
                "midnight_ledger_backend_info";
            return result;
        }
        return ffi->backend_info();
    }

    ledger::ValidationResult MidnightClient::validate_ledger_value(
        const std::string &kind,
        const std::string &value,
        const std::string &network)
    {
        ledger::ValidationResult result;
        auto *ffi = dynamic_cast<ledger::FfiLedgerBackend *>(ledger_backend_.get());
        if (ffi == nullptr || !ffi->can_validate())
        {
            result.error =
                "Native ledger validation requires libmidnight_ledger_ffi with "
                "midnight_ledger_validate";
            return result;
        }
        return ffi->validate(kind, value, network.empty() ? config_.network_name : network);
    }

    ledger::TransactionInspectionResult MidnightClient::inspect_transaction_hex(
        const std::string &transaction_hex,
        uint8_t ledger_version)
    {
        ledger::TransactionInspectionResult result;
        auto *ffi = dynamic_cast<ledger::FfiLedgerBackend *>(ledger_backend_.get());
        if (ffi == nullptr || !ffi->can_inspect_transactions())
        {
            result.error =
                "Transaction inspection requires libmidnight_ledger_ffi with "
                "midnight_ledger_inspect_transaction";
            return result;
        }
        return ffi->inspect_transaction(transaction_hex, ledger_version);
    }

    ledger::WalletAddressDerivationResult MidnightClient::derive_wallet_addresses(
        const std::string &seed_hex_or_mnemonic,
        const std::string &network)
    {
        ledger::WalletAddressDerivationResult result;
        auto *ffi = dynamic_cast<ledger::FfiLedgerBackend *>(ledger_backend_.get());
        if (ffi == nullptr || !ffi->can_derive_wallet_addresses())
        {
            result.error =
                "Canonical wallet derivation requires libmidnight_ledger_ffi with "
                "midnight_ledger_derive_wallet_addresses";
            return result;
        }
        return ffi->derive_wallet_addresses(
            seed_hex_or_mnemonic,
            network.empty() ? config_.network_name : network);
    }

    void MidnightClient::set_ledger_backend(std::shared_ptr<ledger::ILedgerBackend> ledger_backend)
    {
        ledger_backend_ = std::move(ledger_backend);
        if (!ledger_backend_)
        {
            ledger_backend_ = ledger::make_unavailable_ledger_backend();
        }
        if (config_.state_backend_mode != StateBackendMode::Managed)
        {
            state_backend_ = make_state_backend_for_config(config_, ledger_backend_);
        }
    }

    void MidnightClient::set_state_backend(std::shared_ptr<IStateBackend> state_backend)
    {
        state_backend_ = std::move(state_backend);
        if (!state_backend_)
        {
            state_backend_ = make_state_backend_for_config(config_, ledger_backend_);
        }
    }

    SubmitResult MidnightClient::submit_midnight_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        if (transaction_bytes.empty())
        {
            SubmitResult result;
            result.error = "Midnight transaction bytes cannot be empty";
            result.error_info = make_error(ErrorCode::InvalidHash, result.error);
            return result;
        }

        return submit_midnight_transaction_hex(
            midnight::util::bytes_to_hex(transaction_bytes));
    }

    SubmitResult MidnightClient::submit_midnight_transaction_hex(
        const std::string &transaction_hex)
    {
        SubmitResult result;
        const auto hex = midnight::util::strip_hex_prefix(transaction_hex);
        if (hex.empty() || hex.size() % 2 != 0 || !midnight::util::is_hex_string(hex))
        {
            result.error = "Midnight transaction must be even-length hex bytes";
            result.error_info = make_error(ErrorCode::InvalidHash, result.error);
            return result;
        }
        try
        {
            result.extrinsic_hash = node_->submit_transaction(transaction_hex);
            result.success = !result.extrinsic_hash.empty();
            if (!result.success)
            {
                result.error = "Node returned an empty extrinsic hash";
                result.error_info = make_error(ErrorCode::NodeRejectedTx, result.error);
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            result.error_info = make_error(ErrorCode::NodeRejectedTx, "Midnight transaction submission failed", e.what());
        }
        return result;
    }

    SubmitResult MidnightClient::submit_extrinsic_hex(const std::string &extrinsic_hex)
    {
        SubmitResult result;
        const auto hex = midnight::util::strip_hex_prefix(extrinsic_hex);
        if (hex.empty() || hex.size() % 2 != 0 || !midnight::util::is_hex_string(hex))
        {
            result.error = "Extrinsic must be even-length hex bytes";
            result.error_info = make_error(ErrorCode::InvalidHash, result.error);
            return result;
        }
        try
        {
            result.extrinsic_hash = node_->submit_extrinsic(extrinsic_hex);
            result.success = !result.extrinsic_hash.empty();
            if (!result.success)
            {
                result.error = "Node returned an empty extrinsic hash";
                result.error_info = make_error(ErrorCode::NodeRejectedTx, result.error);
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            result.error_info = make_error(ErrorCode::NodeRejectedTx, "Extrinsic submission failed", e.what());
        }
        return result;
    }

    namespace
    {
        BuildSubmitResult submit_built_transaction(
            MidnightClient &client,
            ledger::BuildResult build)
        {
            BuildSubmitResult result;
            result.build = std::move(build);
            if (!result.build.success)
            {
                const auto code = result.build.error.find("native Midnight ledger backend") != std::string::npos ||
                                          result.build.error.find("FFI") != std::string::npos
                                      ? ErrorCode::LedgerBackendError
                                      : ErrorCode::LedgerBuildError;
                result.error = result.build.error.empty()
                    ? "Transaction build failed"
                    : result.build.error;
                result.error_info = make_error(
                    code,
                    "Transaction build failed",
                    result.error);
                return result;
            }
            if (result.build.transaction_bytes.empty())
            {
                result.error = "Ledger backend returned no Midnight transaction bytes";
                result.error_info = make_error(
                    ErrorCode::LedgerBuildError,
                    "Ledger backend returned no Midnight transaction bytes");
                return result;
            }

            result.submit = client.submit_midnight_transaction(result.build.transaction_bytes);
            result.success = result.submit.success;
            if (!result.success)
            {
                result.error = result.submit.error.empty()
                    ? "Transaction submission failed"
                    : result.submit.error;
                result.error_info = result.submit.error_info
                                        ? result.submit.error_info
                                        : make_error(ErrorCode::SubmissionRejected, result.error);
            }
            return result;
        }

        template <typename Params>
        Params with_configured_proof_server(
            const Params &params,
            const ClientConfig &config)
        {
            Params copy = params;
            if (!copy.proof_server && !config.proof_server_url.empty())
            {
                copy.proof_server = config.proof_server_url;
            }
            return copy;
        }
    }

    ledger::BuildResult MidnightClient::build_transfer_night(
        const ledger::TransferNightParams &params)
    {
        const auto normalized = normalize_params_for_transaction_build(params);
        if (auto invalid = validate_transfer_params(normalized); invalid.error_info)
        {
            return build_validation_failure(invalid);
        }
        return ledger_backend_->build_transfer_night(
            with_configured_proof_server(normalized, config_));
    }

    ledger::BuildResult MidnightClient::build_register_dust(
        const ledger::RegisterDustParams &params)
    {
        const auto normalized = normalize_params_for_transaction_build(params);
        if (auto invalid = validate_register_dust_params(normalized); invalid.error_info)
        {
            return build_validation_failure(invalid);
        }
        return ledger_backend_->build_register_dust(
            with_configured_proof_server(normalized, config_));
    }

    ledger::BuildResult MidnightClient::build_deregister_dust(
        const ledger::DeregisterDustParams &params)
    {
        const auto normalized = normalize_params_for_transaction_build(params);
        if (auto invalid = validate_deregister_dust_params(normalized); invalid.error_info)
        {
            return build_validation_failure(invalid);
        }
        return ledger_backend_->build_deregister_dust(
            with_configured_proof_server(normalized, config_));
    }

    ledger::BuildResult MidnightClient::build_simple_contract_deploy(
        const ledger::SimpleContractDeployParams &params)
    {
        const auto normalized = normalize_params_for_transaction_build(params);
        if (auto invalid = validate_deploy_params(normalized); invalid.error_info)
        {
            return build_validation_failure(invalid);
        }
        return ledger_backend_->build_simple_contract_deploy(
            with_configured_proof_server(normalized, config_));
    }

    ledger::BuildResult MidnightClient::build_simple_contract_call(
        const ledger::SimpleContractCallParams &params)
    {
        const auto normalized = normalize_params_for_transaction_build(params);
        if (auto invalid = validate_call_params(normalized); invalid.error_info)
        {
            return build_validation_failure(invalid);
        }
        return ledger_backend_->build_simple_contract_call(
            with_configured_proof_server(normalized, config_));
    }

    ledger::BuildResult MidnightClient::build_custom_contract_transaction(
        const ledger::CustomContractIntentParams &params)
    {
        const auto normalized = normalize_params_for_transaction_build(params);
        if (auto invalid = validate_custom_contract_params(normalized); invalid.error_info)
        {
            return build_validation_failure(invalid);
        }
        return ledger_backend_->build_custom_contract_transaction(
            with_configured_proof_server(normalized, config_));
    }

    ledger::BuildResult MidnightClient::sync_ledger_state(
        const ledger::SyncLedgerStateParams &params)
    {
        auto normalized = params;
        normalized.source = normalize_source_for_sync(params.source);
        if (!validation::has_source_input(normalized.source.src_url, normalized.source.src_files))
        {
            ledger::BuildResult result;
            result.error = "Ledger state sync requires source.src_url or source.src_files";
            return result;
        }
        if ((normalized.source.from_block && !normalized.source.to_block) ||
            (!normalized.source.from_block && normalized.source.to_block))
        {
            ledger::BuildResult result;
            result.error = "Ledger state sync bounded source requires both from_block and to_block";
            return result;
        }
        if (normalized.source.from_block && normalized.source.to_block &&
            *normalized.source.from_block > *normalized.source.to_block)
        {
            ledger::BuildResult result;
            result.error = "Ledger state sync bounded source from_block must be <= to_block";
            return result;
        }
        if (normalized.wallet_seeds.empty())
        {
            ledger::BuildResult result;
            result.error = "Ledger state sync requires at least one wallet seed";
            return result;
        }
        return ledger_backend_->sync_ledger_state(normalized);
    }

    StateStatus MidnightClient::state_status(
        const ledger::SourceConfig &source,
        uint64_t max_lag_blocks)
    {
        if (!state_backend_)
        {
            state_backend_ = make_state_backend_for_config(config_, ledger_backend_);
        }

        auto status = state_backend_->status(source);
        if (status.local_checkpoint_available && status.remote_tip_height == 0)
        {
            const auto remote_tip = best_effort_remote_tip(node_.get());
            status.remote_tip_height = remote_tip.value_or(status.remote_tip_height);
        }
        if (status.ready && status.local_checkpoint_available && status.remote_tip_height > 0)
        {
            status.lag_blocks = status.remote_tip_height > status.local_checkpoint
                                    ? status.remote_tip_height - status.local_checkpoint
                                    : 0;
            status.stale = status.lag_blocks > max_lag_blocks;
        }
        if (status.raw.is_object())
        {
            status.raw["remote_tip_height"] = status.remote_tip_height;
            status.raw["lag_blocks"] = status.lag_blocks;
            status.raw["stale"] = status.stale;
        }
        return status;
    }

    StateRefreshResult MidnightClient::refresh_state(
        const ledger::SyncLedgerStateParams &params,
        const StateRefreshOptions &options)
    {
        if (!state_backend_)
        {
            state_backend_ = make_state_backend_for_config(config_, ledger_backend_);
        }
        return state_backend_->refresh(params, options);
    }

    StateRefreshResult MidnightClient::ensure_state_fresh(
        const ledger::SyncLedgerStateParams &params,
        const StateRefreshOptions &options)
    {
        StateRefreshResult result;
        result.before = state_status(params.source, options.max_lag_blocks);
        if (!options.force &&
            result.before.ready &&
            (!result.before.local_checkpoint_available ||
             result.before.lag_blocks <= options.max_lag_blocks))
        {
            result.success = true;
            result.skipped = true;
            result.after = result.before;
            return result;
        }
        return refresh_state(params, options);
    }

    SnapshotResult MidnightClient::export_state_snapshot(
        const ledger::SourceConfig &source,
        const std::filesystem::path &snapshot_dir)
    {
        if (!state_backend_)
        {
            state_backend_ = make_state_backend_for_config(config_, ledger_backend_);
        }
        return state_backend_->export_snapshot(source, snapshot_dir);
    }

    SnapshotResult MidnightClient::import_state_snapshot(
        const std::filesystem::path &snapshot_dir,
        const ledger::SourceConfig &destination)
    {
        if (!state_backend_)
        {
            state_backend_ = make_state_backend_for_config(config_, ledger_backend_);
        }
        return state_backend_->import_snapshot(snapshot_dir, destination);
    }

    BuildSubmitResult MidnightClient::submit_build_result(ledger::BuildResult build)
    {
        return submit_built_transaction(*this, std::move(build));
    }

    BuildSubmitResult MidnightClient::transfer_night(const ledger::TransferNightParams &params)
    {
        if (auto invalid = validate_transfer_params(params); invalid.error_info)
        {
            return invalid;
        }
        return submit_build_result(build_transfer_night(params));
    }

    BuildSubmitResult MidnightClient::register_dust(const ledger::RegisterDustParams &params)
    {
        if (auto invalid = validate_register_dust_params(params); invalid.error_info)
        {
            return invalid;
        }
        return submit_build_result(build_register_dust(params));
    }

    BuildSubmitResult MidnightClient::deregister_dust(const ledger::DeregisterDustParams &params)
    {
        if (auto invalid = validate_deregister_dust_params(params); invalid.error_info)
        {
            return invalid;
        }
        return submit_build_result(build_deregister_dust(params));
    }

    BuildSubmitResult MidnightClient::deploy_simple_contract(
        const ledger::SimpleContractDeployParams &params)
    {
        if (auto invalid = validate_deploy_params(params); invalid.error_info)
        {
            return invalid;
        }
        return submit_build_result(build_simple_contract_deploy(params));
    }

    BuildSubmitResult MidnightClient::call_simple_contract(
        const ledger::SimpleContractCallParams &params)
    {
        if (auto invalid = validate_call_params(params); invalid.error_info)
        {
            return invalid;
        }
        return submit_build_result(build_simple_contract_call(params));
    }

    BuildSubmitResult MidnightClient::submit_custom_contract_intents(
        const ledger::CustomContractIntentParams &params)
    {
        if (auto invalid = validate_custom_contract_params(params); invalid.error_info)
        {
            return invalid;
        }
        return submit_build_result(build_custom_contract_transaction(params));
    }

    PipelineResult MidnightClient::transfer_night(
        const PipelineOptions &options,
        const ledger::TransferNightParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "transfer-night",
            transfer_night(params));
    }

    PipelineResult MidnightClient::register_dust(
        const PipelineOptions &options,
        const ledger::RegisterDustParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "register-dust",
            register_dust(params));
    }

    PipelineResult MidnightClient::deregister_dust(
        const PipelineOptions &options,
        const ledger::DeregisterDustParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "deregister-dust",
            deregister_dust(params));
    }

    PipelineResult MidnightClient::deploy_simple_contract(
        const PipelineOptions &options,
        const ledger::SimpleContractDeployParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "deploy-simple-contract",
            deploy_simple_contract(params));
    }

    PipelineResult MidnightClient::call_simple_contract(
        const PipelineOptions &options,
        const ledger::SimpleContractCallParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "call-simple-contract",
            call_simple_contract(params));
    }

    PipelineResult MidnightClient::submit_custom_contract_intents(
        const PipelineOptions &options,
        const ledger::CustomContractIntentParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "custom-contract-intents",
            submit_custom_contract_intents(params));
    }

    ConfirmationResult MidnightClient::wait_for_transaction(
        const std::string &transaction_hash,
        const ConfirmationConfig &confirmation)
    {
        return wait_for_inclusion(transaction_hash, confirmation);
    }

    ConfirmationResult MidnightClient::wait_for_inclusion(
        const std::string &transaction_hash,
        const ConfirmationConfig &confirmation)
    {
        ConfirmationResult result;
        result.transaction_hash = transaction_hash;
        if (!validation::is_tx_hash(transaction_hash))
        {
            result.error = make_error(
                ErrorCode::InvalidHash,
                "Transaction hash must be a 32-byte hex value",
                transaction_hash);
            return result;
        }

        const auto deadline = std::chrono::steady_clock::now() + confirmation.timeout;
        while (std::chrono::steady_clock::now() <= deadline)
        {
            try
            {
                auto tx = query_transaction(transaction_hash);
                result = confirmation_from_transaction(transaction_hash, tx);
                if (result.success)
                {
                    return result;
                }
            }
            catch (const std::exception &e)
            {
                result.error = make_error(
                    ErrorCode::IndexerError,
                    "Indexer transaction query failed",
                    e.what());
            }

            std::this_thread::sleep_for(confirmation.poll_interval);
        }

        if (!result.error)
        {
            result.error = make_error(
                ErrorCode::ConfirmationTimeout,
                "Timed out waiting for transaction confirmation",
                transaction_hash);
        }
        return result;
    }

    ConfirmationResult MidnightClient::wait_for_finality(
        const std::string &transaction_hash,
        const ConfirmationConfig &confirmation)
    {
        auto result = wait_for_inclusion(transaction_hash, confirmation);
        if (!result.success)
        {
            return result;
        }

        const auto deadline = std::chrono::steady_clock::now() + confirmation.timeout;
        network::NetworkClient rpc(config_.node_url, config_.node_timeout_ms);
        while (std::chrono::steady_clock::now() <= deadline)
        {
            try
            {
                const auto finalized_hash =
                    rpc_post(rpc, "chain_getFinalizedHead", json::array()).get<std::string>();
                const auto header =
                    rpc_post(rpc, "chain_getHeader", json::array({finalized_hash}));
                const uint64_t finalized_height = parse_header_height(header);
                if (finalized_height >= result.block_height && result.block_height > 0)
                {
                    const auto canonical_hash = rpc_post(
                        rpc,
                        "chain_getBlockHash",
                        json::array({result.block_height})).get<std::string>();
                    if (normalize_hash(canonical_hash) == normalize_hash(result.block_hash))
                    {
                        result.success = true;
                        return result;
                    }
                    result.success = false;
                    result.error = make_error(
                        ErrorCode::FinalityTimeout,
                        "Transaction block is not on the finalized canonical chain",
                        result.block_hash);
                    return result;
                }
            }
            catch (const std::exception &e)
            {
                result.error = make_error(
                    ErrorCode::NodeRpcError,
                    "Finalized head query failed",
                    e.what());
            }
            std::this_thread::sleep_for(confirmation.poll_interval);
        }

        result.success = false;
        result.error = make_error(
            ErrorCode::FinalityTimeout,
            "Timed out waiting for transaction finality",
            transaction_hash);
        return result;
    }

    std::vector<uint8_t> MidnightClient::check_payload(
        const std::vector<uint8_t> &check_payload)
    {
        if (!proof_server_)
        {
            throw std::runtime_error("Proof server URL is not configured");
        }
        return proof_server_->post_check_payload(check_payload);
    }

    std::vector<uint8_t> MidnightClient::prove_payload(
        const std::vector<uint8_t> &proving_payload)
    {
        if (!proof_server_)
        {
            throw std::runtime_error("Proof server URL is not configured");
        }
        return proof_server_->post_proving_payload(proving_payload);
    }

    std::vector<uint8_t> MidnightClient::prove_transaction_payload(
        const std::vector<uint8_t> &prove_tx_payload)
    {
        if (!proof_server_)
        {
            throw std::runtime_error("Proof server URL is not configured");
        }
        return proof_server_->post_prove_tx_payload(prove_tx_payload);
    }

    json MidnightClient::query_transaction(const std::string &tx_hash)
    {
        return indexer_->query_transaction(tx_hash);
    }

    json MidnightClient::query_block(uint64_t height)
    {
        return block_info_to_json(indexer_->query_block(height));
    }

    json MidnightClient::query_block(const std::string &block_hash)
    {
        return block_info_to_json(indexer_->query_block(0, block_hash));
    }

    network::WalletState MidnightClient::query_wallet_state(const std::string &address)
    {
        if (!validation::is_unshielded_night_address(address))
        {
            throw std::invalid_argument("Wallet state queries require a valid mn_addr Midnight address");
        }
        return indexer_->query_wallet_state(address);
    }

    network::WalletState MidnightClient::query_wallet_state(
        const std::string &address,
        uint32_t from_block,
        uint32_t to_block)
    {
        if (!validation::is_unshielded_night_address(address))
        {
            throw std::invalid_argument("Wallet state queries require a valid mn_addr Midnight address");
        }
        return indexer_->query_wallet_state(address, from_block, to_block);
    }

    network::WalletState MidnightClient::query_wallet_state_from_transaction(
        const std::string &address,
        const std::string &tx_hash)
    {
        if (!validation::is_unshielded_night_address(address))
        {
            throw std::invalid_argument("Wallet state queries require a valid mn_addr Midnight address");
        }
        if (!validation::is_tx_hash(tx_hash))
        {
            throw std::invalid_argument("Transaction hash must be a 32-byte hex value");
        }
        return indexer_->query_wallet_state_from_transaction(address, tx_hash);
    }

    json MidnightClient::query_cached_wallet_summary(const ledger::WalletSummaryParams &params)
    {
        if (auto invalid = validate_seed(params.wallet_seed, "Wallet summary wallet_seed"); invalid.error_info)
        {
            throw std::invalid_argument(invalid.error_info.message);
        }
        const auto result = ledger_backend_->wallet_summary(params);
        if (!result.success)
        {
            throw std::runtime_error(result.error.empty()
                                         ? "Native Midnight ledger wallet_summary failed"
                                         : result.error);
        }
        if (result.raw_output.empty())
        {
            return json::object();
        }
        try
        {
            return json::parse(result.raw_output);
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(std::string("Invalid Midnight wallet_summary raw output: ") + e.what());
        }
    }

    json MidnightClient::query_contract_state(const std::string &contract_address_hex)
    {
        if (!validation::is_contract_address_hex(contract_address_hex))
        {
            throw std::invalid_argument(
                "Contract state queries require a 32-byte contract hex address, not a wallet Bech32 address");
        }
        return node_->get_contract_state(contract_address_hex);
    }

} // namespace midnight::production
