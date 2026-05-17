#include "midnight/network/graphql_subscription.hpp"
#include "midnight/network/indexer_client.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include <chrono>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

// cpp-httplib WebSocket support
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#else
#include <httplib.h>
#endif

namespace midnight::network {

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────
// WsImpl: Real WebSocket implementation using httplib::ws::WebSocketClient
// ─────────────────────────────────────────────────────────────────

struct GraphQLSubscriptionClient::WsImpl {
    std::shared_ptr<httplib::ws::WebSocketClient> ws_client;
    std::string url;

    WsImpl(const std::string& wss_url, int timeout_ms)
        : url(wss_url)
    {
        // Midnight Indexer uses graphql-transport-ws subprotocol
        // This is the modern GraphQL over WebSocket protocol (not graphql-ws)
        // graphql-transport-ws is used by Apollo, urql, and most modern implementations
        httplib::Headers headers = {
            {"Sec-WebSocket-Protocol", "graphql-transport-ws"}
        };
        ws_client = std::make_shared<httplib::ws::WebSocketClient>(wss_url, headers);
        const int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : 30000;
        ws_client->set_connection_timeout(effective_timeout_ms / 1000,
                                          (effective_timeout_ms % 1000) * 1000);
        ws_client->set_read_timeout(60, 0);
        // Disable SSL certificate verification for preview network
        ws_client->enable_server_certificate_verification(false);

        if (!ws_client->is_valid()) {
            midnight::g_logger->error("WebSocketClient is_valid()=false - SSL may not be available for wss://");
        }
    }

    bool connect_ws() {
        return ws_client->connect();
    }

    bool send_text(const std::string& msg) {
        return ws_client->send(msg);
    }

    httplib::ws::ReadResult read_msg(std::string& out) {
        return ws_client->read(out);
    }

    bool is_open() const {
        return ws_client->is_open();
    }

    void close_ws() {
        ws_client->close();
    }
};

// ─────────────────────────────────────────────────────────────────
// GraphQLSubscriptionClient Implementation
// ─────────────────────────────────────────────────────────────────

GraphQLSubscriptionClient::GraphQLSubscriptionClient(const std::string& ws_url,
                                                     const std::string& http_base)
    : ws_impl_(nullptr), ws_url_(ws_url), http_base_(http_base)
{
    // Derive HTTP GraphQL URL from WS URL if not provided
    if (http_base_.empty()) {
        std::string path_part;
        if (ws_url_.find("wss://") == 0) {
            http_base_ = "https://" + ws_url_.substr(6);
        } else if (ws_url_.find("ws://") == 0) {
            http_base_ = "http://" + ws_url_.substr(5);
        }
        // Strip trailing /ws from HTTP base (WS URL ends with /ws, HTTP URL doesn't)
        const std::string ws_suffix = "/ws";
        if (http_base_.size() >= ws_suffix.size() &&
            http_base_.compare(http_base_.size() - ws_suffix.size(), ws_suffix.size(), ws_suffix) == 0) {
            http_base_ = http_base_.substr(0, http_base_.size() - ws_suffix.size());
        }
    }
    // Strip trailing /graphql from HTTP base if present (for posting to /graphql)
    if (!http_base_.empty() && http_base_.back() == '/') {
        http_base_ = http_base_.substr(0, http_base_.size() - 1);
    }
}

GraphQLSubscriptionClient::~GraphQLSubscriptionClient() {
    disconnect();
    stop_async();
}

GraphQLSubscriptionClient::GraphQLSubscriptionClient(GraphQLSubscriptionClient&& other) noexcept
    : ws_impl_(std::move(other.ws_impl_))
    , ws_url_(std::move(other.ws_url_))
    , http_base_(std::move(other.http_base_))
    , connected_(other.connected_.load())
    , running_(other.running_.load())
    , session_id_(std::move(other.session_id_))
    , subscription_counter_(other.subscription_counter_)
    , active_subscriptions_(std::move(other.active_subscriptions_))
    , conn_cb_(std::move(other.conn_cb_))
    , error_cb_(std::move(other.error_cb_))
    , utxo_cb_(std::move(other.utxo_cb_))
    , balance_cb_(std::move(other.balance_cb_))
    , progress_cb_(std::move(other.progress_cb_))
    , dust_generation_cb_(std::move(other.dust_generation_cb_))
    , last_state_(std::move(other.last_state_))
    , graphql_http_url_(std::move(other.graphql_http_url_))
    , night_balance_(other.night_balance_.load())
    , dust_balance_(other.dust_balance_.load())
    , utxo_count_(other.utxo_count_.load())
{
    other.connected_ = false;
    other.running_ = false;
}

GraphQLSubscriptionClient& GraphQLSubscriptionClient::operator=(GraphQLSubscriptionClient&& other) noexcept {
    if (this != &other) {
        disconnect();
        stop_async();
        ws_url_ = std::move(other.ws_url_);
        http_base_ = std::move(other.http_base_);
        ws_impl_ = std::move(other.ws_impl_);
        connected_ = other.connected_.load();
        running_ = other.running_.load();
        subscription_counter_ = other.subscription_counter_;
        active_subscriptions_ = std::move(other.active_subscriptions_);
        conn_cb_ = std::move(other.conn_cb_);
        error_cb_ = std::move(other.error_cb_);
        utxo_cb_ = std::move(other.utxo_cb_);
        balance_cb_ = std::move(other.balance_cb_);
        progress_cb_ = std::move(other.progress_cb_);
        dust_generation_cb_ = std::move(other.dust_generation_cb_);
        night_balance_ = other.night_balance_.load();
        dust_balance_ = other.dust_balance_.load();
        utxo_count_ = other.utxo_count_.load();
        last_state_ = std::move(other.last_state_);
        graphql_http_url_ = std::move(other.graphql_http_url_);
        other.connected_ = false;
        other.running_ = false;
    }
    return *this;
}

// ─── Connection ───────────────────────────────────────────────────

bool GraphQLSubscriptionClient::connect(int timeout_ms, const std::string& auth_token) {
    if (connected_.load())
        return true;

    midnight::g_logger->info("GraphQLSubscriptionClient connecting to: " + ws_url_);

    // Create real WebSocket client using httplib::ws::WebSocketClient
    try {
        ws_impl_ = std::make_unique<WsImpl>(ws_url_, timeout_ms);

        // Open WebSocket connection
        if (!ws_impl_->connect_ws()) {
            midnight::g_logger->error("WebSocket connection failed");
            ws_impl_.reset();
            if (conn_cb_) conn_cb_(false, "Connection failed");
            return false;
        }

        midnight::g_logger->info("WebSocket connected, sending connection_init");

        // Send graphql-transport-ws connection_init
        send_connection_init(auth_token);

        connected_ = true;
        midnight::g_logger->info("GraphQLSubscriptionClient connected to WebSocket");
        if (conn_cb_) conn_cb_(true, "WebSocket connected");

    } catch (const std::exception& e) {
        midnight::g_logger->error("WebSocket connection exception: " + std::string(e.what()));
        ws_impl_.reset();
        if (conn_cb_) conn_cb_(false, std::string("Connection failed: ") + e.what());
        return false;
    }

    return true;
}

bool GraphQLSubscriptionClient::connect(const std::string& auth_token, int timeout_ms) {
    return connect(timeout_ms, auth_token);
}

void GraphQLSubscriptionClient::disconnect() {
    if (!connected_.load())
        return;

    // Send complete messages for all active subscriptions
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& sub : active_subscriptions_) {
            json msg = {
                {"type", "complete"},
                {"id", sub.first}
            };
            if (ws_impl_) {
                ws_impl_->send_text(msg.dump());
            }
        }
        active_subscriptions_.clear();
    }

    // Send websocket close
    if (ws_impl_) {
        ws_impl_->close_ws();
    }

    ws_impl_.reset();
    connected_ = false;
    midnight::g_logger->info("GraphQLSubscriptionClient disconnected");

    if (conn_cb_) {
        try { conn_cb_(false, "Disconnected"); } catch (...) {}
    }
}

void GraphQLSubscriptionClient::run() {
    running_ = true;
    message_loop_();
}

void GraphQLSubscriptionClient::start_async() {
    if (running_.load())
        return;
    running_ = true;
    loop_thread_ = std::thread(&GraphQLSubscriptionClient::message_loop_, this);
}

void GraphQLSubscriptionClient::stop_async() {
    running_ = false;
    disconnect();
    cv_.notify_all();
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

// ─── Callbacks ──────────────────────────────────────────────────

void GraphQLSubscriptionClient::on_connection(ConnectionCallback cb) {
    conn_cb_ = std::move(cb);
}

void GraphQLSubscriptionClient::on_error(ErrorCallback cb) {
    error_cb_ = std::move(cb);
}

void GraphQLSubscriptionClient::on_unshielded_tx(UnshieldedTxCallback cb) {
    utxo_cb_ = std::move(cb);
}

void GraphQLSubscriptionClient::on_balance_change(BalanceCallback cb) {
    balance_cb_ = std::move(cb);
}

void GraphQLSubscriptionClient::on_progress_change(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
}

void GraphQLSubscriptionClient::on_dust_generation(DustGenerationCallback cb) {
    dust_generation_cb_ = std::move(cb);
}

// ─── Subscriptions ───────────────────────────────────────────────

std::string GraphQLSubscriptionClient::subscribe_unshielded_transactions(const std::string& address,
                                                                             uint64_t from_transaction_id) {
    std::string id = next_subscription_id();

    // GraphQL subscription query for unshieldedTransactions
    //
    // THE INDEXER DOES NOT HAVE A FAST HTTP BALANCE QUERY.
    // The TypeScript SDK uses ONLY this WebSocket subscription for UTXO data.
    // We mirror that exact approach: WebSocket for everything.
    //
    // Behavior:
    //   - transactionId omitted/null → server sends ALL historical + live events
    //   - transactionId = N      → server sends from tx N+1 onwards
    //
    // Event types emitted:
    //   - UnshieldedTransactionsProgress: { highestTransactionId } — catch-up progress
    //   - UnshieldedTransaction: { tx, createdUtxos[], spentUtxos[] } — actual UTXOs
    std::string query = R"(
        subscription UnshieldedTxSub($addr: UnshieldedAddress!, $transactionId: Int) {
            unshieldedTransactions(address: $addr, transactionId: $transactionId) {
                __typename
                ... on UnshieldedTransaction {
                    transaction { id hash block { height } }
                    createdUtxos {
                        owner
                        value
                        outputIndex
                        tokenType
                        ctime
                        initialNonce
                        registeredForDustGeneration
                        createdAtTransaction { id hash }
                    }
                    spentUtxos {
                        owner
                        value
                        tokenType
                        outputIndex
                        ctime
                        initialNonce
                        registeredForDustGeneration
                        createdAtTransaction { id hash }
                    }
                }
                ... on UnshieldedTransactionsProgress {
                    highestTransactionId
                }
            }
        }
    )";

    json variables = {{"addr", address}};
    if (from_transaction_id > 0) {
        variables["transactionId"] = from_transaction_id;
    }
    send_subscribe(id, query, variables);

    midnight::g_logger->info("Subscribed to unshieldedTransactions for: " +
                             address.substr(0, 20) + "..." +
                             (from_transaction_id > 0 ? " (resuming from tx " + std::to_string(from_transaction_id) + ")" : " (full sync)"));

    return id;
}

std::string GraphQLSubscriptionClient::subscribe_dust_generations(
    const std::string& dust_address,
    uint64_t start_index,
    uint64_t end_index) {
    std::string id = next_subscription_id();

    std::string query = R"(
        subscription DustGenerationsSub($dustAddress: DustAddress!, $startIndex: Int!, $endIndex: Int!) {
            dustGenerations(dustAddress: $dustAddress, startIndex: $startIndex, endIndex: $endIndex) {
                __typename
                ... on DustGenerationsItem {
                    commitmentMtIndex
                    generationMtIndex
                    owner
                    value
                    initialValue
                    backingNight
                    ctime
                    transactionId
                    collapsedMerkleTree {
                        startIndex
                        endIndex
                        update
                        protocolVersion
                    }
                }
                ... on DustGenerationsProgress {
                    highestIndex
                    collapsedMerkleTree {
                        startIndex
                        endIndex
                        update
                        protocolVersion
                    }
                }
                ... on DustGenerationDtimeUpdateItem {
                    id
                    raw
                    maxId
                    protocolVersion
                }
            }
        }
    )";

    json variables = {
        {"dustAddress", dust_address},
        {"startIndex", static_cast<int>(start_index)},
        {"endIndex", static_cast<int>(end_index)}
    };
    send_subscribe(id, query, variables);

    midnight::g_logger->info("Subscribed to dustGenerations for: " +
                             dust_address.substr(0, std::min<size_t>(20, dust_address.size())) +
                             "... [" + std::to_string(start_index) + ", " +
                             std::to_string(end_index) + "]");
    return id;
}

bool GraphQLSubscriptionClient::unsubscribe(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_subscriptions_.find(subscription_id);
    if (it == active_subscriptions_.end())
        return false;

    midnight::g_logger->info("Unsubscribing: " + subscription_id);
    active_subscriptions_.erase(it);
    return true;
}

uint64_t GraphQLSubscriptionClient::current_balance(const std::string& token_type) const {
    if (token_type == "DUST" || token_type == "dust")
        return dust_balance_.load();
    return night_balance_.load();
}

size_t GraphQLSubscriptionClient::current_utxo_count() const {
    return utxo_count_.load();
}

WalletState GraphQLSubscriptionClient::last_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_state_;
}

// ─── Message Loop (REAL WebSocket read) ───────────────────────────────────

void GraphQLSubscriptionClient::message_loop_() {
    midnight::g_logger->debug("GraphQLSubscriptionClient message loop started");

    while (running_.load() && connected_.load()) {
        // Read from real WebSocket connection
        if (ws_impl_ && ws_impl_->is_open()) {
            std::string msg;
            auto result = ws_impl_->read_msg(msg);

            if (result == httplib::ws::ReadResult::Text) {
                midnight::g_logger->debug("WS << " + msg.substr(0, std::min<size_t>(msg.size(), 200)));
                handle_ws_message(msg);
            } else if (result == httplib::ws::ReadResult::Binary) {
                midnight::g_logger->debug("WS << binary " + std::to_string(msg.size()) + " bytes");
            } else {
                midnight::g_logger->debug("WebSocket read failed, connection closed");
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Also process queued messages
        std::string queued_msg;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!message_queue_.empty()) {
                queued_msg = message_queue_.front();
                message_queue_.pop();
            }
        }
        if (!queued_msg.empty()) {
            handle_ws_message(queued_msg);
        }
    }

    connected_ = false;
    midnight::g_logger->debug("GraphQLSubscriptionClient message loop ended");
    if (conn_cb_) {
        try { conn_cb_(false, "Connection closed"); } catch (...) {}
    }
}

bool GraphQLSubscriptionClient::send_ws_message(const std::string& msg) {
    if (!ws_impl_) {
        midnight::g_logger->warn("Cannot send WS message: not connected");
        return false;
    }

    midnight::g_logger->debug("WS >> " + msg.substr(0, std::min<size_t>(msg.size(), 200)));
    return ws_impl_->send_text(msg);
}

void GraphQLSubscriptionClient::handle_ws_message(const std::string& msg) {
    try {
        json j = json::parse(msg);
        std::string type = j.value("type", "");

        midnight::g_logger->debug("WS << " + type);

        if (type == "connection_ack") {
            handle_connection_ack();
        }
        else if (type == "next") {
            if (j.contains("payload") && j["payload"].contains("data")) {
                handle_subscription_data(j["payload"]["data"]);
            }
        }
        else if (type == "error") {
            if (j.contains("payload")) {
                handle_subscription_error(j["payload"]);
            }
        }
        else if (type == "complete") {
            std::string id = j.value("id", "");
            handle_subscription_complete(id);
        }
        else if (type == "ka" || type == "pong") {
            handle_ka();
        }
        else if (type == "connection_error") {
            midnight::g_logger->error("Connection error from server");
            if (error_cb_) {
                try { error_cb_("Connection error"); } catch (...) {}
            }
        }
    } catch (const json::parse_error& e) {
        midnight::g_logger->warn("Failed to parse WS message: " + std::string(e.what()));
    }
}

void GraphQLSubscriptionClient::handle_connection_ack() {
    midnight::g_logger->info("Received connection_ack from GraphQL server");
}

void GraphQLSubscriptionClient::handle_subscription_data(const json& payload) {
    // Dispatch to the appropriate handler based on subscription type
    if (payload.contains("unshieldedTransactions")) {
        parse_unshielded_transaction_event(payload);
    }
    if (payload.contains("dustGenerations")) {
        parse_dust_generations_event(payload);
    }
}

void GraphQLSubscriptionClient::handle_subscription_error(const json& payload) {
    midnight::g_logger->error("Subscription error: " + payload.dump());
    if (error_cb_) {
        try { error_cb_(payload.dump()); } catch (...) {}
    }
}

void GraphQLSubscriptionClient::handle_subscription_complete(const std::string& id) {
    midnight::g_logger->info("Subscription complete: " + id);
    std::lock_guard<std::mutex> lock(mutex_);
    active_subscriptions_.erase(id);
}

void GraphQLSubscriptionClient::handle_ka() {
    // Keep-alive received, respond with ping if needed
    midnight::g_logger->debug("Received keep-alive");
}

std::string GraphQLSubscriptionClient::next_subscription_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    return "sub_" + std::to_string(++subscription_counter_);
}

void GraphQLSubscriptionClient::send_connection_init(const std::string& auth_token) {
    json payload = json::object();
    if (!auth_token.empty()) {
        // Send auth token in connection_init payload
        // graphql-transport-ws supports Bearer token format
        payload = json::object({
            {"Authorization", "Bearer " + auth_token}
        });
    }
    json msg = {
        {"type", "connection_init"},
        {"payload", payload}
    };
    send_ws_message(msg.dump());
}

void GraphQLSubscriptionClient::send_subscribe(const std::string& id,
                                               const std::string& query,
                                               const json& variables) {
    json payload = {{"query", query}};
    if (!variables.empty()) {
        payload["variables"] = variables;
    }

    json msg = {
        {"type", "subscribe"},
        {"id", id},
        {"payload", payload}
    };

    send_ws_message(msg.dump());

    std::lock_guard<std::mutex> lock(mutex_);
    active_subscriptions_[id] = query;
}

void GraphQLSubscriptionClient::parse_unshielded_transaction_event(const json& data) {
    // data contains: { unshieldedTransactions: { __typename, ...UnshieldedTransaction | ...UnshieldedTransactionsProgress } }
    if (!data.contains("unshieldedTransactions") || data["unshieldedTransactions"].is_null())
        return;

    auto wrapper = data["unshieldedTransactions"];
    std::string type_name = wrapper.value("__typename", "");

    // Handle progress updates (used for initial sync catch-up)
    if (type_name == "UnshieldedTransactionsProgress") {
        int64_t highest_id = wrapper.value("highestTransactionId", 0);
        midnight::g_logger->info("UTXO sync progress: highestTransactionId=" +
                                std::to_string(highest_id));
        if (progress_cb_) {
            try { progress_cb_(static_cast<uint64_t>(highest_id)); } catch (...) {}
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_state_.synced = true;
        }
        return;
    }

    // Handle actual transaction event
    if (type_name != "UnshieldedTransaction")
        return;

    // Extract transaction info
    auto tx_info = wrapper.value("transaction", json::object());
    std::string tx_hash = tx_info.value("hash", "");
    uint64_t tx_id = static_cast<uint64_t>(tx_info.value("id", 0));
    auto blk = tx_info.value("block", json::object());
    int64_t blk_height = blk.value("height", 0);

    midnight::g_logger->debug("UnshieldedTransaction: tx=" + tx_hash.substr(0, 16) +
                            "... blk=" + std::to_string(blk_height));

    // Parse CREATED UTXOs
    auto created_arr = wrapper.value("createdUtxos", json::array());
    for (const auto& out : created_arr) {
        UtxoEvent utxo;
        utxo.utxo_hash = tx_hash + ":" + std::to_string(out.value("outputIndex", 0));
        utxo.tx_hash = tx_hash;
        utxo.output_index = static_cast<uint32_t>(out.value("outputIndex", 0));
        utxo.amount = parse_amount(out.value("value", json::object()));
        utxo.token_type = normalize_token_type(out.value("tokenType", "NIGHT"));
        utxo.owner = out.value("owner", "");
        utxo.ctime = static_cast<int64_t>(parse_amount(out.value("ctime", json(0))));
        utxo.initial_nonce = out.value("initialNonce", "");
        utxo.registered_for_dust_generation = out.value("registeredForDustGeneration", false);

        midnight::g_logger->debug("  +UTXO: " + std::to_string(utxo.amount) +
                                " " + utxo.token_type);

        // Update running balance
        if (utxo.token_type == "NIGHT" || utxo.token_type.empty()) {
            night_balance_.store(night_balance_.load() + utxo.amount);
            if (balance_cb_) {
                try { balance_cb_("NIGHT", night_balance_.load()); } catch (...) {}
            }
        } else if (utxo.token_type == "DUST") {
            dust_balance_.store(dust_balance_.load() + utxo.amount);
            if (balance_cb_) {
                try { balance_cb_("DUST", dust_balance_.load()); } catch (...) {}
            }
        }
        utxo_count_.store(utxo_count_.load() + 1);
    }

    // Parse SPENT UTXOs
    auto spent_arr = wrapper.value("spentUtxos", json::array());
    for (const auto& out : spent_arr) {
        UtxoEvent utxo;
        const auto created_tx = out.value("createdAtTransaction", json::object());
        utxo.tx_hash = created_tx.value("hash", tx_hash);
        utxo.output_index = static_cast<uint32_t>(out.value("outputIndex", 0));
        utxo.amount = parse_amount(out.value("value", json::object()));
        utxo.token_type = normalize_token_type(out.value("tokenType", "NIGHT"));
        utxo.owner = out.value("owner", "");
        utxo.is_spent = true;
        utxo.ctime = static_cast<int64_t>(parse_amount(out.value("ctime", json(0))));
        utxo.initial_nonce = out.value("initialNonce", "");
        utxo.registered_for_dust_generation = out.value("registeredForDustGeneration", false);

        midnight::g_logger->debug("  -UTXO: " + std::to_string(utxo.amount) +
                                " " + utxo.token_type);

        // Update running balance (subtract spent)
        if (utxo.token_type == "NIGHT" || utxo.token_type.empty()) {
            night_balance_.store(night_balance_.load() > utxo.amount
                                 ? night_balance_.load() - utxo.amount : 0);
        } else if (utxo.token_type == "DUST") {
            dust_balance_.store(dust_balance_.load() > utxo.amount
                                ? dust_balance_.load() - utxo.amount : 0);
        }
        utxo_count_.store(utxo_count_.load() > 0 ? utxo_count_.load() - 1 : 0);
    }

    // Build event and fire callbacks
    if (!created_arr.empty() || !spent_arr.empty()) {
        UnshieldedTxEvent evt;
        evt.transaction_id = tx_id;

        // Re-populate created
        for (const auto& out : created_arr) {
            UtxoEvent u;
            u.utxo_hash = tx_hash + ":" + std::to_string(out.value("outputIndex", 0));
            u.tx_hash = tx_hash;
            u.output_index = static_cast<uint32_t>(out.value("outputIndex", 0));
            u.amount = parse_amount(out.value("value", json::object()));
            u.token_type = normalize_token_type(out.value("tokenType", "NIGHT"));
            u.owner = out.value("owner", "");
            u.ctime = static_cast<int64_t>(parse_amount(out.value("ctime", json(0))));
            u.initial_nonce = out.value("initialNonce", "");
            u.registered_for_dust_generation = out.value("registeredForDustGeneration", false);
            evt.created.push_back(u);
        }

        // Re-populate spent
        for (const auto& out : spent_arr) {
            UtxoEvent u;
            const auto created_tx = out.value("createdAtTransaction", json::object());
            u.tx_hash = created_tx.value("hash", tx_hash);
            u.output_index = static_cast<uint32_t>(out.value("outputIndex", 0));
            u.amount = parse_amount(out.value("value", json::object()));
            u.token_type = normalize_token_type(out.value("tokenType", "NIGHT"));
            u.owner = out.value("owner", "");
            u.is_spent = true;
            u.ctime = static_cast<int64_t>(parse_amount(out.value("ctime", json(0))));
            u.initial_nonce = out.value("initialNonce", "");
            u.registered_for_dust_generation = out.value("registeredForDustGeneration", false);
            evt.spent.push_back(u);
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_state_.available_utxo_count = static_cast<uint64_t>(utxo_count_.load());
            last_state_.unshielded_balance = std::to_string(night_balance_.load());
            last_state_.dust_balance = std::to_string(dust_balance_.load());
            last_state_.synced = true;
        }

        midnight::g_logger->info("UTXO event: +" + std::to_string(evt.created.size()) +
                                " -" + std::to_string(evt.spent.size()));

        if (utxo_cb_) {
            try { utxo_cb_(evt); } catch (const std::exception& e) {
                midnight::g_logger->error("UTXO callback error: " + std::string(e.what()));
            }
        }
    }
}

void GraphQLSubscriptionClient::parse_dust_generations_event(const json& data) {
    if (!data.contains("dustGenerations") || data["dustGenerations"].is_null())
        return;

    const auto& wrapper = data["dustGenerations"];
    DustGenerationEvent evt;
    evt.event_type = wrapper.value("__typename", "");

    if (evt.event_type == "DustGenerationsItem") {
        evt.commitment_mt_index = parse_amount(wrapper.value("commitmentMtIndex", json(0)));
        evt.generation_mt_index = parse_amount(wrapper.value("generationMtIndex", json(0)));
        evt.owner = wrapper.value("owner", "");
        evt.value = wrapper.value("value", "0");
        evt.initial_value = wrapper.value("initialValue", "0");
        evt.backing_night = wrapper.value("backingNight", "");
        evt.ctime = parse_amount(wrapper.value("ctime", json(0)));
        evt.transaction_id = parse_amount(wrapper.value("transactionId", json(0)));
    } else if (evt.event_type == "DustGenerationsProgress") {
        evt.highest_index = parse_amount(wrapper.value("highestIndex", json(0)));
    }

    if (wrapper.contains("collapsedMerkleTree") && !wrapper["collapsedMerkleTree"].is_null()) {
        evt.collapsed_merkle_tree = wrapper["collapsedMerkleTree"];
    }

    if (dust_generation_cb_) {
        try { dust_generation_cb_(evt); } catch (...) {}
    }
}

uint64_t GraphQLSubscriptionClient::parse_amount(const json& val) {
    if (val.is_number()) {
        return static_cast<uint64_t>(val.get<double>());
    }
    if (val.is_string()) {
        try {
            return std::stoull(val.get<std::string>());
        } catch (...) {}
    }
    if (val.is_object() && val.contains("value")) {
        return parse_amount(val["value"]);
    }
    return 0;
}

std::string GraphQLSubscriptionClient::normalize_token_type(const std::string& token_type) const {
    std::string raw = midnight::util::strip_hex_prefix(token_type);
    std::transform(raw.begin(), raw.end(), raw.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (raw.empty() || raw == std::string(64, '0')) {
        return "NIGHT";
    }
    return token_type;
}

// ─────────────────────────────────────────────────────────────────
// RealtimeIndexerClient Implementation
// ─────────────────────────────────────────────────────────────────

RealtimeIndexerClient::RealtimeIndexerClient(const std::string& ws_url,
                                             const std::string& graphql_url)
    : ws_client_(ws_url, graphql_url) {
}

RealtimeIndexerClient::~RealtimeIndexerClient() {
    stop();
}

void RealtimeIndexerClient::init_state(const std::string& address) {
    address_ = address;
    midnight::g_logger->info("RealtimeIndexerClient: will use WebSocket subscription for " +
                             address.substr(0, 20) + "...");

    // IMPORTANT: We intentionally do NOT use HTTP for initial state.
    // The JS SDK does the same — WebSocket subscription handles everything.
    // The WebSocket subscription streams ALL historical UTXOs on first connect
    // (when transactionId is omitted), then gives real-time updates.
    //
    // The sync progress (applied_id) is tracked inside the wallet facade
    // as we receive UnshieldedTransaction events.
}

bool RealtimeIndexerClient::subscribe_resume(const std::string& address, uint64_t applied_id) {
    address_ = address;

    ws_client_.on_unshielded_tx([this](const UnshieldedTxEvent& evt) {
        on_ws_utxo(evt);
    });

    ws_client_.on_balance_change([this](const std::string& token, uint64_t bal) {
        on_ws_balance(token, bal);
    });

    ws_client_.on_connection([this](bool ok, const std::string& msg) {
        if (conn_cb_) {
            try { conn_cb_(ok, msg); } catch (...) {}
        }
    });

    ws_client_.on_progress_change([this](uint64_t highest_id) {
        if (progress_cb_) {
            try { progress_cb_(highest_id); } catch (...) {}
        }
    });

    if (!ws_client_.connect()) {
        midnight::g_logger->error("Failed to connect to WebSocket");
        return false;
    }

    std::string sub_id = ws_client_.subscribe_unshielded_transactions(address, applied_id);
    if (sub_id.empty()) {
        midnight::g_logger->error("Failed to subscribe to unshieldedTransactions");
        return false;
    }

    subscribed_ = true;
    midnight::g_logger->info("RealtimeIndexerClient subscribed (resume from tx " +
                             std::to_string(applied_id) + ")");

    ws_client_.start_async();
    return true;
}

bool RealtimeIndexerClient::subscribe(const std::string& address) {
    address_ = address;

    ws_client_.on_unshielded_tx([this](const UnshieldedTxEvent& evt) {
        on_ws_utxo(evt);
    });

    ws_client_.on_balance_change([this](const std::string& token, uint64_t bal) {
        on_ws_balance(token, bal);
    });

    ws_client_.on_connection([this](bool ok, const std::string& msg) {
        if (conn_cb_) {
            try { conn_cb_(ok, msg); } catch (...) {}
        }
    });

    ws_client_.on_progress_change([this](uint64_t highest_id) {
        if (progress_cb_) {
            try { progress_cb_(highest_id); } catch (...) {}
        }
    });

    if (!ws_client_.connect()) {
        midnight::g_logger->error("Failed to connect to WebSocket");
        return false;
    }

    std::string sub_id = ws_client_.subscribe_unshielded_transactions(address, 0);
    if (sub_id.empty()) {
        midnight::g_logger->error("Failed to subscribe to unshieldedTransactions");
        return false;
    }

    subscribed_ = true;
    midnight::g_logger->info("RealtimeIndexerClient subscribed to " + address_);

    ws_client_.start_async();
    return true;
}

std::string RealtimeIndexerClient::subscribe_dust_generations(
    const std::string& dust_address,
    uint64_t start_index,
    uint64_t end_index) {
    ws_client_.on_dust_generation([this](const DustGenerationEvent& evt) {
        if (dust_generation_cb_) {
            try { dust_generation_cb_(evt); } catch (...) {}
        }
    });

    if (!ws_client_.is_connected() && !ws_client_.connect()) {
        midnight::g_logger->error("Failed to connect to WebSocket for dustGenerations");
        return "";
    }

    const auto id = ws_client_.subscribe_dust_generations(dust_address, start_index, end_index);
    if (!id.empty() && !running_.load()) {
        ws_client_.start_async();
    }
    return id;
}

void RealtimeIndexerClient::unsubscribe() {
    if (!subscribed_.load())
        return;

    subscribed_ = false;
    ws_client_.disconnect();
    midnight::g_logger->info("RealtimeIndexerClient unsubscribed");
}

void RealtimeIndexerClient::run() {
    ws_client_.run();
}

void RealtimeIndexerClient::start() {
    running_ = true;
    loop_thread_ = std::thread([this] { run(); });
}

void RealtimeIndexerClient::stop() {
    running_ = false;
    unsubscribe();
    ws_client_.stop_async();
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

WalletState RealtimeIndexerClient::last_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

uint64_t RealtimeIndexerClient::balance(const std::string& token) const {
    return ws_client_.current_balance(token);
}

void RealtimeIndexerClient::on_balance(BalanceCallback cb) {
    balance_cb_ = std::move(cb);
}

void RealtimeIndexerClient::on_utxo(UnshieldedTxCallback cb) {
    utxo_cb_ = std::move(cb);
}

void RealtimeIndexerClient::on_connection(ConnectionCallback cb) {
    conn_cb_ = std::move(cb);
}

void RealtimeIndexerClient::on_progress(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
}

void RealtimeIndexerClient::on_dust_generation(DustGenerationCallback cb) {
    dust_generation_cb_ = std::move(cb);
}

void RealtimeIndexerClient::on_ws_balance(const std::string& token, uint64_t bal) {
    midnight::g_logger->info("Balance update: " + token + " = " + std::to_string(bal));

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (token == "NIGHT" || token.empty()) {
            state_.unshielded_balance = std::to_string(bal);
        } else if (token == "DUST") {
            state_.dust_balance = std::to_string(bal);
        }
    }

    if (balance_cb_) {
        try { balance_cb_(token, bal); } catch (...) {}
    }
}

void RealtimeIndexerClient::on_ws_utxo(const UnshieldedTxEvent& evt) {
    midnight::g_logger->info("UTXO event: " + std::to_string(evt.created.size()) +
                               " created, " + std::to_string(evt.spent.size()) + " spent");

    // Update UTXO count in state
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        uint64_t registered = state_.dust_registered_utxo_count;
        for (const auto& u : evt.created) {
            if (u.registered_for_dust_generation) {
                registered++;
            }
        }
        for (const auto& u : evt.spent) {
            if (u.registered_for_dust_generation && registered > 0) {
                registered--;
            }
        }
        state_.address = address_;
        state_.available_utxo_count = static_cast<uint32_t>(
            ws_client_.current_utxo_count()
        );
        state_.dust_registered_utxo_count = registered;
        state_.synced = true;
    }

    if (utxo_cb_) {
        try { utxo_cb_(evt); } catch (...) {}
    }
}

} // namespace midnight::network
