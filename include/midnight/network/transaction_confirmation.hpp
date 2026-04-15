#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <chrono>
#include "retry_config.hpp"

using json = nlohmann::json;

namespace midnight::network
{

    /**
     * Transaction Confirmation Monitor
     *
     * Polls blockchain for transaction confirmation
     * Implements exponential backoff for polling intervals
     */

    class TransactionConfirmationMonitor
    {
    public:
        struct ConfirmationResult
        {
            bool confirmed = false;
            uint32_t current_block_height = 0;
            uint32_t confirmation_block = 0;
            uint32_t confirmations = 0; // Number of blocks after tx block
            std::string status;         // "pending", "submitted_or_unknown", "timeout", "rpc_error"
        };

        /**
         * Constructor
         * @param base_url RPC endpoint URL (e.g., https://rpc.preprod.midnight.network)
         * @param timeout_ms Request timeout in milliseconds
         */
        TransactionConfirmationMonitor(
            const std::string &base_url,
            uint32_t timeout_ms = 5000);

        ~TransactionConfirmationMonitor();

        /**
         * Wait for transaction confirmation with polling
         *
         * @param tx_id Transaction ID to monitor
         * @param max_blocks Maximum blocks to wait (0 = wait indefinitely)
         * @param poll_interval_ms Initial polling interval (doubled after each poll)
         * @return ConfirmationResult with confirmation status
         *
         * @throws std::runtime_error if RPC calls fail
         */
        ConfirmationResult wait_for_confirmation(
            const std::string &tx_id,
            uint32_t max_blocks = 24, // ~2 minutes on Midnight
            uint32_t poll_interval_ms = 1000);

        /**
         * Check transaction status without waiting
         * Single RPC call to get current status
         *
         * @param tx_id Transaction ID
         * @return Current confirmation status
         */
        ConfirmationResult check_status(const std::string &tx_id);

        /**
         * Get network statistics
         */
        const NetworkStats &get_stats() const { return stats_; }

        /**
         * Reset statistics
         */
        void reset_stats() { stats_.reset(); }

    private:
        std::string rpc_url_;
        uint32_t timeout_ms_;
        NetworkStats stats_;

        // RPC call methods
        ConfirmationResult query_tx_status_(const std::string &tx_id);
        uint32_t query_chain_tip_();
    };

} // namespace midnight::network
