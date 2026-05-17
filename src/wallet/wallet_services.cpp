#include "midnight/wallet/wallet_services.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/network/indexer_client.hpp"
#include <sodium.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace midnight::wallet {

// ─── Helpers ─────────────────────────────────────────────────

static std::string sha256_hex(const std::vector<uint8_t>& data) {
    uint8_t hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, data.data(), data.size());
    std::ostringstream oss;
    for (size_t i = 0; i < crypto_hash_sha256_BYTES; ++i)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    return oss.str();
}

static std::string to_hex_string(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << "0x";
    for (auto b : data)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

// ─── SerializedTransaction ────────────────────────────────────

SerializedTransaction SerializedTransaction::from_bytes(const std::vector<uint8_t>& data) {
    SerializedTransaction st;
    st.data = data;
    return st;
}

// ─── SubmissionService ───────────────────────────────────────

SubmissionService::SubmissionService(const SubmissionServiceConfig& config)
    : config_(config) {
    if (midnight::g_logger) {
        midnight::g_logger->info("SubmissionService created: " + config.relay_url);
    }
}

SubmissionService::~SubmissionService() {
    close();
}

SubmissionEvent SubmissionService::submit_transaction(
    const SerializedTransaction& tx,
    SubmissionEventTag wait_for) {

    if (tx.data.empty()) {
        return SubmissionEvent{SubmissionEventTag::Submitted, "", 0, "",
                               "Empty transaction data"};
    }

    // Step 1: Submit via HTTP POST to the relay
    auto result = submit_via_http(tx);
    if (result.is_error()) {
        return result;
    }

    // Step 2: If caller wants to wait for a higher status, poll
    if (wait_for != SubmissionEventTag::Submitted) {
        result = wait_for_status(tx.tx_hash, wait_for);
    }

    return result;
}

SubmissionEvent SubmissionService::submit_via_http(const SerializedTransaction& tx) {
    SubmissionEvent event;
    event.tx_hash = tx.tx_hash;

    if (config_.relay_url.empty()) {
        event.tag = SubmissionEventTag::Submitted;
        connected_ = false;
        event.error = "No relay URL configured";
        return event;
    }

    try {
        // Build the Substrate author_submitExtrinsic RPC call
        json rpc_body;
        rpc_body["jsonrpc"] = "2.0";
        rpc_body["id"] = 1;
        rpc_body["method"] = "author_submitExtrinsic";

        // Encode TX data as hex; tx.data must already contain complete
        // ledger/extrinsic bytes for the target node API.
        rpc_body["params"] = json::array({to_hex_string(tx.data)});

        // Create HTTP client for relay
        network::NetworkClient relay_client(config_.relay_url, config_.timeout_ms);
        relay_client.set_header("Content-Type", "application/json");

        // Post to Substrate RPC endpoint
        auto response = relay_client.post_json("/rpc", rpc_body);

        // Check for RPC errors
        if (response.contains("error")) {
            std::string err_msg = "Relay RPC error";
            if (response["error"].contains("message")) {
                err_msg = response["error"]["message"].get<std::string>();
            } else if (response["error"].contains("data")) {
                err_msg = response["error"]["data"].get<std::string>();
            } else {
                err_msg = response["error"].dump();
            }
            event.error = err_msg;
            connected_ = false;
            if (midnight::g_logger) {
                midnight::g_logger->warn("TX submission failed: " + err_msg);
            }
            return event;
        }

        if (response.contains("result") && response["result"].is_string()) {
            event.tx_hash = response["result"].get<std::string>();
        }

        event.tag = SubmissionEventTag::Submitted;
        connected_ = true;

        if (midnight::g_logger) {
            midnight::g_logger->info("TX submitted to relay: " + tx.tx_hash.substr(0, 16) + "...");
        }

    } catch (const std::exception& e) {
        event.error = std::string("Submission failed: ") + e.what();
        connected_ = false;
        if (midnight::g_logger) {
            midnight::g_logger->error("TX submission error: " + event.error);
        }
    }

    return event;
}

SubmissionEvent SubmissionService::wait_for_status(const std::string& tx_hash,
                                                     SubmissionEventTag target) {
    SubmissionEvent event;
    event.tx_hash = tx_hash;

    // Poll the indexer/node for status updates
    // In production, this would query the node RPC for:
    //   - author_pendingExtrinsics (Submitted)
    //   - chain_getBlock (InBlock)
    //   - chain_getFinalizedHead (Finalized)

    // For now, return the target status directly
    event.tag = target;

    if (midnight::g_logger) {
        std::string status_str;
        switch (target) {
            case SubmissionEventTag::Submitted: status_str = "Submitted"; break;
            case SubmissionEventTag::InBlock: status_str = "InBlock"; break;
            case SubmissionEventTag::Finalized: status_str = "Finalized"; break;
        }
        midnight::g_logger->info("TX " + tx_hash.substr(0, 16) + "... -> " + status_str);
    }

    return event;
}

void SubmissionService::close() {
    connected_ = false;
}

// ─── ProvingService ──────────────────────────────────────────

ProvingService::ProvingService(const ProvingServiceConfig& config)
    : config_(config) {
    if (midnight::g_logger) {
        midnight::g_logger->info("ProvingService created: " + config.proving_server_url);
    }
}

ProvedTransaction ProvingService::prove(const std::vector<uint8_t>& unproven_tx) {
    ProvedTransaction result;

    if (unproven_tx.empty()) {
        result.error = "Empty transaction data";
        return result;
    }

    if (config_.proving_server_url.empty()) {
        result.error = "No proving server URL configured";
        return result;
    }

    try {
        // Create HTTP client for proof server
        network::NetworkClient proof_client(config_.proving_server_url, config_.timeout_ms);

        // Official proof-server contract:
        //   POST /prove
        //   Content-Type: application/octet-stream
        //   Body: ledger createProvingPayload(...) bytes
        //   Response: serialized proof result bytes
        result.data = proof_client.post_bytes("/prove", unproven_tx);
        result.proof_hash = sha256_hex(result.data);
        result.success = true;

        if (midnight::g_logger) {
            midnight::g_logger->info("TX proved: " + result.proof_hash.substr(0, 16) + "...");
        }

    } catch (const std::exception& e) {
        result.error = std::string("Proof server error: ") + e.what();
        result.success = false;

        if (midnight::g_logger) {
            midnight::g_logger->warn("ProvingService failed: " + result.error);
        }
    }

    return result;
}

bool ProvingService::is_available() const {
    if (config_.proving_server_url.empty()) {
        return false;
    }

    try {
        network::NetworkClient proof_client(config_.proving_server_url, config_.timeout_ms);
        auto response = proof_client.get_json("/health");
        return response.value("status", "") == "ok" || response.contains("version");
    } catch (...) {
        return false;
    }
}

json ProvingService::get_status() {
    json status;
    status["url"] = config_.proving_server_url;
    status["available"] = false;

    if (config_.proving_server_url.empty()) {
        status["error"] = "No proof server URL configured";
        return status;
    }

    try {
        network::NetworkClient proof_client(config_.proving_server_url, config_.timeout_ms);
        auto health = proof_client.get_json("/health");
        status["available"] = health.value("status", "") == "ok" || health.contains("version");
        status["health"] = health;
    } catch (const std::exception& e) {
        status["error"] = e.what();
    }

    return status;
}

// ─── PendingTransactionsService ──────────────────────────────

PendingTransactionsService::PendingTransactionsService(const PendingTransactionsConfig& config)
    : config_(config) {
    if (midnight::g_logger) {
        midnight::g_logger->info("PendingTransactionsService created");
    }
}

PendingTransactionsService::~PendingTransactionsService() {
    stop();
}

void PendingTransactionsService::start() {
    running_ = true;
    if (midnight::g_logger) {
        midnight::g_logger->info("PendingTransactionsService started (poll every " +
                                  std::to_string(config_.poll_interval_ms) + "ms)");
    }
}

void PendingTransactionsService::stop() {
    running_ = false;
    if (midnight::g_logger) {
        midnight::g_logger->info("PendingTransactionsService stopped");
    }
}

void PendingTransactionsService::add_pending_transaction(const std::string& tx_hash) {
    PendingTx ptx;
    ptx.tx_hash = tx_hash;
    ptx.status = SubmissionEventTag::Submitted;
    ptx.submitted_at = std::chrono::system_clock::now();
    ptx.last_checked = ptx.submitted_at;
    pending_.push_back(ptx);

    if (midnight::g_logger) {
        midnight::g_logger->info("Tracking pending TX: " + tx_hash.substr(0, 16) + "...");
    }
}

void PendingTransactionsService::clear(const std::string& tx_hash) {
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
                       [&](const PendingTx& p) { return p.tx_hash == tx_hash; }),
        pending_.end());
}

std::vector<PendingTx> PendingTransactionsService::get_pending() const {
    return pending_;
}

std::optional<PendingTx> PendingTransactionsService::get_state(const std::string& tx_hash) const {
    for (const auto& p : pending_) {
        if (p.tx_hash == tx_hash) return p;
    }
    return std::nullopt;
}

void PendingTransactionsService::on_status_change(StatusCallback callback) {
    callbacks_.push_back(std::move(callback));
}

void PendingTransactionsService::poll_once() {
    if (pending_.empty()) return;

    if (config_.indexer_http_url.empty()) {
        if (midnight::g_logger) {
            midnight::g_logger->warn("PendingTransactionsService: no indexer URL configured");
        }
        return;
    }

    auto now = std::chrono::system_clock::now();

    try {
        network::IndexerClient indexer(config_.indexer_http_url);

        for (auto& ptx : pending_) {
            ptx.last_checked = now;

            // Query indexer for TX status
            // Indexer v4: Use transactions(offset: { hash: $hash }) query
            // Note: 'identifiers' is an array, so we pass as [HexEncoded!]!
            const std::string query = R"(
                query TransactionByHash($hash: HexEncoded!) {
                    transactions(offset: { hash: $hash }) {
                        id
                        hash
                        block {
                            height
                            hash
                        }
                        transactionResult {
                            status
                        }
                        fees {
                            paidFees
                            estimatedFees
                        }
                    }
                }
            )";

            json variables = {{"hash", midnight::util::ensure_hex_prefix(ptx.tx_hash)}};
            auto data = indexer.graphql_query(query, variables);

            if (data.contains("transactions") && data["transactions"].is_array() &&
                !data["transactions"].empty()) {
                auto& tx_data = data["transactions"][0];

                // Update block height if available
                if (tx_data.contains("block") && !tx_data["block"].is_null()) {
                    if (tx_data["block"].contains("height") && !tx_data["block"]["height"].is_null()) {
                        try {
                            ptx.block_number = tx_data["block"]["height"].get<uint64_t>();
                        } catch (...) {}
                    }
                }

                // Parse status from transactionResult
                if (tx_data.contains("transactionResult") &&
                    !tx_data["transactionResult"].is_null() &&
                    tx_data["transactionResult"].contains("status")) {
                    std::string status_str;
                    try {
                        status_str = tx_data["transactionResult"]["status"].get<std::string>();
                    } catch (...) {}

                    SubmissionEventTag new_status = SubmissionEventTag::Submitted;
                    bool changed = false;

                    // Map indexer status to SubmissionEventTag
                    if (status_str == "Finalized" || status_str == "finalized") {
                        if (ptx.status != SubmissionEventTag::Finalized) {
                            new_status = SubmissionEventTag::Finalized;
                            ptx.status = new_status;
                            changed = true;
                        }
                    } else if (status_str == "InBlock" || status_str == "inblock" ||
                               status_str == "in_block" || status_str == "confirmed") {
                        if (ptx.status == SubmissionEventTag::Submitted) {
                            new_status = SubmissionEventTag::InBlock;
                            ptx.status = new_status;
                            changed = true;
                        }
                    }

                    // Notify callbacks on status change
                    if (changed) {
                        for (auto& callback : callbacks_) {
                            callback(ptx);
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        if (midnight::g_logger) {
            midnight::g_logger->warn("PendingTransactionsService poll failed: " + std::string(e.what()));
        }
    }
}

} // namespace midnight::wallet
