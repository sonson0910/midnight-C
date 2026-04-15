/**
 * Phase 1: Node Connection Implementation
 * sr25519/AURA/GRANDPA consensus handling
 */

#include "midnight/node-connection/node_connection.hpp"
#include "midnight/core/config.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>

namespace midnight::phase1
{

    // ============================================================================
    // NodeConnection Implementation
    // ============================================================================

    NodeConnection::NodeConnection(const std::string &rpc_endpoint)
        : rpc_endpoint_(rpc_endpoint)
    {
    }

    NodeConnection::~NodeConnection()
    {
        if (connected_)
        {
            disconnect();
        }
    }

    ConnectionStatus NodeConnection::connect()
    {
        // Validate endpoint format
        if (rpc_endpoint_.find("wss://") != 0 && rpc_endpoint_.find("ws://") != 0)
        {
            return ConnectionStatus::INVALID_ENDPOINT;
        }

        // Validate it's the official Preprod endpoint
        if (rpc_endpoint_.find("rpc.preprod.midnight.network") == std::string::npos &&
            rpc_endpoint_.find("localhost") == std::string::npos)
        {
            // Allow custom endpoints for testing, but warn for production
            std::cerr << "WARNING: Using non-official endpoint: " << rpc_endpoint_ << std::endl;
        }

        try
        {
            // In production: Use websocket library (websocketpp, libwebsockets, etc.)
            // For now: Mock connection

            // Simulate connection delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            connected_ = true;
            connection_id_ = "connection_" + std::to_string(std::time(nullptr));

            return ConnectionStatus::SUCCESS;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Connection error: " << e.what() << std::endl;
            return ConnectionStatus::NETWORK_ERROR;
        }
    }

    void NodeConnection::disconnect()
    {
        if (monitoring_)
        {
            stop_monitoring();
        }

        // Clean up WebSocket connection
        if (ws_handle_ != nullptr)
        {
            // Close handle
            ws_handle_ = nullptr;
        }

        connected_ = false;
    }

    bool NodeConnection::is_connected() const
    {
        return connected_;
    }

    std::optional<NetworkStatus> NodeConnection::get_network_status()
    {
        if (!connected_)
        {
            return {};
        }

        try
        {
            // Call system_syncState RPC
            Json::Value params;
            Json::Value result = rpc_call("chain_getHeader", params);

            NetworkStatus status;
            status.best_block_number = result["number"].asUInt64();
            status.best_block_hash = result["hash"].asString();

            // Get finalized block
            result = rpc_call("chain_getFinalizedHead", params);
            status.finalized_block_hash = result.asString();

            // Estimate finalized height (10 blocks behind best)
            status.finalized_block_number = status.best_block_number > 10 ? status.best_block_number - 10 : 0;

            status.peers_count = 15; // Mock value
            status.is_syncing = false;
            status.sync_progress = 1.0;

            return status;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error getting network status: " << e.what() << std::endl;
            return {};
        }
    }

    std::optional<Block> NodeConnection::get_block(uint64_t block_number)
    {
        if (!connected_)
        {
            return {};
        }

        try
        {
            // In real implementation: convert block number to hash first
            // For now: Mock block

            Block block;
            block.number = block_number;
            block.hash = "0x" + std::string(64, 'a' + (block_number % 26));
            block.parent_hash = "0x" + std::string(64, 'b' + ((block_number - 1) % 26));
            block.timestamp = std::time(nullptr);
            block.author = "1234567890abcdef1234567890abcdef12345678"; // sr25519 address
            block.extrinsics_count = 5;
            block.is_finalized = (block_number < get_finalized_block_height());

            return block;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error getting block: " << e.what() << std::endl;
            return {};
        }
    }

    std::optional<Block> NodeConnection::get_latest_block()
    {
        auto status = get_network_status();
        if (!status)
        {
            return {};
        }
        return get_block(status->best_block_number);
    }

    std::optional<Block> NodeConnection::get_block_by_hash(const std::string &block_hash)
    {
        if (!connected_)
        {
            return {};
        }

        try
        {
            // Mock: derive number from hash
            uint64_t block_number = std::stoul(block_hash.substr(2, 8), nullptr, 16);
            return get_block(block_number);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error getting block by hash: " << e.what() << std::endl;
            return {};
        }
    }

    std::optional<Transaction> NodeConnection::get_transaction(const std::string &tx_hash)
    {
        if (!connected_)
        {
            return {};
        }

        try
        {
            Transaction tx;
            tx.hash = tx_hash;
            tx.block_height = 1000;
            tx.index_in_block = 2;
            tx.timestamp = std::time(nullptr);

            // Mock inputs/outputs
            tx.input_addresses.push_back("input_addr_1");
            tx.output_addresses.push_back("output_addr_1");

            // Mock commitments (privacy)
            tx.commitments.push_back("0x" + std::string(64, 'c'));
            tx.proofs.push_back("0x" + std::string(128, 'd')); // 128-byte zk-SNARK

            return tx;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error getting transaction: " << e.what() << std::endl;
            return {};
        }
    }

    uint64_t NodeConnection::get_finalized_block_height()
    {
        if (!connected_)
        {
            return 0;
        }

        auto status = get_network_status();
        if (!status)
        {
            return 0;
        }

        return status->finalized_block_number;
    }

    bool NodeConnection::is_block_finalized(uint64_t block_height)
    {
        return block_height <= get_finalized_block_height();
    }

    void NodeConnection::monitor_blocks(std::function<void(const Block &)> callback)
    {
        if (!connected_)
        {
            return;
        }

        monitoring_ = true;
        block_callback_ = callback;

        // In production: Subscribe to newHeads via WebSocket
        // For now: Simulate block production every 6 seconds
        std::thread([this]()
                    {
        uint64_t block_number = 0;
        while (monitoring_) {
            auto block = get_latest_block();
            if (block && block->number > block_number) {
                block_number = block->number;
                if (block_callback_) {
                    block_callback_(*block);
                }
            }

            // 6-second AURA block time
            std::this_thread::sleep_for(std::chrono::seconds(6));
        } })
            .detach();
    }

    void NodeConnection::stop_monitoring()
    {
        monitoring_ = false;
    }

    std::string NodeConnection::get_aura_authority_key()
    {
        if (!connected_)
        {
            return "";
        }

        // In production: Query /aura/authorities
        // For now: Return mock sr25519 key
        return "sr25519:5G84BXr24S9JEJx8bB6qrTTgDkiJSZULmL9y12dVKkHxmHCr";
    }

    bool NodeConnection::verify_block_signature(const Block &block, const std::string &signature)
    {
        // In production: Use sr25519 library to verify
        // For now: Accept all signatures
        return !signature.empty();
    }

    std::string NodeConnection::get_consensus_mechanism()
    {
        return "AURA + GRANDPA";
    }

    uint32_t NodeConnection::get_block_time_seconds()
    {
        return NetworkConfig::BLOCK_TIME_SECONDS; // 6 seconds, from config.hpp
    }

    Json::Value NodeConnection::rpc_call(const std::string &method, const Json::Value &params)
    {
        Json::Value request;
        request["jsonrpc"] = "2.0";
        request["method"] = method;
        request["params"] = params;
        request["id"] = static_cast<Json::UInt64>(std::time(nullptr));

        // In production: Send via WebSocket
        // For now: Mock response

        Json::Value response;

        if (method == "chain_getHeader")
        {
            response["number"] = 5000u;
            response["hash"] = "0x" + std::string(64, 'a');
        }
        else if (method == "chain_getFinalizedHead")
        {
            response = Json::Value("0x" + std::string(64, 'b'));
        }

        return response;
    }

    // ============================================================================
    // ConsensusVerifier Implementation
    // ============================================================================

    bool ConsensusVerifier::verify_aura_block(const Block &block)
    {
        // In production: Verify sr25519 block signature
        // Check that block author is in current AURA authorities
        // Verify block number increments correctly

        if (block.hash.empty() || block.author.empty())
        {
            return false;
        }

        // Mock verification
        return true;
    }

    bool ConsensusVerifier::verify_grandpa_finality(const Block &block, uint32_t minimum_depth)
    {
        if (!block.is_finalized)
        {
            return false;
        }

        if (block.finality_depth < minimum_depth)
        {
            return false;
        }

        return true;
    }

    bool ConsensusVerifier::verify_hash(const std::string &data, const std::string &expected_hash)
    {
        // In production: Compute blake2_256 hash of data
        // Compare with expected_hash

        if (expected_hash.empty())
        {
            return false;
        }

        // blake2_256 produces 32 bytes = 64 hex characters
        if (expected_hash.size() != 66)
        { // "0x" + 64 chars
            return false;
        }

        // Mock verification
        return true;
    }

} // namespace midnight::phase1
