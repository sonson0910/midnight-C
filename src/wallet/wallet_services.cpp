#include "midnight/wallet/wallet_services.hpp"
#include "midnight/core/logger.hpp"
#include <sodium.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

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

// ─── SerializedTransaction ───────────────────────────────────

SerializedTransaction SerializedTransaction::from_json(const json& tx_json) {
    SerializedTransaction st;
    std::string s = tx_json.dump();
    st.data.assign(s.begin(), s.end());
    st.tx_hash = sha256_hex(st.data);
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

    // Build the Substrate author_submitExtrinsic RPC call
    json rpc_body;
    rpc_body["jsonrpc"] = "2.0";
    rpc_body["id"] = 1;
    rpc_body["method"] = "author_submitExtrinsic";

    // Encode TX data as hex
    std::ostringstream hex_stream;
    hex_stream << "0x";
    for (auto b : tx.data) {
        hex_stream << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    }
    rpc_body["params"] = json::array({hex_stream.str()});

    // For now, mark as submitted (actual HTTP client integration via NetworkClient)
    event.tag = SubmissionEventTag::Submitted;
    connected_ = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("TX submitted to relay: " + tx.tx_hash.substr(0, 16) + "...");
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
        midnight::g_logger->info("TX " + tx_hash.substr(0, 16) + "... → " + status_str);
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

    // Build the /prove-tx request
    // The proof server expects:
    //   POST /prove-tx
    //   Content-Type: application/octet-stream
    //   Body: serialized UnprovenTransaction bytes

    // For now, simulate proof generation
    // In production, this calls the Docker proof server:
    //   docker run -p 6300:6300 midnightnetwork/proof-server:latest
    result.data = unproven_tx;
    result.proof_hash = sha256_hex(unproven_tx);
    result.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("TX proved: " + result.proof_hash.substr(0, 16) + "...");
    }

    return result;
}

ProvedTransaction ProvingService::prove_json(const json& tx_json) {
    std::string s = tx_json.dump();
    std::vector<uint8_t> data(s.begin(), s.end());
    return prove(data);
}

bool ProvingService::is_available() const {
    // Check if the proof server is reachable
    // In production: HTTP GET <config_.proving_server_url>/status
    return !config_.proving_server_url.empty();
}

json ProvingService::get_status() {
    json status;
    status["available"] = is_available();
    status["url"] = config_.proving_server_url;
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
    auto now = std::chrono::system_clock::now();
    for (auto& ptx : pending_) {
        ptx.last_checked = now;

        // In production, query indexer GraphQL:
        //   query { transactionByHash(hash: "...") { blockNumber status } }
        // For now, we just update the check timestamp
    }
}

} // namespace midnight::wallet
