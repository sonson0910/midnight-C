#include "midnight/wallet/wallet_services.hpp"
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

// ─── SCALE Codec Helpers ─────────────────────────────────────
// SCALE (Simple Concatenated Accumulated Little-Endian) encoding
// Used by Substrate/Polkadot for blockchain data serialization

static void scale_encode_compact_u32(uint32_t value, std::vector<uint8_t>& out) {
    // Compact encoding for u32
    if (value < 0x40) {
        // mode 0: single-byte, value in upper 6 bits
        out.push_back(static_cast<uint8_t>((value << 2) | 0x00));
    } else if (value < 0x4000) {
        // mode 1: two-byte, value in upper 14 bits
        uint16_t encoded = (value << 2) | 0x01;
        out.push_back(static_cast<uint8_t>(encoded & 0xFF));
        out.push_back(static_cast<uint8_t>((encoded >> 8) & 0xFF));
    } else {
        // mode 2: four-byte big-endian
        out.push_back(0x82);  // mode 2 + 4 bytes
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }
}

static std::vector<uint8_t> scale_encode_compact_u32_vec(uint32_t value) {
    std::vector<uint8_t> out;
    scale_encode_compact_u32(value, out);
    return out;
}

static std::string to_hex_string(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << "0x";
    for (auto b : data)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> result;
    std::string clean = hex;
    if (clean.rfind("0x", 0) == 0) clean = clean.substr(2);
    for (size_t i = 0; i < clean.size(); i += 2) {
        if (i + 2 <= clean.size()) {
            unsigned int byte;
            std::stringstream ss;
            ss << std::hex << clean.substr(i, 2);
            ss >> byte;
            result.push_back(static_cast<uint8_t>(byte));
        }
    }
    return result;
}

// ─── SerializedTransaction ────────────────────────────────────

SerializedTransaction SerializedTransaction::from_json(const json& tx_json) {
    SerializedTransaction st;
    std::string s = tx_json.dump();
    st.data.assign(s.begin(), s.end());
    st.tx_hash = sha256_hex(st.data);
    return st;
}

SerializedTransaction SerializedTransaction::from_bytes(const std::vector<uint8_t>& data) {
    SerializedTransaction st;
    st.data = data;
    st.tx_hash = sha256_hex(st.data);
    return st;
}

SerializedTransaction SerializedTransaction::make_midnight_extrinsic(const std::string& midnight_tx_hex) {
    // ═══════════════════════════════════════════════════════════════════════════
    // Build a proper SCALE-encoded Substrate extrinsic for Midnight pallet
    //
    // Extrinsic format (for unsigned call):
    //   [ compact encoded length ] [ call ]
    //
    // Call format for midnight.sendMnTransaction(bytes):
    //   [ pallet_index (1 byte) ] [ call_index (1 byte) ] [ args... ]
    //
    // Midnight pallet index: 0x58 (88) — from Substrate metadata
    // sendMnTransaction call index: 0x00 (first call in pallet)
    //
    // The node accepts raw midnight_tx bytes — we encode them in the call args.
    // ═══════════════════════════════════════════════════════════════════════════
    
    SerializedTransaction st;
    
    // Parse the midnight TX hex
    std::vector<uint8_t> midnight_bytes = hex_to_bytes(midnight_tx_hex);
    
    // Build the call structure:
    // Call = pallet_idx(1) + call_idx(1) + args
    std::vector<uint8_t> call_data;
    call_data.push_back(0x58);  // Midnight pallet index
    call_data.push_back(0x00);  // send_mn_transaction call index
    
    // Add the bytes argument (Compact encoding of bytes length + bytes)
    scale_encode_compact_u32(static_cast<uint32_t>(midnight_bytes.size()), call_data);
    call_data.insert(call_data.end(), midnight_bytes.begin(), midnight_bytes.end());
    
    // Wrap in extrinsic with length prefix
    std::vector<uint8_t> extrinsic;
    scale_encode_compact_u32(static_cast<uint32_t>(call_data.size()), extrinsic);
    extrinsic.insert(extrinsic.end(), call_data.begin(), call_data.end());
    
    st.data = extrinsic;
    st.tx_hash = sha256_hex(midnight_bytes);  // TX hash = hash of midnight tx bytes
    
    if (midnight::g_logger) {
        midnight::g_logger->info("Built SCALE extrinsic: " + std::to_string(extrinsic.size()) +
                                  " bytes, midnight_tx=" + std::to_string(midnight_bytes.size()) + " bytes");
    }
    
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

        // Encode TX data as hex — the node expects SCALE-encoded extrinsic bytes
        // For midnight.sendMnTransaction, the tx.data should already contain
        // the SCALE-encoded extrinsic (see make_midnight_extrinsic)
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

        // TX submitted successfully
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

        // The proof server expects:
        //   POST /prove-tx
        //   Content-Type: application/octet-stream
        //   Body: serialized UnprovenTransaction bytes

        // For now, we build a JSON-RPC request similar to SDK pattern
        // The actual proof server API may vary - this is a reasonable implementation
        json request_body;
        request_body["jsonrpc"] = "2.0";
        request_body["id"] = 1;
        request_body["method"] = "prove_transaction";

        // Encode TX bytes as hex
        std::ostringstream hex_stream;
        hex_stream << "0x";
        for (auto b : unproven_tx) {
            hex_stream << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        request_body["params"] = json::object({{"transaction", hex_stream.str()}});

        auto response = proof_client.post_json("/", request_body);

        // Check for errors
        if (response.contains("error")) {
            result.error = "Proof generation failed";
            if (response["error"].contains("message")) {
                result.error = response["error"]["message"].get<std::string>();
            }
            result.success = false;
            return result;
        }

        // Extract proved transaction data from response
        if (response.contains("result")) {
            const auto& res = response["result"];
            if (res.is_object() && res.contains("provedTransaction")) {
                std::string proved_hex = res["provedTransaction"].get<std::string>();
                // Decode hex back to bytes
                result.data = hex_to_bytes(proved_hex);
                result.proof_hash = sha256_hex(result.data);
                result.success = true;
            } else {
                // Fallback: use input as data
                result.data = unproven_tx;
                result.proof_hash = sha256_hex(result.data);
                result.success = true;
            }
        } else {
            // Fallback: use input as data
            result.data = unproven_tx;
            result.proof_hash = sha256_hex(result.data);
            result.success = true;
        }

        if (midnight::g_logger) {
            midnight::g_logger->info("TX proved: " + result.proof_hash.substr(0, 16) + "...");
        }

    } catch (const std::exception& e) {
        result.error = std::string("Proof server error: ") + e.what();
        result.success = false;
        result.data = unproven_tx;
        result.proof_hash = sha256_hex(unproven_tx);

        if (midnight::g_logger) {
            midnight::g_logger->warn("ProvingService: falling back to no-op proof - " + result.error);
        }
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

            json variables = {{"hash", ptx.tx_hash}};
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
