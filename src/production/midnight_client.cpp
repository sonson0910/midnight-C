#include "midnight/production/midnight_client.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/production/validation.hpp"
#include <chrono>
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
    }

    MidnightClient::MidnightClient(const ClientConfig &config)
        : config_(config)
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
        if (hex.empty() || !midnight::util::is_hex_string(hex))
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
                result.error_info = make_error(ErrorCode::NodeRpcError, result.error);
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            result.error_info = make_error(ErrorCode::NodeRpcError, "Midnight transaction submission failed", e.what());
        }
        return result;
    }

    SubmitResult MidnightClient::submit_extrinsic_hex(const std::string &extrinsic_hex)
    {
        SubmitResult result;
        const auto hex = midnight::util::strip_hex_prefix(extrinsic_hex);
        if (hex.empty() || !midnight::util::is_hex_string(hex))
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
                result.error_info = make_error(ErrorCode::NodeRpcError, result.error);
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            result.error_info = make_error(ErrorCode::NodeRpcError, "Extrinsic submission failed", e.what());
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
                result.error = result.build.error.empty()
                    ? "Transaction build failed"
                    : result.build.error;
                result.error_info = make_error(
                    ErrorCode::LedgerBuildError,
                    "Transaction build failed",
                    result.error);
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
    }

    BuildSubmitResult MidnightClient::transfer_night(
        const ledger::ToolkitConfig &toolkit,
        const ledger::TransferNightParams &params)
    {
        auto cfg = toolkit;
        if (cfg.proof_server_url.empty())
        {
            cfg.proof_server_url = config_.proof_server_url;
        }
        ledger::TransactionBuilder builder(cfg);
        return submit_built_transaction(*this, builder.build_transfer_night(params));
    }

    BuildSubmitResult MidnightClient::register_dust(
        const ledger::ToolkitConfig &toolkit,
        const ledger::RegisterDustParams &params)
    {
        auto cfg = toolkit;
        if (cfg.proof_server_url.empty())
        {
            cfg.proof_server_url = config_.proof_server_url;
        }
        ledger::TransactionBuilder builder(cfg);
        return submit_built_transaction(*this, builder.build_register_dust(params));
    }

    BuildSubmitResult MidnightClient::deregister_dust(
        const ledger::ToolkitConfig &toolkit,
        const ledger::DeregisterDustParams &params)
    {
        auto cfg = toolkit;
        if (cfg.proof_server_url.empty())
        {
            cfg.proof_server_url = config_.proof_server_url;
        }
        ledger::TransactionBuilder builder(cfg);
        return submit_built_transaction(*this, builder.build_deregister_dust(params));
    }

    BuildSubmitResult MidnightClient::deploy_simple_contract(
        const ledger::ToolkitConfig &toolkit,
        const ledger::SimpleContractDeployParams &params)
    {
        auto cfg = toolkit;
        if (cfg.proof_server_url.empty())
        {
            cfg.proof_server_url = config_.proof_server_url;
        }
        ledger::TransactionBuilder builder(cfg);
        return submit_built_transaction(*this, builder.build_simple_contract_deploy(params));
    }

    BuildSubmitResult MidnightClient::call_simple_contract(
        const ledger::ToolkitConfig &toolkit,
        const ledger::SimpleContractCallParams &params)
    {
        auto cfg = toolkit;
        if (cfg.proof_server_url.empty())
        {
            cfg.proof_server_url = config_.proof_server_url;
        }
        ledger::TransactionBuilder builder(cfg);
        return submit_built_transaction(*this, builder.build_simple_contract_call(params));
    }

    BuildSubmitResult MidnightClient::submit_custom_contract_intents(
        const ledger::ToolkitConfig &toolkit,
        const ledger::CustomContractIntentParams &params)
    {
        auto cfg = toolkit;
        if (cfg.proof_server_url.empty())
        {
            cfg.proof_server_url = config_.proof_server_url;
        }
        ledger::TransactionBuilder builder(cfg);
        return submit_built_transaction(*this, builder.build_custom_contract_transaction(params));
    }

    PipelineResult MidnightClient::transfer_night(
        const PipelineOptions &options,
        const ledger::TransferNightParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "transfer-night",
            transfer_night(options.toolkit, params));
    }

    PipelineResult MidnightClient::register_dust(
        const PipelineOptions &options,
        const ledger::RegisterDustParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "register-dust",
            register_dust(options.toolkit, params));
    }

    PipelineResult MidnightClient::deregister_dust(
        const PipelineOptions &options,
        const ledger::DeregisterDustParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "deregister-dust",
            deregister_dust(options.toolkit, params));
    }

    PipelineResult MidnightClient::deploy_simple_contract(
        const PipelineOptions &options,
        const ledger::SimpleContractDeployParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "deploy-simple-contract",
            deploy_simple_contract(options.toolkit, params));
    }

    PipelineResult MidnightClient::call_simple_contract(
        const PipelineOptions &options,
        const ledger::SimpleContractCallParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "call-simple-contract",
            call_simple_contract(options.toolkit, params));
    }

    PipelineResult MidnightClient::submit_custom_contract_intents(
        const PipelineOptions &options,
        const ledger::CustomContractIntentParams &params)
    {
        return pipeline_from_build_submit(
            *this,
            options,
            "custom-contract-intents",
            submit_custom_contract_intents(options.toolkit, params));
    }

    ConfirmationResult MidnightClient::wait_for_transaction(
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
        return indexer_->query_wallet_state(address);
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
