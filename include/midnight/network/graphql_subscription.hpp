#pragma once

/**********************************************************
 * Midnight Network API - GraphQL Subscriptions
 *
 * Header chứa đầy đủ các class và struct cho:
 * - GraphQLSubscriptions: Low-level subscription client
 * - GraphQLSubscriptionClient: Core WebSocket implementation
 * - RealtimeIndexerClient: High-level UTXO real-time client
 *
 * WebSocket Subprotocol:
 *   Midnight Indexer uses graphql-transport-ws protocol (not graphql-ws)
 *   - connection_init: Send auth token if required
 *   - subscribe: Subscribe to GraphQL operations
 *   - next: Receive subscription data
 *   - complete: Subscription completed
 *   - ka: Keep-alive ping/pong
 *
 * Endpoint: wss://<host>:<port>/api/v4/graphql/ws
 **********************************************************/

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

#include "midnight/network/indexer_client.hpp"

namespace midnight::network
{

    using json = nlohmann::json;

    // ═══════════════════════════════════════════════════════════
    // UTXO Event Types
    // ═══════════════════════════════════════════════════════════

    struct UtxoEvent {
        std::string utxo_hash;
        std::string tx_hash;
        uint32_t output_index = 0;
        uint64_t amount = 0;
        std::string token_type = "NIGHT";
        std::string owner;
        bool is_spent = false;
        int64_t ctime = 0;
    };

    struct UnshieldedTxEvent {
        uint64_t transaction_id = 0;
        std::vector<UtxoEvent> created;
        std::vector<UtxoEvent> spent;
    };

    // ═══════════════════════════════════════════════════════════
    // WebSocket Message Types
    // ═══════════════════════════════════════════════════════════

    enum class GqlWsMessageType
    {
        CONNECTION_INIT,
        CONNECTION_ACK,
        CONNECTION_ERROR,
        CONNECTION_TERMINATE,
        SUBSCRIBE,
        NEXT,
        ERROR,
        COMPLETE
    };

    struct GqlWsMessage
    {
        GqlWsMessageType type;
        json payload;
        std::optional<std::string> id;

        static GqlWsMessage parse(const std::string &raw);
        std::string serialize() const;
    };

    // ═══════════════════════════════════════════════════════════
    // Block and Transaction Updates
    // ═══════════════════════════════════════════════════════════

    struct BlockUpdate
    {
        std::string hash;
        uint64_t height = 0;
        std::string timestamp;
        std::string author;
        std::string protocol_version;
        std::string parent_hash;
        uint32_t tx_count = 0;
    };

    struct UnshieldedOutput
    {
        std::string owner;
        std::string value;
        std::string token_type;
        std::string intent_hash;
        uint32_t output_index = 0;
        uint64_t block_height = 0;
    };

    struct TransactionUpdate
    {
        std::string id;
        std::string hash;
        std::string status;
        std::vector<std::string> segment_ids;
        std::vector<bool> segment_success;
        std::vector<UnshieldedOutput> created_outputs;
        std::vector<UnshieldedOutput> spent_outputs;
    };

    struct ContractActionUpdate
    {
        std::string address;
        std::string action_type;
        std::string entry_point;
        json state;
        json zswap_state;
        std::vector<json> unshielded_balances;
    };

    struct DustLedgerEvent
    {
        std::string id;
        std::string cardano_reward_address;
        std::string midnight_address;
        std::string event_type;
        std::string amount;
        uint64_t block_height = 0;
    };

    struct ZswapLedgerEvent
    {
        std::string id;
        std::string shielded_address;
        std::string event_type;
        std::string amount;
        std::string token_type;
        uint64_t block_height = 0;
    };

    // ═══════════════════════════════════════════════════════════
    // Callback Types
    // ═══════════════════════════════════════════════════════════

    using ConnectionCallback = std::function<void(bool ok, const std::string& msg)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    using UnshieldedTxCallback = std::function<void(const UnshieldedTxEvent& evt)>;
    using BalanceCallback = std::function<void(const std::string& token, uint64_t balance)>;
    using ProgressCallback = std::function<void(uint64_t highest_transaction_id)>;

    // ═══════════════════════════════════════════════════════════
    // GraphQLSubscriptions (legacy interface)
    // ═══════════════════════════════════════════════════════════

    class GraphQLSubscriptions
    {
    public:
        explicit GraphQLSubscriptions(const std::string &ws_url,
                                    const std::string &graphql_http_url = "");
        ~GraphQLSubscriptions();

        GraphQLSubscriptions(const GraphQLSubscriptions &) = delete;
        GraphQLSubscriptions &operator=(const GraphQLSubscriptions &) = delete;
        GraphQLSubscriptions(GraphQLSubscriptions &&) = delete;
        GraphQLSubscriptions &operator=(GraphQLSubscriptions &&) = delete;

        bool connect(const std::string &viewing_key = "");
        void disconnect();
        bool is_connected() const { return connected_.load(); }

        std::string subscribe_blocks();
        std::string subscribe_contract_actions(const std::string &address);
        std::string subscribe_unshielded_transactions(const std::string &address);
        std::string subscribe_shielded_transactions(const std::string &session_id);
        std::string subscribe_dust_events(const std::string &cardano_address);
        std::string subscribe_zswap_events(const std::string &shielded_address);
        void unsubscribe(const std::string &subscription_id);

        void on_block(std::function<void(const BlockUpdate &)> callback);
        void on_unshielded_transaction(std::function<void(const TransactionUpdate &)> callback);
        void on_shielded_transaction(std::function<void(const TransactionUpdate &)> callback);
        void on_contract_action(std::function<void(const ContractActionUpdate &)> callback);
        void on_dust_event(std::function<void(const DustLedgerEvent &)> callback);
        void on_zswap_event(std::function<void(const ZswapLedgerEvent &)> callback);
        void on_error(std::function<void(const std::string &)> callback);

        void start();
        void stop();

    private:
        std::string ws_url_;
        std::string http_url_;
        std::atomic<bool> connected_{false};
        std::atomic<bool> running_{false};
        std::string session_id_;
        uint32_t subscription_counter_ = 0;

        std::mutex callback_mutex_;
        std::function<void(const BlockUpdate &)> on_block_callback_;
        std::function<void(const TransactionUpdate &)> on_unshielded_tx_callback_;
        std::function<void(const TransactionUpdate &)> on_shielded_tx_callback_;
        std::function<void(const ContractActionUpdate &)> on_contract_action_callback_;
        std::function<void(const DustLedgerEvent &)> on_dust_event_callback_;
        std::function<void(const ZswapLedgerEvent &)> on_zswap_event_callback_;
        std::function<void(const std::string &)> on_error_callback_;

        std::mutex socket_mutex_;
        std::condition_variable socket_cv_;
        std::queue<std::string> message_queue_;
        std::thread reader_thread_;
        std::thread writer_thread_;

        std::string next_id();
        void reader_loop();
        void writer_loop();
        void process_message(const std::string &raw);
        void handle_message(const GqlWsMessage &msg);
        bool send_message(const GqlWsMessage &msg);
        bool ws_send(const std::string &data);
        std::optional<std::string> ws_receive();

        void *ws_handle_ = nullptr;
        bool ws_connect();
        void ws_disconnect();
    };

    // ═══════════════════════════════════════════════════════════
    // GraphQLSubscriptionClient (Core Implementation)
    // ═══════════════════════════════════════════════════════════

    class GraphQLSubscriptionClient
    {
    public:
        explicit GraphQLSubscriptionClient(const std::string& ws_url,
                                            const std::string& http_base = "");
        ~GraphQLSubscriptionClient();

        GraphQLSubscriptionClient(GraphQLSubscriptionClient&& other) noexcept;
        GraphQLSubscriptionClient& operator=(GraphQLSubscriptionClient&& other) noexcept;

        /**
         * @brief Connect with optional authentication token
         *
         * Midnight Indexer may require authentication via token in connection_init.
         * If auth_token is provided, it will be sent in the connection_init payload.
         *
         * @param timeout_ms Connection timeout in milliseconds
         * @param auth_token Optional auth token for connection_init payload
         * @return true if connected successfully
         */
        bool connect(int timeout_ms = 30000, const std::string& auth_token = "");

        /**
         * @brief Connect with authentication token (convenience overload)
         *
         * @param auth_token Auth token to send in connection_init payload
         * @param timeout_ms Connection timeout in milliseconds
         * @return true if connected successfully
         */
        bool connect(const std::string& auth_token, int timeout_ms = 30000);

        void disconnect();
        bool is_connected() const { return connected_.load(); }

        void run();
        void start_async();
        void stop_async();

        // Subscription methods
        std::string subscribe_unshielded_transactions(const std::string& address,
                                                      uint64_t from_transaction_id = 0);
        bool unsubscribe(const std::string& subscription_id);

        // State queries
        uint64_t current_balance(const std::string& token_type) const;
        size_t current_utxo_count() const;
        WalletState last_state() const;

        // Callback setters
        void on_connection(ConnectionCallback cb);
        void on_error(ErrorCallback cb);
        void on_unshielded_tx(UnshieldedTxCallback cb);
        void on_balance_change(BalanceCallback cb);
        void on_progress_change(ProgressCallback cb);

    private:
        // WebSocket implementation (pimpl)
        struct WsImpl;
        std::unique_ptr<WsImpl> ws_impl_;

        std::string ws_url_;
        std::string http_base_;

        std::atomic<bool> connected_{false};
        std::atomic<bool> running_{false};
        std::string session_id_;

        uint32_t subscription_counter_ = 0;
        std::mutex mutex_;
        std::unordered_map<std::string, std::string> active_subscriptions_;
        std::queue<std::string> message_queue_;

        std::thread loop_thread_;
        std::condition_variable cv_;

        // Callbacks
        ConnectionCallback conn_cb_;
        ErrorCallback error_cb_;
        UnshieldedTxCallback utxo_cb_;
        BalanceCallback balance_cb_;
        ProgressCallback progress_cb_;

        // State tracking
        mutable std::mutex state_mutex_;
        WalletState last_state_;  // in midnight::network namespace

        std::string graphql_http_url_;

        // Balance tracking
        std::atomic<uint64_t> night_balance_{0};
        std::atomic<uint64_t> dust_balance_{0};
        std::atomic<size_t> utxo_count_{0};

        // Internal methods
        void message_loop_();
        bool send_ws_message(const std::string& msg);
        void handle_ws_message(const std::string& msg);
        void handle_connection_ack();
        void handle_subscription_data(const json& payload);
        void handle_subscription_error(const json& payload);
        void handle_subscription_complete(const std::string& id);
        void handle_ka();
        std::string next_subscription_id();
        void send_connection_init(const std::string& auth_token = "");
        void send_subscribe(const std::string& id, const std::string& query, const json& variables);
        void parse_unshielded_transaction_event(const json& data);
        uint64_t parse_amount(const json& val);
    };

    // ═══════════════════════════════════════════════════════════
    // RealtimeIndexerClient (High-Level UTXO Client)
    // ═══════════════════════════════════════════════════════════

    class RealtimeIndexerClient
    {
    public:
        RealtimeIndexerClient(const std::string& ws_url,
                             const std::string& graphql_url);
        ~RealtimeIndexerClient();

        void init_state(const std::string& address);
        bool subscribe(const std::string& address);
        bool subscribe_resume(const std::string& address, uint64_t applied_id);
        void unsubscribe();

        void start();
        void stop();
        void run();

        WalletState last_state() const;
        uint64_t balance(const std::string& token) const;

        void on_balance(BalanceCallback cb);
        void on_utxo(UnshieldedTxCallback cb);
        void on_connection(ConnectionCallback cb);
        void on_progress(ProgressCallback cb);

    private:
        GraphQLSubscriptionClient ws_client_;
        std::string address_;
        std::atomic<bool> subscribed_{false};
        std::atomic<bool> running_{false};
        std::thread loop_thread_;

        mutable std::mutex state_mutex_;
        WalletState state_;  // in midnight::network namespace

        BalanceCallback balance_cb_;
        UnshieldedTxCallback utxo_cb_;
        ConnectionCallback conn_cb_;
        ProgressCallback progress_cb_;

        void on_ws_balance(const std::string& token, uint64_t bal);
        void on_ws_utxo(const UnshieldedTxEvent& evt);
    };

} // namespace midnight::network
