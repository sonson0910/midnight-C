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
        ConfirmationResult result;

        try
        {
            result.current_block_height = query_chain_tip_();
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->debug("Could not fetch chain tip while checking tx status: " + std::string(e.what()));
        }

        // Create RPC client for this query
        NetworkClient rpc_client(rpc_url_, timeout_ms_);

        try
        {
            // Query mempool via Substrate RPC to detect pending extrinsic presence.
            json request = {
                {"jsonrpc", "2.0"},
                {"method", "author_pendingExtrinsics"},
                {"params", json::array()},
                {"id", 1}};

            auto response = rpc_client.post_json("/", request);

            if (response.contains("result") && response["result"].is_array())
            {
                bool found_in_pending = false;
                for (const auto &ext : response["result"])
                {
                    if (!ext.is_string())
                    {
                        continue;
                    }

                    const std::string raw_extrinsic = ext.get<std::string>();
                    if (raw_extrinsic == tx_id ||
                        (tx_id.rfind("0x", 0) == 0 && tx_id.size() > 10 &&
                         raw_extrinsic.find(tx_id.substr(2)) != std::string::npos))
                    {
                        found_in_pending = true;
                        break;
                    }
                }

                result.status = found_in_pending ? "pending" : "submitted_or_unknown";
                result.confirmed = false;
                return result;
            }
            else if (response.contains("error"))
            {
                result.status = "rpc_error";
                result.confirmed = false;
                return result;
            }

            result.status = "rpc_error";
            return result;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("RPC error querying transaction: " + std::string(e.what()));
            result.status = "rpc_error";
            result.confirmed = false;
            return result;
        }
    }

    uint32_t TransactionConfirmationMonitor::query_chain_tip_()
    {
        // Create RPC client for this query
        NetworkClient rpc_client(rpc_url_, timeout_ms_);

        try
        {
            // JSON-RPC 2.0 call to get latest header.
            json request = {
                {"jsonrpc", "2.0"},
                {"method", "chain_getHeader"},
                {"params", json::array()},
                {"id", 1}};

            auto response = rpc_client.post_json("/", request);

            if (response.contains("result") && response["result"].is_object())
            {
                const auto &header = response["result"];
                if (header.contains("number"))
                {
                    if (header["number"].is_string())
                    {
                        return static_cast<uint32_t>(std::stoul(header["number"].get<std::string>(), nullptr, 0));
                    }
                    if (header["number"].is_number_unsigned())
                    {
                        return header["number"].get<uint32_t>();
                    }
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
