#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/blockchain/transaction.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/crypto/ed25519_signer.hpp"
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>

namespace midnight::blockchain
{

    MidnightBlockchain::MidnightBlockchain() : connected_(false) {}

    MidnightBlockchain::~MidnightBlockchain()
    {
        if (connected_)
        {
            disconnect();
        }
    }

    void MidnightBlockchain::initialize(const std::string &network, const ProtocolParams &protocol_params)
    {
        network_ = network;
        protocol_params_ = protocol_params;

        std::ostringstream msg;
        msg << "Midnight blockchain manager initialized for network: " << network
            << " with min_fee_a: " << protocol_params.min_fee_a;
        midnight::g_logger->info(msg.str());
    }

    bool MidnightBlockchain::connect(const std::string &node_url)
    {
        node_url_ = node_url;

        std::ostringstream msg;
        msg << "Connecting to Midnight node at: " << node_url;
        midnight::g_logger->info(msg.str());

        try
        {
            // Create RPC client for node communication
            rpc_ = std::make_unique<midnight::network::MidnightNodeRPC>(node_url, 5000);

            // Verify node is ready
            if (!rpc_->is_ready())
            {
                midnight::g_logger->warn("Node is not ready yet");
                connected_ = false;
                return false;
            }

            // Fetch protocol parameters from node if not initialized
            if (protocol_params_.min_fee_a == 0)
            {
                protocol_params_ = rpc_->get_protocol_params();
                msg.str("");
                msg << "Fetched protocol params: min_fee_a=" << protocol_params_.min_fee_a;
                midnight::g_logger->info(msg.str());
            }

            connected_ = true;
            midnight::g_logger->info("Successfully connected to Midnight node");
            return true;
        }
        catch (const std::exception &e)
        {
            msg.str("");
            msg << "Failed to connect to node: " << e.what();
            midnight::g_logger->error(msg.str());
            connected_ = false;
            return false;
        }
    }

    std::string MidnightBlockchain::create_address(const std::string &public_key, uint8_t network_id)
    {
        if (public_key.empty())
        {
            return "";
        }

        const std::string prefix = (network_id == 0) ? "mn_addr_preprod1" : "mn_addr_mainnet1";
        return prefix + public_key.substr(0, std::min<size_t>(public_key.size(), 48));
    }

    bool MidnightBlockchain::validate_address(const std::string &address)
    {
        return address.rfind("mn_addr_", 0) == 0 || address.rfind("addr", 0) == 0;
    }

    Result MidnightBlockchain::build_transaction(
        const std::vector<UTXO> &utxos,
        const std::vector<std::pair<std::string, uint64_t>> &outputs,
        const std::string &change_address)
    {

        Result result{false, "", ""};
        if (utxos.empty())
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

    Result MidnightBlockchain::sign_transaction(const std::string &transaction_hex, const std::string &private_key)
    {
        Result result{false, "", ""};

        if (transaction_hex.empty() || private_key.empty())
        {
            result.error_message = "Transaction or private key is empty";
            midnight::g_logger->error(result.error_message);
            return result;
        }

        try
        {
            midnight::g_logger->info("Signing transaction with Ed25519...");

            // Initialize crypto library (idempotent - safe to call multiple times)
            midnight::crypto::Ed25519Signer::initialize();

            // Parse private key from hex string
            if (private_key.length() < 128)
            {
                result.error_message = "Private key must be at least 128 hex characters (64 bytes)";
                midnight::g_logger->error(result.error_message);
                return result;
            }

            // Convert hex private key to binary
            midnight::crypto::Ed25519Signer::PrivateKey priv_key_bytes;
            for (size_t i = 0; i < 64; ++i)
            {
                std::string byte_hex = private_key.substr(i * 2, 2);
                try
                {
                    priv_key_bytes[i] = static_cast<uint8_t>(std::stoul(byte_hex, nullptr, 16));
                }
                catch (...)
                {
                    result.error_message = "Invalid private key hex format";
                    midnight::g_logger->error(result.error_message);
                    return result;
                }
            }

            // Sign the transaction (transaction_hex is treated as the message)
            auto signature = midnight::crypto::Ed25519Signer::sign_message(
                transaction_hex,
                priv_key_bytes);

            // Convert signature to hex string
            std::string signature_hex = midnight::crypto::Ed25519Signer::signature_to_hex(signature);

            // Construct signed transaction: transaction + witness/signature
            result.success = true;
            result.result = transaction_hex + signature_hex; // Concatenate tx and signature

            midnight::g_logger->info("Transaction signed successfully");
            midnight::g_logger->debug("Signature length: " + std::to_string(signature_hex.length()) + " hex chars");

            return result;
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error_message = std::string("Signing failed: ") + e.what();
            midnight::g_logger->error(result.error_message);
            return result;
        }
    }

    Result MidnightBlockchain::submit_transaction(const std::string &signed_tx)
    {
        Result result{false, "", ""};

        if (!connected_ || !rpc_)
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

        try
        {
            // Submit via RPC node
            std::string tx_id = rpc_->submit_transaction(signed_tx);

            result.success = true;
            result.result = tx_id;

            std::ostringstream msg;
            msg << "Transaction submitted: " << tx_id.substr(0, 16) << "...";
            midnight::g_logger->info(msg.str());
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error_message = std::string("RPC submission failed: ") + e.what();

            std::ostringstream msg;
            msg << "Failed to submit transaction: " << e.what();
            midnight::g_logger->error(msg.str());
        }

        return result;
    }

    std::vector<UTXO> MidnightBlockchain::query_utxos(const std::string &address)
    {
        std::vector<UTXO> utxos;

        if (!connected_ || !rpc_)
        {
            midnight::g_logger->warn("Not connected to query UTXOs");
            return utxos;
        }

        std::ostringstream msg;
        msg << "Querying UTXOs for address: " << address.substr(0, 20) << "...";
        midnight::g_logger->debug(msg.str());

        try
        {
            // Query from RPC node
            utxos = rpc_->get_utxos(address);

            msg.str("");
            msg << "Found " << utxos.size() << " UTXOs";
            midnight::g_logger->debug(msg.str());
        }
        catch (const std::exception &e)
        {
            msg.str("");
            msg << "Failed to query UTXOs: " << e.what();
            midnight::g_logger->error(msg.str());
        }

        return utxos;
    }

    ProtocolParams MidnightBlockchain::get_protocol_params() const
    {
        return protocol_params_;
    }

    Result MidnightBlockchain::get_chain_tip() const
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

    void MidnightBlockchain::disconnect()
    {
        connected_ = false;
        rpc_ = nullptr; // Clean up RPC client
        midnight::g_logger->info("Disconnected from Midnight network");
    }

    uint64_t MidnightBlockchain::calculate_min_fee(size_t tx_size)
    {
        // Fee formula: fee = minFeeA * size + minFeeB
        return protocol_params_.min_fee_a * tx_size + protocol_params_.min_fee_b;
    }

    bool MidnightBlockchain::validate_transaction(const Transaction &tx)
    {
        // Validate transaction structure
        return !tx.get_inputs().empty() && !tx.get_outputs().empty();
    }

} // namespace midnight::blockchain
