#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <optional>

namespace midnight::wallet {

using json = nlohmann::json;

// ─── Submission Events (matching SDK SubmissionEvent) ────────

enum class SubmissionEventTag {
    Submitted,     ///< TX accepted by relay
    InBlock,       ///< TX included in a block
    Finalized      ///< TX finalized by GRANDPA
};

struct SubmissionEvent {
    SubmissionEventTag tag = SubmissionEventTag::Submitted;
    std::string tx_hash;
    uint64_t block_number = 0;
    std::string block_hash;
    std::string error;

    bool is_error() const { return !error.empty(); }
};

// ─── Serialized Transaction ─────────────────────────────────

struct SerializedTransaction {
    std::vector<uint8_t> data;           ///< Serialized TX bytes
    std::string tx_hash;                 ///< SHA-256 hash of serialized data

    /// Serialize from JSON TX representation
    static SerializedTransaction from_json(const json& tx_json);
};

// ─── Submission Service ─────────────────────────────────────
// Matching @midnight-ntwrk/wallet-sdk-capabilities/submission

struct SubmissionServiceConfig {
    std::string relay_url;               ///< Midnight relay node URL (ws:// or wss://)
    uint32_t timeout_ms = 30000;
};

class SubmissionService {
public:
    explicit SubmissionService(const SubmissionServiceConfig& config);
    ~SubmissionService();

    /// Submit a serialized transaction to the network
    /// @param tx Serialized transaction
    /// @param wait_for Wait for this status before returning
    /// @return SubmissionEvent with result
    SubmissionEvent submit_transaction(
        const SerializedTransaction& tx,
        SubmissionEventTag wait_for = SubmissionEventTag::InBlock);

    /// Close the connection
    void close();

    /// Check if connected to relay
    bool is_connected() const { return connected_; }

private:
    SubmissionServiceConfig config_;
    bool connected_ = false;

    /// Submit via HTTP POST to relay URL
    SubmissionEvent submit_via_http(const SerializedTransaction& tx);

    /// Poll indexer for TX status
    SubmissionEvent wait_for_status(const std::string& tx_hash,
                                     SubmissionEventTag target);
};

// ─── Proving Service ────────────────────────────────────────
// Matching @midnight-ntwrk/wallet-sdk-capabilities/proving

struct ProvingServiceConfig {
    std::string proving_server_url;      ///< Proof server URL (http://localhost:6300)
    uint32_t timeout_ms = 120000;        ///< Proof generation can be slow
};

/// Proof result for a single transaction
struct ProvedTransaction {
    std::vector<uint8_t> data;           ///< Proved TX bytes
    std::string proof_hash;              ///< Hash of the proof
    bool success = false;
    std::string error;
};

class ProvingService {
public:
    explicit ProvingService(const ProvingServiceConfig& config);

    /// Prove an unproven transaction
    /// Matching SDK: prove(transaction: UnprovenTransaction): Promise<UnboundTransaction>
    ProvedTransaction prove(const std::vector<uint8_t>& unproven_tx);

    /// Prove a transaction from JSON representation
    ProvedTransaction prove_json(const json& tx_json);

    /// Check if proof server is available
    bool is_available() const;

    /// Get proof server status
    json get_status();

private:
    ProvingServiceConfig config_;
};

// ─── Pending Transactions Service ───────────────────────────
// Matching @midnight-ntwrk/wallet-sdk-capabilities/pendingTransactions

struct PendingTx {
    std::string tx_hash;
    SubmissionEventTag status = SubmissionEventTag::Submitted;
    std::chrono::system_clock::time_point submitted_at;
    std::chrono::system_clock::time_point last_checked;
    uint64_t block_number = 0;
};

struct PendingTransactionsConfig {
    std::string indexer_http_url;        ///< Indexer URL for polling
    uint32_t poll_interval_ms = 5000;    ///< Polling interval
};

class PendingTransactionsService {
public:
    explicit PendingTransactionsService(const PendingTransactionsConfig& config);
    ~PendingTransactionsService();

    /// Start polling for pending TX status
    void start();

    /// Stop polling
    void stop();

    /// Add a pending transaction to track
    void add_pending_transaction(const std::string& tx_hash);

    /// Clear a transaction (confirmed or failed)
    void clear(const std::string& tx_hash);

    /// Get all pending transactions
    std::vector<PendingTx> get_pending() const;

    /// Get state of a specific transaction
    std::optional<PendingTx> get_state(const std::string& tx_hash) const;

    /// Register callback for status changes
    using StatusCallback = std::function<void(const PendingTx&)>;
    void on_status_change(StatusCallback callback);

    /// Check if running
    bool is_running() const { return running_; }

private:
    PendingTransactionsConfig config_;
    std::vector<PendingTx> pending_;
    std::vector<StatusCallback> callbacks_;
    bool running_ = false;

    /// Poll indexer for TX status updates
    void poll_once();
};

} // namespace midnight::wallet
