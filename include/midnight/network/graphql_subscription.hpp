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
#include <queue>
#include <condition_variable>

namespace midnight::network {

using json = nlohmann::json;

/**
 * @brief UTXO with metadata, matches TypeScript SDK UtxoWithMeta
 */
struct UtxoEvent {
    std::string utxo_hash;      // tx_hash:output_index
    std::string tx_hash;
    uint32_t output_index = 0;
    uint64_t amount = 0;
    std::string token_type = "NIGHT";
    std::string owner;
    int64_t ctime = 0;          // Unix timestamp
    bool is_spent = false;
};

/**
 * @brief Unshielded transaction event from WebSocket subscription
 */
struct UnshieldedTxEvent {
    uint64_t transaction_id = 0;  // For sync progress tracking (applied_id)
    std::vector<UtxoEvent> created;
    std::vector<UtxoEvent> spent;
};

/**
 * @brief Callback types for real-time events
 */
using UnshieldedTxCallback = std::function<void(const UnshieldedTxEvent&)>;
using ProgressCallback = std::function<void(uint64_t highest_transaction_id)>;
using BalanceCallback = std::function<void(const std::string& token_type, uint64_t total_balance)>;
using ConnectionCallback = std::function<void(bool connected, const std::string& message)>;
using ErrorCallback = std::function<void(const std::string& error)>;

/**
 * @brief GraphQL WebSocket Subscription Client
 *
 * Implements the "graphql-ws" protocol for real-time subscriptions
 * to Midnight Indexer events.
 *
 * Protocol:
 *   1. Connect to ws://.../graphql
 *   2. Send: {"type":"connection_init","payload":{}}
 *   3. Receive: {"type":"connection_ack"}
 *   4. Send: {"id":"1","type":"subscribe","payload":{"query":"subscription {...}"}}
 *   5. Receive: {"id":"1","type":"next","payload":{"data":{...}}}
 *   6. On disconnect: {"type":"complete","id":"1"}
 *
 * Supported subscriptions:
 *   - unshieldedTransactions(address: "Bech32m") → UTXO creation/spending
 *   - shieldedTransactions(sessionId: "...") → Shielded TX events
 *   - dustLedgerEvents(id: ...) → DUST generation events
 *   - blocks(offset: {hash: "0x..."}) → New blocks
 *
 * @code
 *   GraphQLSubscriptionClient client(
 *       "wss://indexer.preview.midnight.network/api/v4/graphql",
 *       "wss://indexer.preview.midnight.network/api/v4/graphql"  // HTTP base for HTTP fallback
 *   );
 *
 *   client.on_connection([](bool ok, const std::string& msg) {
 *       std::cout << "Connected: " << ok << std::endl;
 *   });
 *
 *   client.on_unshielded_tx([](const UnshieldedTxEvent& evt) {
 *       for (auto& u : evt.created) {
 *           std::cout << "UTXO created: " << u.amount << " " << u.token_type << std::endl;
 *       }
 *   });
 *
 *   client.on_balance_change([](const std::string& token, uint64_t bal) {
 *       std::cout << token << " balance: " << bal << std::endl;
 *   });
 *
 *   // Subscribe to UTXO changes for an address
 *   std::string addr = "mn_addr_preview...";
 *   client.subscribe_unshielded_transactions(addr);
 *
 *   client.run();  // blocking message loop
 *   // or use start_async() for non-blocking
 * @endcode
 */
class GraphQLSubscriptionClient {
public:
    /**
     * @brief Construct subscription client
     * @param ws_url WebSocket URL (wss://indexer.../api/v4/graphql)
     * @param http_base HTTP base URL for health checks (https://indexer.../api/v4)
     */
    GraphQLSubscriptionClient(const std::string& ws_url, const std::string& http_base = "");

    ~GraphQLSubscriptionClient();

    // Non-copyable
    GraphQLSubscriptionClient(const GraphQLSubscriptionClient&) = delete;
    GraphQLSubscriptionClient& operator=(const GraphQLSubscriptionClient&) = delete;
    GraphQLSubscriptionClient(GraphQLSubscriptionClient&&) noexcept;
    GraphQLSubscriptionClient& operator=(GraphQLSubscriptionClient&&) noexcept;

    // ─────────────────────────────────────────────
    // Connection management
    // ─────────────────────────────────────────────

    /**
     * @brief Connect and start message loop
     * @param timeout_ms Connection timeout
     * @return true if connected successfully
     */
    bool connect(int timeout_ms = 10000);

    /**
     * @brief Disconnect and stop message loop
     */
    void disconnect();

    /**
     * @brief Check if connected
     */
    bool is_connected() const { return connected_.load(); }

    /**
     * @brief Run blocking message loop (call from dedicated thread)
     *        Returns when disconnected.
     */
    void run();

    /**
     * @brief Start background thread for message loop
     */
    void start_async();

    /**
     * @brief Stop background thread
     */
    void stop_async();

    // ─────────────────────────────────────────────
    // Event callbacks
    // ─────────────────────────────────────────────

    /** @brief Called when connection state changes */
    void on_connection(ConnectionCallback cb);

    /** @brief Called on any error */
    void on_error(ErrorCallback cb);

    /** @brief Called on unshielded transaction events */
    void on_unshielded_tx(UnshieldedTxCallback cb);

    /** @brief Called when balance changes */
    void on_balance_change(BalanceCallback cb);

    /** @brief Called on UnshieldedTransactionsProgress events */
    void on_progress_change(ProgressCallback cb);

    // ─────────────────────────────────────────────
    // Subscription management
    // ─────────────────────────────────────────────

    /**
     * @brief Subscribe to unshielded transactions for an address
     *        Uses WebSocket subscription: unshieldedTransactions(address: "Bech32m")
     *
     * This is the PRIMARY method for UTXO tracking — both initial sync and real-time.
     * Matches the TypeScript SDK approach: WebSocket subscription handles EVERYTHING.
     * HTTP is NEVER used for UTXO data.
     *
     * Behavior:
     *   - transactionId = null → server streams ALL historical txs (for initial sync)
     *   - transactionId = N    → server streams from tx N+1 (for resume)
     *
     * Event sequence during initial sync:
     *   1. Multiple UnshieldedTransactionsProgress events → tracking catch-up progress
     *   2. First UnshieldedTransaction event → sync is now complete (historical done)
     *   3. Subsequent UnshieldedTransaction events → real-time updates
     *
     * @param address Bech32m encoded Midnight address (mn_addr_preview...)
     * @param from_transaction_id Resume from this tx ID (0 = sync from genesis)
     * @return subscription ID, empty on failure
     */
    std::string subscribe_unshielded_transactions(const std::string& address,
                                                 uint64_t from_transaction_id = 0);

    /**
     * @brief Unsubscribe from unshielded transactions
     */
    bool unsubscribe(const std::string& subscription_id);

    /**
     * @brief Get current known balance for a token type
     */
    uint64_t current_balance(const std::string& token_type = "NIGHT") const;

    /**
     * @brief Get current UTXO count
     */
    size_t current_utxo_count() const;

    /**
     * @brief Get last known state
     */
    WalletState last_state() const;

private:
    void message_loop_();
    bool send_ws_message(const std::string& msg);
    void handle_ws_message(const std::string& msg);
    void handle_connection_ack();
    void handle_subscription_data(const json& payload);
    void handle_subscription_error(const json& payload);
    void handle_subscription_complete(const std::string& id);
    void handle_ka(); // keep-alive / pong
    std::string next_subscription_id();

    // GraphQL-ws protocol helpers
    void send_connection_init();
    void send_subscribe(const std::string& id, const std::string& query, const json& variables = json::object());

    // UTXO parsing helpers
    void parse_unshielded_transaction_event(const json& data);
    uint64_t parse_amount(const json& val);

    std::string ws_url_;
    std::string http_base_;

    // cpp-httplib ws::WebSocketClient for real WebSocket connections (PImpl)
    struct WsImpl;
    std::unique_ptr<WsImpl> ws_impl_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};

    std::thread loop_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::string> message_queue_;

    uint32_t subscription_counter_ = 0;
    std::map<std::string, std::string> active_subscriptions_;  // id → query

    // Event callbacks
    ConnectionCallback conn_cb_;
    ErrorCallback error_cb_;
    UnshieldedTxCallback utxo_cb_;
    BalanceCallback balance_cb_;
    ProgressCallback progress_cb_;

    // Current state (updated by subscription events)
    std::atomic<uint64_t> night_balance_{0};
    std::atomic<uint64_t> dust_balance_{0};
    std::atomic<size_t> utxo_count_{0};

    mutable std::mutex state_mutex_;
    WalletState last_state_;

    // HTTP fallback (for initial state)
    std::string graphql_http_url_;
};

/**
 * @brief Real-time indexer subscription with WebSocket + HTTP fallback
 *
 * Combines WebSocket subscriptions for real-time updates with
 * HTTP GraphQL queries for initial state and robustness.
 *
 * This is the recommended client for wallet applications.
 *
 * @code
 *   RealtimeIndexerClient client(
 *       "wss://indexer.preview.midnight.network/api/v4/graphql",
 *       "https://indexer.preview.midnight.network/api/v4/graphql"
 *   );
 *
 *   // Start with initial state query
 *   client.init_state("mn_addr_preview...");
 *
 *   // Subscribe for real-time updates
 *   client.subscribe("mn_addr_preview...");
 *
 *   // Event handlers
 *   client.on_balance([](auto token, auto bal) { ... });
 *   client.on_utxo([](auto& evt) { ... });
 *
 *   // Run message loop
 *   client.run();
 * @endcode
 */
class RealtimeIndexerClient {
public:
    RealtimeIndexerClient(const std::string& ws_url, const std::string& graphql_url);

    ~RealtimeIndexerClient();

    /**
     * @brief Initialize state from HTTP query (call before subscribe)
     *        This is the HTTP fallback for initial state.
     */
    void init_state(const std::string& address);

    /**
     * @brief Subscribe for real-time UTXO updates
     * @param address Bech32m Midnight address
     * @return true on success
     */
    bool subscribe(const std::string& address);

    /**
     * @brief Subscribe with transaction ID for resumption
     *        Pass the last applied_id to avoid re-processing historical txs.
     * @param address Bech32m Midnight address
     * @param applied_id Resume from this transaction ID
     * @return true on success
     */
    bool subscribe_resume(const std::string& address, uint64_t applied_id);

    /**
     * @brief Unsubscribe
     */
    void unsubscribe();

    /**
     * @brief Run blocking loop
     */
    void run();

    /**
     * @brief Start async loop
     */
    void start();

    /**
     * @brief Stop async loop
     */
    void stop();

    /**
     * @brief Check if subscribed
     */
    bool is_subscribed() const { return subscribed_.load(); }

    /**
     * @brief Get last known state
     */
    WalletState last_state() const;

    /**
     * @brief Get current balance
     */
    uint64_t balance(const std::string& token = "NIGHT") const;

    /**
     * @brief Register balance change callback
     */
    void on_balance(BalanceCallback cb);

    /**
     * @brief Register UTXO event callback
     */
    void on_utxo(UnshieldedTxCallback cb);

    /**
     * @brief Register sync progress callback (UnshieldedTransactionsProgress events)
     */
    void on_progress(ProgressCallback cb);

    /**
     * @brief Register connection callback
     */
    void on_connection(ConnectionCallback cb);

private:
    void on_ws_balance(const std::string& token, uint64_t bal);
    void on_ws_utxo(const UnshieldedTxEvent& evt);

    GraphQLSubscriptionClient ws_client_;
    std::string address_;
    std::atomic<bool> subscribed_{false};
    mutable std::mutex state_mutex_;
    WalletState state_;

    BalanceCallback balance_cb_;
    UnshieldedTxCallback utxo_cb_;
    ConnectionCallback conn_cb_;
    ProgressCallback progress_cb_;

    std::thread loop_thread_;
    std::atomic<bool> running_{false};
};

} // namespace midnight::network
