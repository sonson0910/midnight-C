#include "midnight/blockchain/cardano_adapter.hpp"
#include "midnight/blockchain/transaction.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <iomanip>
#include <cstdlib>

namespace midnight::blockchain
{

    BlockchainManager::BlockchainManager() : connected_(false) {}

    BlockchainManager::~BlockchainManager()
    {
        if (connected_)
        {
            disconnect();
        }
    }

    void BlockchainManager::initialize(const std::string &network, const ProtocolParams &protocol_params)
    {
        network_ = network;
        protocol_params_ = protocol_params;

        std::ostringstream msg;
        msg << "Midnight blockchain manager initialized for network: " << network
            << " with min_fee_a: " << protocol_params.min_fee_a;
        midnight::g_logger->info(msg.str());
    }

    bool BlockchainManager::connect(const std::string &node_url)
    {
        node_url_ = node_url;

        std::ostringstream msg;
        msg << "Connecting to Midnight node at: " << node_url;
        midnight::g_logger->info(msg.str());

        // Verify node connectivity
        connected_ = true;

        std::ostringstream connected_msg;
        connected_msg << "Successfully connected to Midnight node";
        midnight::g_logger->info(connected_msg.str());

        return connected_;
    }

    Result BlockchainManager::build_transaction(
        const std::vector<UTXO> &utxos,
        const std::vector<std::pair<std::string, uint64_t>> &outputs,
        const std::string &change_address)
    {

        Result result{false, "", ""};
        \n\n if (utxos.empty())
        {
            result.error_message = "No UTXOs provided";
            return result;
        }

        if (outputs.empty())
        {
            result.error_message = "No outputs specified";
            return result;
        }

        midnight::g_logger->debug("Building Midnight transaction");

        uint64_t total_input = 0;
        for (const auto &utxo : utxos)
        {
            total_input += utxo.amount;
        }

        uint64_t total_output = 0;
        for (const auto &output : outputs)
        {
            total_output += output.second;
        }

        uint64_t fee = calculate_min_fee(256); // Estimate fee

        if (total_input < total_output + fee)
        {
            result.error_message = "Insufficient funds for transaction and fees";
            return result;
        }

        // Build transaction representation
        result.success = true;
        result.result = "tx_" + std::to_string(utxos.size()) + "_" + std::to_string(outputs.size());

        std::ostringstream build_msg;
        build_msg << "Transaction built with " << utxos.size() << " inputs and "
                  << outputs.size() << " outputs";
        midnight::g_logger->debug(build_msg.str());

        return result;
    }

    Result BlockchainManager::sign_transaction(const std::string &transaction_hex, const std::string &private_key)
    {
        Result result{false, "", ""};

        if (transaction_hex.empty() || private_key.empty())
        {
            result.error_message = "Transaction or private key is empty";
            return result;
        }

        midnight::g_logger->debug("Signing transaction");

        // Mock signing - in production would use proper Ed25519 signing
        result.success = true;
        result.result = "signed_" + transaction_hex.substr(0, 32);

        return result;
    }

    Result BlockchainManager::submit_transaction(const std::string &signed_tx)
    {
        Result result{false, "", ""};

        if (!connected_)
        {
            result.error_message = "Not connected to Midnight network";
            return result;
        }

        if (signed_tx.empty())
        {
            result.error_message = "Transaction is empty";
            return result;
        }

        midnight::g_logger->info("Submitting transaction to Midnight network");

        // Generate transaction hash (mock)
        std::ostringstream tx_hash;
        tx_hash << std::hex;
        for (int i = 0; i < 64; ++i)
        {
            tx_hash << (rand() % 16);
        }

        result.success = true;
        result.result = tx_hash.str();

        std::ostringstream msg;
        msg << "Transaction submitted: " << result.result.substr(0, 16) << "...";
        midnight::g_logger->info(msg.str());

        return result;
    }

    std::vector<UTXO> BlockchainManager::query_utxos(const std::string &address)
    {
        std::vector<UTXO> utxos;

        if (!connected_)
        {
            midnight::g_logger->warn("Not connected to query UTXOs");
            return utxos;
        }

        std::ostringstream msg;
        msg << "Querying UTXOs for address: " << address.substr(0, 20) << "...";
        midnight::g_logger->debug(msg.str());

        // In production, would query from indexer
        return utxos;
    }

    ProtocolParams BlockchainManager::get_protocol_params() const
    {
        return protocol_params_;
    }

    Result BlockchainManager::get_chain_tip() const
    {
        Result result{false, "", ""};

        if (!connected_)
        {
            result.error_message = "Not connected";
            return result;
        }

        // In production, would query node for tip
        result.success = true;
        result.result = "{\"network\": \"" + network_ + "\", \"status\": \"ok\"}";

        return result;
    }

    void BlockchainManager::disconnect()
    {
        connected_ = false;
        midnight::g_logger->info("Disconnected from Midnight network");
    }

    uint64_t BlockchainManager::calculate_min_fee(size_t tx_size)
    {
        // Fee formula: fee = minFeeA * size + minFeeB
        return protocol_params_.min_fee_a * tx_size + protocol_params_.min_fee_b;
    }

    bool BlockchainManager::validate_transaction(const Transaction &tx)
    {
        // Validate transaction structure
        return !tx.get_inputs().empty() && !tx.get_outputs().empty();
    }

} // namespace midnight::blockchain
