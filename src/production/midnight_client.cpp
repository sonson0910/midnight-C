#include "midnight/production/midnight_client.hpp"
#include "midnight/core/common_utils.hpp"
#include <chrono>
#include <stdexcept>

namespace midnight::production
{
    namespace
    {
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
            return {.success = false, .error = "Midnight transaction bytes cannot be empty"};
        }

        return submit_midnight_transaction_hex(
            midnight::util::bytes_to_hex(transaction_bytes));
    }

    SubmitResult MidnightClient::submit_midnight_transaction_hex(
        const std::string &transaction_hex)
    {
        SubmitResult result;
        try
        {
            result.extrinsic_hash = node_->submit_transaction(transaction_hex);
            result.success = !result.extrinsic_hash.empty();
            if (!result.success)
            {
                result.error = "Node returned an empty extrinsic hash";
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        return result;
    }

    SubmitResult MidnightClient::submit_extrinsic_hex(const std::string &extrinsic_hex)
    {
        SubmitResult result;
        try
        {
            result.extrinsic_hash = node_->submit_extrinsic(extrinsic_hex);
            result.success = !result.extrinsic_hash.empty();
            if (!result.success)
            {
                result.error = "Node returned an empty extrinsic hash";
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
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
        return indexer_->query_block(height);
    }

    json MidnightClient::query_block(const std::string &block_hash)
    {
        return indexer_->query_block(block_hash);
    }

    network::WalletState MidnightClient::query_wallet_state(const std::string &address)
    {
        return indexer_->query_wallet_state(address);
    }

    json MidnightClient::query_contract_state(const std::string &contract_address_hex)
    {
        return node_->get_contract_state(contract_address_hex);
    }

} // namespace midnight::production
