#pragma once

#include "midnight/network/indexer_client.hpp"
#include "midnight/wallet/wallet_facade.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace midnight::network {

using json = nlohmann::json;

/**
 * @brief Callback for unshielded transaction events
 *
 * Matches @midnight-ntwrk/wallet-sdk-indexer-client UnshieldedTransactions subscription.
 * Called when new UTXOs are created or spent for a given address.
 */
using UnshieldedTxCallback = std::function<void(
    const std::vector<wallet::UtxoWithMeta>& created,
    const std::vector<wallet::UtxoWithMeta>& spent
)>;

/**
 * @brief Callback for balance change notifications
 */
using BalanceCallback = std::function<void(const std::string& token_type, uint64_t new_balance)>;

/**
 * @brief Polling-based indexer subscription client
 *
 * C++ equivalent of the WebSocket subscription system in
 * @midnight-ntwrk/wallet-sdk-indexer-client.
 *
 * Since the C++ runtime doesn't have a native WebSocket GraphQL
 * subscription transport, we implement the same semantics via
 * HTTP polling with configurable intervals.
 *
 * @code
 *   IndexerSubscription sub("https://indexer...", "mn_addr_preview1...");
 *   sub.on_balance_change([](auto token, auto balance) {
 *       std::cout << token << ": " << balance << std::endl;
 *   });
 *   sub.start(5000); // poll every 5s
 *   // ...
 *   sub.stop();
 * @endcode
 */
class IndexerSubscription {
public:
    explicit IndexerSubscription(const std::string& graphql_url,
                                  const std::string& address);
    ~IndexerSubscription();

    // Non-copyable, movable
    IndexerSubscription(const IndexerSubscription&) = delete;
    IndexerSubscription& operator=(const IndexerSubscription&) = delete;
    IndexerSubscription(IndexerSubscription&&) noexcept;
    IndexerSubscription& operator=(IndexerSubscription&&) noexcept;

    /**
     * @brief Register callback for unshielded transaction events
     */
    void on_unshielded_tx(UnshieldedTxCallback callback);

    /**
     * @brief Register callback for balance changes
     */
    void on_balance_change(BalanceCallback callback);

    /**
     * @brief Start polling
     * @param interval_ms Polling interval in milliseconds (default 5000)
     */
    void start(uint32_t interval_ms = 5000);

    /**
     * @brief Stop polling
     */
    void stop();

    /**
     * @brief Check if subscription is active
     */
    bool is_running() const;

    /**
     * @brief Get last known state
     */
    WalletState last_state() const;

private:
    std::string graphql_url_;
    std::string address_;

    std::vector<UnshieldedTxCallback> tx_callbacks_;
    std::vector<BalanceCallback> balance_callbacks_;

    std::thread poll_thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;

    WalletState last_state_;
    uint64_t last_known_balance_ = 0;
    uint64_t last_utxo_count_ = 0;

    void poll_loop(uint32_t interval_ms);
};

} // namespace midnight::network
