#include "midnight/network/indexer_subscription.hpp"
#include "midnight/core/logger.hpp"
#include <chrono>

namespace midnight::network {

IndexerSubscription::IndexerSubscription(const std::string& graphql_url,
                                           const std::string& address)
    : graphql_url_(graphql_url), address_(address)
{
}

IndexerSubscription::~IndexerSubscription() {
    stop();
}

IndexerSubscription::IndexerSubscription(IndexerSubscription&& other) noexcept
    : graphql_url_(std::move(other.graphql_url_))
    , address_(std::move(other.address_))
    , tx_callbacks_(std::move(other.tx_callbacks_))
    , balance_callbacks_(std::move(other.balance_callbacks_))
    , running_(other.running_.load())
    , last_state_(std::move(other.last_state_))
    , last_known_balance_(other.last_known_balance_)
    , last_utxo_count_(other.last_utxo_count_)
{
    other.running_ = false;
}

IndexerSubscription& IndexerSubscription::operator=(IndexerSubscription&& other) noexcept {
    if (this != &other) {
        stop();
        graphql_url_ = std::move(other.graphql_url_);
        address_ = std::move(other.address_);
        tx_callbacks_ = std::move(other.tx_callbacks_);
        balance_callbacks_ = std::move(other.balance_callbacks_);
        running_ = other.running_.load();
        last_state_ = std::move(other.last_state_);
        last_known_balance_ = other.last_known_balance_;
        last_utxo_count_ = other.last_utxo_count_;
        other.running_ = false;
    }
    return *this;
}

void IndexerSubscription::on_unshielded_tx(UnshieldedTxCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    tx_callbacks_.push_back(std::move(callback));
}

void IndexerSubscription::on_balance_change(BalanceCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    balance_callbacks_.push_back(std::move(callback));
}

void IndexerSubscription::start(uint32_t interval_ms) {
    if (running_.load())
        return;

    running_ = true;
    poll_thread_ = std::thread(&IndexerSubscription::poll_loop, this, interval_ms);
    if (midnight::g_logger) {
        midnight::g_logger->info("IndexerSubscription started for " +
                                  address_.substr(0, 30) + "... (poll every " +
                                  std::to_string(interval_ms) + "ms)");
    }
}

void IndexerSubscription::stop() {
    running_ = false;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    if (midnight::g_logger) {
        midnight::g_logger->info("IndexerSubscription stopped");
    }
}

bool IndexerSubscription::is_running() const {
    return running_.load();
}

WalletState IndexerSubscription::last_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_state_;
}

void IndexerSubscription::poll_loop(uint32_t interval_ms) {
    while (running_.load()) {
        try {
            IndexerClient indexer(graphql_url_);
            auto ws = indexer.query_wallet_state(address_);

            uint64_t new_balance = 0;
            try {
                new_balance = std::stoull(ws.unshielded_balance);
            } catch (...) {
                new_balance = 0;
            }

            bool balance_changed = (new_balance != last_known_balance_);
            bool utxo_changed = (ws.available_utxo_count != last_utxo_count_);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                last_state_ = ws;
            }

            // Notify balance change
            if (balance_changed) {
                if (midnight::g_logger) {
                    midnight::g_logger->info("Balance changed: " +
                                              std::to_string(last_known_balance_) +
                                              " → " + std::to_string(new_balance) + " NIGHT");
                }

                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& cb : balance_callbacks_) {
                    try {
                        cb("NIGHT", new_balance);
                    } catch (const std::exception& e) {
                        if (midnight::g_logger)
                            midnight::g_logger->error("Balance callback error: " + std::string(e.what()));
                    }
                }
                last_known_balance_ = new_balance;
            }

            // Notify UTXO changes (new transactions)
            if (utxo_changed) {
                auto utxos = indexer.query_utxos(address_);

                std::vector<wallet::UtxoWithMeta> created;
                for (const auto& utxo : utxos) {
                    wallet::UtxoWithMeta meta;
                    meta.utxo_hash = utxo.tx_hash + ":" + std::to_string(utxo.output_index);
                    meta.output_index = utxo.output_index;
                    meta.amount = utxo.amount;
                    meta.token_type = "NIGHT";
                    meta.tx_hash = utxo.tx_hash;
                    meta.ctime = std::chrono::system_clock::now();
                    created.push_back(meta);
                }

                std::vector<wallet::UtxoWithMeta> spent; // TODO: diff with previous state

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (auto& cb : tx_callbacks_) {
                        try {
                            cb(created, spent);
                        } catch (const std::exception& e) {
                            if (midnight::g_logger)
                                midnight::g_logger->error("TX callback error: " + std::string(e.what()));
                        }
                    }
                }

                last_utxo_count_ = ws.available_utxo_count;
            }

        } catch (const std::exception& e) {
            if (midnight::g_logger)
                midnight::g_logger->error("IndexerSubscription poll error: " + std::string(e.what()));
        }

        // Sleep in small increments to allow clean shutdown
        auto end_time = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(interval_ms);
        while (running_.load() && std::chrono::steady_clock::now() < end_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace midnight::network
