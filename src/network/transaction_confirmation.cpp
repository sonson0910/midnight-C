#include "midnight/network/transaction_confirmation.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/core/logger.hpp"
#include <thread>
#include <chrono>

namespace midnight::network
{

    TransactionConfirmationMonitor::TransactionConfirmationMonitor(
        const std::string &base_url,
        uint32_t timeout_ms) : rpc_url_(base_url), timeout_ms_(timeout_ms)
    {
        midnight::g_logger->info("TransactionConfirmationMonitor created for: " + base_url);
    }

    TransactionConfirmationMonitor::~TransactionConfirmationMonitor() = default;

    TransactionConfirmationMonitor::ConfirmationResult
    TransactionConfirmationMonitor::wait_for_confirmation(
        const std::string &tx_id,
        uint32_t max_blocks,
        uint32_t poll_interval_ms)
    {
        midnight::g_logger->info("Waiting for transaction confirmation: " + tx_id);
        midnight::g_logger->info("Max blocks: " + std::to_string(max_blocks) +
                                 ", Poll interval: " + std::to_string(poll_interval_ms) + "ms");

        auto start_time = std::chrono::high_resolution_clock::now();
        auto start_height = query_chain_tip_();

        uint32_t current_poll_interval = poll_interval_ms;
        const uint32_t MAX_POLL_INTERVAL_MS = 30000; // Cap at 30 seconds

        while (true)
        {
            // Check current status
            auto result = query_tx_status_(tx_id);
            stats_.total_requests++;

            if (result.confirmed)
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::high_resolution_clock::now() - start_time)
                                   .count();

                midnight::g_logger->info(
                    "Transaction confirmed! ID: " + tx_id +
                    " | Confirmations: " + std::to_string(result.confirmations) +
                    " | Time: " + std::to_string(elapsed) + "ms");
                stats_.successful_requests++;
                return result;
            }

            // Check if we've exceeded max blocks
            if (max_blocks > 0)
            {
                auto current_height = query_chain_tip_();
                auto blocks_passed = current_height - start_height;

                if (blocks_passed >= max_blocks)
                {
                    midnight::g_logger->warn(
                        "Timeout waiting for transaction: " + tx_id +
                        " | Blocks passed: " + std::to_string(blocks_passed) +
                        " | Max: " + std::to_string(max_blocks));
                    stats_.timeout_errors++;
                    result.status = "timeout";
                    return result;
                }
            }

            // Wait before next poll (exponential backoff)
            midnight::g_logger->debug(
                "Transaction not yet confirmed, waiting " +
                std::to_string(current_poll_interval) + "ms before next check");

            std::this_thread::sleep_for(
                std::chrono::milliseconds(current_poll_interval));

            // Increase poll interval gradually (up to MAX)
            current_poll_interval = std::min(
                static_cast<uint32_t>(current_poll_interval * 1.5),
                MAX_POLL_INTERVAL_MS);
        }
    }

    TransactionConfirmationMonitor::ConfirmationResult
    TransactionConfirmationMonitor::check_status(const std::string &tx_id)
    {
        midnight::g_logger->debug("Checking transaction status: " + tx_id);

        try
        {
            auto result = query_tx_status_(tx_id);
            stats_.total_requests++;

            if (result.confirmed)
            {
                stats_.successful_requests++;
            }

            return result;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to check transaction status: " + std::string(e.what()));
            stats_.failed_requests++;
            throw;
        }
    }

    TransactionConfirmationMonitor::ConfirmationResult
    TransactionConfirmationMonitor::query_tx_status_(const std::string &tx_id)
    {
        // Create RPC client for this query
        NetworkClient rpc_client(rpc_url_, timeout_ms_);

        try
        {
            // JSON-RPC 2.0 call to get transaction status
            json request = {
                {"jsonrpc", "2.0"},
                {"method", "getTransaction"},
                {"params", {tx_id}},
                {"id", 1}};

            auto response = rpc_client.post_json("/", request);

            ConfirmationResult result;

            if (response.contains("result"))
            {
                auto tx_result = response["result"];

                // Parse transaction status
                if (tx_result.contains("status"))
                {
                    result.status = tx_result["status"].get<std::string>();
                    result.confirmed = (result.status == "confirmed" || result.status == "finalized");
                }

                // Get confirmation details
                if (tx_result.contains("blockNumber"))
                {
                    result.confirmation_block = tx_result["blockNumber"].get<uint32_t>();
                }

                if (tx_result.contains("confirmations"))
                {
                    result.confirmations = tx_result["confirmations"].get<uint32_t>();
                }

                // Get current block height
                result.current_block_height = query_chain_tip_();

                return result;
            }
            else if (response.contains("error"))
            {
                // Transaction not found or RPC error
                result.status = "not_found";
                result.confirmed = false;
                return result;
            }

            return result;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("RPC error querying transaction: " + std::string(e.what()));
            throw;
        }
    }

    uint32_t TransactionConfirmationMonitor::query_chain_tip_()
    {
        // Create RPC client for this query
        NetworkClient rpc_client(rpc_url_, timeout_ms_);

        try
        {
            // JSON-RPC 2.0 call to get chain tip
            json request = {
                {"jsonrpc", "2.0"},
                {"method", "getChainTip"},
                {"params", {}},
                {"id", 1}};

            auto response = rpc_client.post_json("/", request);

            if (response.contains("result"))
            {
                if (response["result"].contains("height"))
                {
                    return response["result"]["height"].get<uint32_t>();
                }
            }

            throw std::runtime_error("Could not parse chain tip response");
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("RPC error querying chain tip: " + std::string(e.what()));
            throw;
        }
    }

} // namespace midnight::network
