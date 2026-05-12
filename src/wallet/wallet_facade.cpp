#include "midnight/wallet/wallet_facade.hpp"
#include "midnight/wallet/transaction_balancer.hpp"
#include "midnight/crypto/state_encryption.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/network/graphql_subscription.hpp"
#include <sodium.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <functional>

namespace midnight::wallet {

// ─── Helpers ─────────────────────────────────────────────────

static std::string to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

static size_t random_lt(size_t upper_bound) {
    // Returns a uniform random integer in [0, upper_bound).
    // Uses libsodium's secure random for cryptographic safety.
    uint32_t random_u32 = 0;
    const size_t kMaxValid = static_cast<size_t>(UINT32_MAX) -
                                 (static_cast<size_t>(UINT32_MAX) % upper_bound);
    do {
        random_u32 = randombytes_random();
    } while (random_u32 >= kMaxValid);
    return static_cast<size_t>(random_u32 % upper_bound);
}

static std::string sha256_hex_internal(const std::vector<uint8_t>& data) {
    uint8_t hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, data.data(), data.size());
    return to_hex(std::vector<uint8_t>(hash, hash + 32));
}

// ─── Factory Methods ─────────────────────────────────────────

WalletFacade WalletFacade::from_mnemonic(
    const std::string& mnemonic,
    const std::string& indexer_url,
    address::Network network)
{
    auto hd = HDWallet::from_mnemonic(mnemonic);
    return from_wallet(hd, indexer_url, network);
}

WalletFacade WalletFacade::from_wallet(
    const HDWallet& hd_wallet,
    const std::string& indexer_url,
    address::Network network)
{
    WalletFacade facade;
    facade.hd_wallet_ = hd_wallet;
    facade.indexer_url_ = indexer_url;
    facade.network_ = network;

    // Derive all 5 role keys: m/44'/2400'/0'/role/0
    facade.night_key_ = hd_wallet.derive_night(0, 0);
    facade.night_internal_key_ = hd_wallet.derive_night_internal(0, 0);
    facade.dust_key_ = hd_wallet.derive_dust(0, 0);
    facade.zswap_key_ = hd_wallet.derive_zswap(0, 0);
    facade.metadata_key_ = hd_wallet.derive_metadata(0, 0);

    // Encode Bech32m addresses
    facade.night_addr_ = address::encode_unshielded(
        facade.night_key_.public_key, network);
    facade.dust_addr_ = address::encode_dust_from_pubkey(
        facade.dust_key_.public_key, network);

    // Encode shielded address: coinPk || encPk
    // SDK pattern: ShieldedAddress = coinPublicKey + encryptionPublicKey
    //   coin_pk  = zswap public key (Ed25519)
    //   enc_pk   = Curve25519 derived from zswap secret key (via x25519 scalar mult)
    // This links the encryption key to the zswap key cryptographically while
    // keeping the two key types independent (different curves).
    std::vector<uint8_t> enc_pk = hd_wallet.derive_shielded_encryption_key(
        facade.zswap_key_.secret_key);
    facade.shielded_addr_ = address::encode_shielded(
        facade.zswap_key_.public_key,
        enc_pk,
        network);

    // Initialize state
    facade.state_.unshielded.address = facade.night_addr_;
    facade.state_.dust.address = facade.dust_addr_;
    facade.state_.shielded.address = facade.shielded_addr_;

    if (midnight::g_logger) {
        midnight::g_logger->info("WalletFacade created for " +
                                  facade.night_addr_.substr(0, 30) + "...");
    }

    return facade;
}

WalletFacade WalletFacade::from_mnemonic_with_config(
    const std::string& mnemonic,
    const WalletFacadeConfig& config)
{
    auto facade = from_mnemonic(mnemonic, config.indexer_http_url, config.network);
    facade.coin_strategy_ = config.coin_selection;

    // Initialize services if URLs provided
    if (!config.relay_url.empty()) {
        SubmissionServiceConfig sub_cfg;
        sub_cfg.relay_url = config.relay_url;
        facade.submission_service_ = std::make_unique<SubmissionService>(sub_cfg);
    }

    if (!config.proving_server_url.empty()) {
        ProvingServiceConfig prov_cfg;
        prov_cfg.proving_server_url = config.proving_server_url;
        facade.proving_service_ = std::make_unique<ProvingService>(prov_cfg);
    }

    PendingTransactionsConfig ptx_cfg;
    ptx_cfg.indexer_http_url = config.indexer_http_url;
    facade.pending_tx_service_ = std::make_unique<PendingTransactionsService>(ptx_cfg);

    return facade;
}

WalletFacade WalletFacade::restore(const std::string& serialized_state) {
    auto j = json::parse(serialized_state);

    WalletFacade facade;
    facade.indexer_url_ = j.value("indexer_url", "");
    facade.night_addr_ = j.value("unshielded_address", "");
    facade.dust_addr_ = j.value("dust_address", "");
    facade.shielded_addr_ = j.value("shielded_address", "");

    std::string net_str = j.value("network", "preview");
    facade.network_ = address::network_from_string(net_str);

    facade.state_.unshielded.address = facade.night_addr_;
    facade.state_.dust.address = facade.dust_addr_;
    facade.state_.shielded.address = facade.shielded_addr_;

    if (j.contains("balances") && j["balances"].is_object()) {
        for (auto& [key, val] : j["balances"].items()) {
            facade.state_.unshielded.balances[key] = val.get<uint64_t>();
        }
    }

    if (j.contains("sync_progress")) {
        auto& sp = j["sync_progress"];
        facade.state_.unshielded.progress.applied_id = sp.value("applied_id", uint64_t(0));
        facade.state_.unshielded.progress.highest_transaction_id = sp.value("highest_tx_id", uint64_t(0));
        facade.state_.unshielded.progress.is_connected = sp.value("is_connected", false);
    }

    if (j.contains("dust_registered")) {
        facade.state_.dust.registered = j["dust_registered"].get<bool>();
    }
    if (j.contains("dust_registered_night_amount")) {
        facade.state_.dust.registered_night_amount = j["dust_registered_night_amount"].get<uint64_t>();
    }

    // Restore UTXOs
    if (j.contains("available_utxos")) {
        for (const auto& u : j["available_utxos"]) {
            UtxoWithMeta utxo;
            utxo.utxo_hash = u.value("utxo_hash", "");
            utxo.output_index = u.value("output_index", uint32_t(0));
            utxo.amount = u.value("amount", uint64_t(0));
            utxo.token_type = u.value("token_type", "NIGHT");
            utxo.tx_hash = u.value("tx_hash", "");
            utxo.registered_for_dust = u.value("registered_for_dust", false);
            facade.state_.unshielded.available_coins.push_back(utxo);
        }
    }

    facade.cleared_ = true; // Keys need re-derivation

    if (midnight::g_logger) {
        midnight::g_logger->info("WalletFacade restored (state only, keys need re-derivation)");
    }

    return facade;
}

// ─── Lifecycle ───────────────────────────────────────────────

void WalletFacade::start() {
    ensure_not_cleared();
    running_ = true;
    if (pending_tx_service_) {
        pending_tx_service_->start();
    }
    if (midnight::g_logger) {
        midnight::g_logger->info("WalletFacade started");
    }
}

void WalletFacade::stop() {
    running_ = false;
    if (pending_tx_service_) {
        pending_tx_service_->stop();
    }
    if (submission_service_) {
        submission_service_->close();
    }
    if (midnight::g_logger) {
        midnight::g_logger->info("WalletFacade stopped");
    }
}

// ─── State Queries ───────────────────────────────────────────

FacadeState WalletFacade::state() const { return state_; }
std::string WalletFacade::unshielded_address() const { return night_addr_; }
std::string WalletFacade::dust_address() const { return dust_addr_; }
SyncProgress WalletFacade::sync_progress() const { return state_.unshielded.progress; }
std::vector<UtxoWithMeta> WalletFacade::available_coins() const { return state_.unshielded.available_coins; }
std::vector<UtxoWithMeta> WalletFacade::pending_coins() const { return state_.unshielded.pending_coins; }

uint64_t WalletFacade::balance(const std::string& token_type) const {
    auto it = state_.unshielded.balances.find(token_type);
    return (it != state_.unshielded.balances.end()) ? it->second : 0;
}

Balances WalletFacade::all_balances() const { return state_.unshielded.balances; }

// ─── Coin Selection ──────────────────────────────────────────

void WalletFacade::set_coin_selection_strategy(CoinSelectionStrategy strategy) {
    coin_strategy_ = strategy;
}

std::vector<UtxoWithMeta> WalletFacade::select_coins(
    uint64_t amount,
    const std::string& token_type) const
{
    std::vector<UtxoWithMeta> candidates;
    for (const auto& coin : state_.unshielded.available_coins) {
        if (coin.token_type == token_type)
            candidates.push_back(coin);
    }

    switch (coin_strategy_) {
        case CoinSelectionStrategy::SmallestFirst:
            std::sort(candidates.begin(), candidates.end(),
                      [](const UtxoWithMeta& a, const UtxoWithMeta& b) {
                          return a.amount < b.amount;
                      });
            break;
        case CoinSelectionStrategy::RandomImprove: {
            // Fisher-Yates shuffle for random selection using crypto-secure random
            // Fix: was using std::rand() which is not cryptographically secure
            std::vector<UtxoWithMeta> shuffled = candidates;
            for (size_t i = shuffled.size() - 1; i > 0; --i) {
                // Use libsodium's crypto-secure randombytes_uniform for unbiased random index
                size_t j = randombytes_uniform(i + 1);
                if (j != i) std::swap(shuffled[i], shuffled[j]);
            }
            // Accumulate random UTXOs until target is reached
            std::vector<UtxoWithMeta> selected;
            uint64_t accumulated = 0;
            for (const auto& utxo : shuffled) {
                selected.push_back(utxo);
                accumulated += utxo.amount;
                if (accumulated >= amount) break;
            }
            // Improvement pass: try to reduce waste
            // If waste is too high, try replacing the largest selected UTXO
            uint64_t waste = accumulated > amount ? accumulated - amount : 0;
            if (waste > 0) {
                size_t largest_idx = 0;
                for (size_t i = 1; i < selected.size(); ++i) {
                    if (selected[i].amount > selected[largest_idx].amount) largest_idx = i;
                }
                for (const auto& candidate : shuffled) {
                    auto it = std::find_if(selected.begin(), selected.end(),
                        [&](const UtxoWithMeta& u) { return u.utxo_hash == candidate.utxo_hash; });
                    if (it != selected.end()) continue;
                    uint64_t new_accumulated = accumulated - selected[largest_idx].amount + candidate.amount;
                    uint64_t new_waste = new_accumulated > amount ? new_accumulated - amount : 0;
                    if (new_waste < waste) {
                        selected[largest_idx] = candidate;
                        accumulated = new_accumulated;
                        waste = new_waste;
                        break;
                    }
                }
            }
            if (accumulated < amount) return {};
            return selected;
        }
        case CoinSelectionStrategy::LargestFirst:
        default:
            std::sort(candidates.begin(), candidates.end(),
                      [](const UtxoWithMeta& a, const UtxoWithMeta& b) {
                          return a.amount > b.amount;
                      });
            break;
    }

    std::vector<UtxoWithMeta> selected;
    uint64_t total = 0;
    for (const auto& coin : candidates) {
        selected.push_back(coin);
        total += coin.amount;
        if (total >= amount) break;
    }

    if (total < amount) return {};
    return selected;
}

// ─── Signing ─────────────────────────────────────────────────

std::vector<uint8_t> WalletFacade::sign(const std::vector<uint8_t>& data) const {
    ensure_not_cleared();
    return night_key_.sign(data);
}

TransferResult WalletFacade::sign_transaction(TransferResult& tx_result) const {
    ensure_not_cleared();

    if (!tx_result.success || tx_result.tx_hash.empty()) {
        tx_result.error = errors::serialization_error("Cannot sign: transaction not built");
        return tx_result;
    }

    std::vector<uint8_t> hash_bytes;
    for (size_t i = 0; i < tx_result.tx_hash.size(); i += 2) {
        if (i + 2 > tx_result.tx_hash.size()) {
            tx_result.error = errors::serialization_error("Invalid tx_hash: odd-length hex string");
            return tx_result;
        }
        auto byte_str = tx_result.tx_hash.substr(i, 2);
        try {
            size_t pos = 0;
            unsigned long byte_val = std::stoul(byte_str, &pos, 16);
            if (pos != 2) {
                tx_result.error = errors::serialization_error("Invalid tx_hash: non-hex character found");
                return tx_result;
            }
            hash_bytes.push_back(static_cast<uint8_t>(byte_val));
        } catch (const std::exception &) {
            tx_result.error = errors::serialization_error("Invalid tx_hash: failed to parse hex byte");
            return tx_result;
        }
    }

    auto sig = night_key_.sign(hash_bytes);
    // Store signature separately from tx_hash (fix: was appending to tx_hash)
    tx_result.tx_signature = to_hex(sig);
    return tx_result;
}

// ─── Build Transfer (step 1) ─────────────────────────────────

TransferResult WalletFacade::build_transfer(
    const std::vector<TokenTransfer>& outputs,
    std::chrono::system_clock::time_point ttl)
{
    ensure_not_cleared();

    TransferResult result;

    if (outputs.empty()) {
        result.error = errors::serialization_error("No outputs specified");
        return result;
    }

    std::map<std::string, uint64_t> required;
    for (const auto& out : outputs) {
        required[out.token_type] += out.amount;
    }

    for (const auto& [token, amount] : required) {
        auto selected = select_coins(amount, token);
        if (selected.empty()) {
            result.error = errors::insufficient_funds(balance(token), amount, token);
            return result;
        }

        uint64_t input_total = 0;
        for (const auto& coin : selected) {
            input_total += coin.amount;
            result.inputs_used.push_back(coin);
        }

        uint64_t change = input_total - amount;
        if (change > 0) {
            UtxoWithMeta change_utxo;
            change_utxo.amount = change;
            change_utxo.token_type = token;
            change_utxo.ctime = std::chrono::system_clock::now();
            // Fix: Populate all required fields for change UTXO
            change_utxo.receiver_address = night_addr_;
            change_utxo.tx_hash = "";  // Will be set when transaction is finalized
            change_utxo.output_index = 0;  // Will be set when transaction is finalized
            change_utxo.utxo_hash = "";  // Will be set when transaction is finalized
            result.change_outputs.push_back(change_utxo);
        }
    }

    if (ttl == std::chrono::system_clock::time_point{}) {
        ttl = std::chrono::system_clock::now() + std::chrono::hours(1);
    }

    json tx_json;
    tx_json["type"] = "unshielded_transfer";
    tx_json["sender"] = night_addr_;
    tx_json["ttl"] = std::chrono::duration_cast<std::chrono::seconds>(
                         ttl.time_since_epoch()).count();

    json inputs_arr = json::array();
    for (const auto& input : result.inputs_used) {
        inputs_arr.push_back({
            {"utxo_hash", input.utxo_hash},
            {"amount", input.amount},
            {"token_type", input.token_type}
        });
    }
    tx_json["inputs"] = inputs_arr;

    json outputs_arr = json::array();
    for (const auto& out : outputs) {
        outputs_arr.push_back({
            {"receiver", out.receiver_address},
            {"amount", out.amount},
            {"token_type", out.token_type}
        });
    }
    for (const auto& ch : result.change_outputs) {
        outputs_arr.push_back({
            {"receiver", night_addr_},
            {"amount", ch.amount},
            {"token_type", ch.token_type}
        });
    }
    tx_json["outputs"] = outputs_arr;

    std::string tx_str = tx_json.dump();
    std::vector<uint8_t> tx_bytes(tx_str.begin(), tx_str.end());
    result.tx_hash = sha256_hex_internal(tx_bytes);
    result.success = true;
    // TODO: Replace hardcoded fee_estimate with fee from ProtocolParams (min_fee_a, min_fee_b, max_tx_size)
    result.fee_estimate = 1000;

    for (const auto& input : result.inputs_used) {
        state_.unshielded.pending_coins.push_back(input);
    }

    if (midnight::g_logger) {
        midnight::g_logger->info("Transfer TX built: " + result.tx_hash.substr(0, 16) +
                                  "... (" + std::to_string(result.inputs_used.size()) +
                                  " inputs, " + std::to_string(outputs.size()) + " outputs)");
    }

    return result;
}

// ─── Full Transfer Pipeline (steps 1-5) ──────────────────────

TransferResult WalletFacade::transfer_transaction(
    const std::vector<TokenTransfer>& outputs,
    bool pay_fees,
    std::chrono::system_clock::time_point ttl)
{
    ensure_not_cleared();

    // Step 1: Build
    auto result = build_transfer(outputs, ttl);
    if (!result.success) return result;

    // Step 2: Sign
    result = sign_transaction(result);

    // Step 3: Prove (via proof server)
    if (proving_service_) {
        std::string tx_data_str = result.tx_hash;
        std::vector<uint8_t> tx_bytes(tx_data_str.begin(), tx_data_str.end());
        auto proved = proving_service_->prove(tx_bytes);
        if (!proved.success) {
            result.success = false;
            result.error = errors::proof_error("unshield", proved.error);
            return result;
        }
    }

    // Step 4: Balance (pay DUST fees)
    if (pay_fees) {
        auto fee = calculate_transaction_fee(result);
        result.fee_estimate = fee.total_fee;
    }

    // Step 5: Submit
    if (submission_service_) {
        result.submission_event = submit_transaction(result);
        if (result.submission_event.is_error()) {
            result.success = false;
            result.error = errors::network_error("submission", "Submission failed: " + result.submission_event.error);
            return result;
        }
    }

    // Track pending
    if (pending_tx_service_) {
        pending_tx_service_->add_pending_transaction(result.tx_hash);
    }

    return result;
}

// ─── Submit Transaction (step 5) ─────────────────────────────

SubmissionEvent WalletFacade::submit_transaction(const TransferResult& tx_result) {
    if (!submission_service_) {
        return SubmissionEvent{SubmissionEventTag::Submitted, tx_result.tx_hash,
                               0, "", "No submission service configured"};
    }

    auto st = SerializedTransaction::from_json(json{
        {"tx_hash", tx_result.tx_hash},
        {"inputs", tx_result.inputs_used.size()},
        {"fee", tx_result.fee_estimate}
    });

    return submission_service_->submit_transaction(st, SubmissionEventTag::InBlock);
}

// ─── Fee Estimation ──────────────────────────────────────────

FeeEstimation WalletFacade::calculate_transaction_fee(const TransferResult& tx) const {
    FeeEstimation fee;
    // Simplified: each input costs ~500, each output costs ~200
    // TODO: Replace with ProtocolParams-based calculation: min_fee_a * tx_size + min_fee_b
    fee.tx_fee = tx.inputs_used.size() * 500 + tx.change_outputs.size() * 200;
    fee.total_fee = fee.tx_fee + 500; // Balancing TX overhead
    return fee;
}

FeeEstimation WalletFacade::estimate_transaction_fee(const std::vector<TokenTransfer>& outputs) const {
    FeeEstimation fee;
    // Simplified: estimate based on output count
    // TODO: Replace with ProtocolParams-based calculation: min_fee_a * estimated_tx_size + min_fee_b
    fee.tx_fee = outputs.size() * 700;
    fee.total_fee = fee.tx_fee + 500;
    return fee;
}

// ─── Dust Registration ───────────────────────────────────────

DustRegistrationResult WalletFacade::register_for_dust(uint64_t night_amount) {
    DustRegistrationResult result;

    try {
        ensure_not_cleared();

        if (balance("NIGHT") < night_amount) {
            result.error = errors::insufficient_funds(balance("NIGHT"), night_amount, "NIGHT");
            return result;
        }

        auto selected = select_coins(night_amount, "NIGHT");
        if (selected.empty()) {
            result.error = errors::serialization_error("Failed to select coins for dust registration: no coins selected");
            return result;
        }

        // ═══════════════════════════════════════════════════════════════════════════
        // Build the DUST registration payload
        //
        // This matches the Midnight ledger's DustRegistration structure:
        //   struct DustRegistration<S> {
        //     night_key: VerifyingKey,     // 32-byte Ed25519 public key
        //     dust_address: Option<DustPublicKey>,  // 32-byte BLS12-381 scalar
        //     allow_fee_payment: u128,     // boolean as u128
        //     signature: S,               // Ed25519/BIP-340 signature
        //   }
        //
        // The signature covers: Intent::data_to_sign(segment_id)
        // which is a serialized representation of the intent.
        //
        // Since we don't have the full ledger serialization (Rust SCALE codec),
        // we build a compatible payload that can be verified.
        // ═══════════════════════════════════════════════════════════════════════════

        json reg_tx;
        reg_tx["type"] = "dust_registration";
        reg_tx["night_address"] = night_addr_;
        reg_tx["dust_address"] = dust_addr_;
        reg_tx["night_amount"] = night_amount;

        // Add selected inputs
        json inputs_arr = json::array();
        for (const auto& coin : selected) {
            inputs_arr.push_back({
                {"utxo_hash", coin.utxo_hash},
                {"amount", coin.amount}
            });
        }
        reg_tx["inputs"] = inputs_arr;

        // Compute TX hash from canonical JSON bytes
        std::string tx_str = reg_tx.dump();
        std::vector<uint8_t> tx_bytes(tx_str.begin(), tx_str.end());
        result.tx_hash = sha256_hex_internal(tx_bytes);

        midnight::g_logger->info("DUST registration payload: " + std::to_string(tx_bytes.size()) +
                                  " bytes, hash=" + result.tx_hash.substr(0, 16) + "...");

        // ─── Sign the transaction ────────────────────────────────────────────────
        // Ed25519 sign the raw tx bytes with the NIGHT key
        auto signature = night_key_.sign(tx_bytes);
        std::string sig_hex = to_hex(signature);

        // Attach signature to the payload
        reg_tx["signature"] = sig_hex;

        midnight::g_logger->info("Signature: " + sig_hex.substr(0, 16) + "...");

        // ─── Submit to relay node ───────────────────────────────────────────────
        if (!submission_service_) {
            // No submission service — mark local state only
            result.success = true;
            result.estimated_dust_per_epoch = night_amount / 1000;
            state_.dust.registered = true;
            state_.dust.registered_night_amount = night_amount;
            midnight::g_logger->warn("No submission service — TX not broadcast. State updated locally.");
            return result;
        }

        // Build a proper SCALE-encoded Substrate extrinsic for Midnight pallet.
        // The extrinsic format:
        //   Extrinsic = [compact(length)][pallet_idx(0x58)][call_idx(0x00)][args...]
        // For send_mn_transaction, the args are: [compact(bytes_len)][midnight_tx_bytes]
        //
        // We send the full JSON registration payload as the midnight_tx.
        // The node will SCALE-decode it and pass to the Midnight pallet.
        auto st = SerializedTransaction::make_midnight_extrinsic(tx_str);

        midnight::g_logger->info("Submitting extrinsic: " + std::to_string(st.data.size()) +
                                  " bytes, TX hash=" + st.tx_hash.substr(0, 16) + "...");

        auto submit_result = submission_service_->submit_transaction(st, SubmissionEventTag::InBlock);

        if (submit_result.is_error()) {
            result.error = errors::serialization_error("Submission failed: " + submit_result.error);
            midnight::g_logger->error("DUST registration submission failed: " + submit_result.error);
            return result;
        }

        // Update state
        result.success = true;
        result.estimated_dust_per_epoch = night_amount / 1000;
        state_.dust.registered = true;
        state_.dust.registered_night_amount = night_amount;

        if (midnight::g_logger) {
            midnight::g_logger->info("DUST registration submitted: " + submit_result.tx_hash +
                                  " | " + std::to_string(night_amount) + " NIGHT -> ~" +
                                  std::to_string(result.estimated_dust_per_epoch) + " DUST/epoch");
        }

        return result;

    } catch (const std::exception& e) {
        result.error = errors::serialization_error(std::string("DUST registration error: ") + e.what());
        midnight::g_logger->error(std::string("DUST registration error: ") + e.what());
        return result;
    }
}

bool WalletFacade::deregister_from_dust() {
    ensure_not_cleared();
    if (!state_.dust.registered) return false;
    state_.dust.registered = false;
    state_.dust.registered_night_amount = 0;
    if (midnight::g_logger) {
        midnight::g_logger->info("Deregistered from DUST generation");
    }
    return true;
}

uint64_t WalletFacade::estimate_dust_generation(uint64_t night_amount) const {
    return night_amount / 1000;
}

FeeEstimation WalletFacade::estimate_registration(const std::vector<UtxoWithMeta>& night_utxos) const {
    FeeEstimation fee;
    uint64_t total_night = 0;
    for (const auto& u : night_utxos) total_night += u.amount;
    // TODO: Replace with ProtocolParams-based calculation: min_fee_a * tx_size + min_fee_b
    fee.tx_fee = night_utxos.size() * 500 + 200;
    fee.total_fee = fee.tx_fee + 500;
    fee.dust_generation_rate = total_night / 1000;
    return fee;
}

// ─── Revert Transaction ──────────────────────────────────────

bool WalletFacade::revert_transaction(const TransferResult& tx_result) {
    // Remove inputs from pending list
    for (const auto& input : tx_result.inputs_used) {
        auto& pending = state_.unshielded.pending_coins;
        pending.erase(
            std::remove_if(pending.begin(), pending.end(),
                           [&](const UtxoWithMeta& p) { return p.utxo_hash == input.utxo_hash; }),
            pending.end());

        // Restore to available
        state_.unshielded.available_coins.push_back(input);
    }

    // Clear from pending TX service
    if (pending_tx_service_ && !tx_result.tx_hash.empty()) {
        pending_tx_service_->clear(tx_result.tx_hash);
    }

    if (midnight::g_logger) {
        midnight::g_logger->info("Reverted TX: " + tx_result.tx_hash.substr(0, 16) + "...");
    }

    return true;
}

bool WalletFacade::revert(const BalancingRecipe& recipe) {
    auto txs = recipe.get_transactions();
    for (const auto& tx_json : txs) {
        // Clear each TX from pending service
        std::string tx_hash = tx_json.value("tx_hash", "");
        if (pending_tx_service_ && !tx_hash.empty()) {
            pending_tx_service_->clear(tx_hash);
        }
    }

    if (midnight::g_logger) {
        midnight::g_logger->info("Reverted recipe with " +
                                  std::to_string(txs.size()) + " transactions");
    }

    return true;
}

// ─── Fetch Terms and Conditions (SDK #13) ────────────────────

TermsAndConditions WalletFacade::fetch_terms_and_conditions(const std::string& indexer_url) {
    network::IndexerClient indexer(indexer_url);

    // GraphQL query matching SDK's FetchTermsAndConditions query
    std::string query = R"({
        block {
            systemParameters {
                termsAndConditions {
                    hash
                    url
                }
            }
        }
    })";

    auto result = indexer.graphql_query(query);

    TermsAndConditions tc;

    try {
        auto& sys_params = result["data"]["block"]["systemParameters"]["termsAndConditions"];
        if (sys_params.is_null()) {
            throw std::runtime_error(
                "Terms and Conditions are not currently set on the network.");
        }
        tc.hash = sys_params.value("hash", "");
        tc.url = sys_params.value("url", "");
    } catch (const json::exception&) {
        throw std::runtime_error(
            "Terms and Conditions are not currently set on the network.");
    }

    if (tc.hash.empty() && tc.url.empty()) {
        throw std::runtime_error(
            "Terms and Conditions are not currently set on the network.");
    }

    if (midnight::g_logger) {
        midnight::g_logger->info("Fetched T&C: hash=" + tc.hash.substr(0, 16) +
                                  "... url=" + tc.url);
    }

    return tc;
}

// ─── Transaction Balancing (SDK #8) ──────────────────────────

// Helper: create a balancing transaction JSON for the given inputs
static json create_balancing_tx(
    const json& original_tx,
    const std::string& sender_addr,
    uint64_t fee,
    std::chrono::system_clock::time_point ttl)
{
    json bal_tx;
    bal_tx["type"] = "balancing_transaction";
    bal_tx["original_tx_hash"] = original_tx.value("tx_hash", "");
    bal_tx["sender"] = sender_addr;
    bal_tx["fee"] = fee;

    auto ttl_sec = std::chrono::duration_cast<std::chrono::seconds>(
                       ttl.time_since_epoch()).count();
    bal_tx["ttl"] = ttl_sec;

    // Hash the balancing TX
    std::string tx_str = bal_tx.dump();
    std::vector<uint8_t> tx_bytes(tx_str.begin(), tx_str.end());
    uint8_t hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, tx_bytes.data(), tx_bytes.size());

    std::ostringstream oss;
    for (size_t i = 0; i < crypto_hash_sha256_BYTES; ++i)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    bal_tx["tx_hash"] = oss.str();

    return bal_tx;
}

// Helper: merge two JSON transactions (SDK: mergeUnprovenTransactions)
static json merge_transactions(const json& a, const json& b) {
    if (a.is_null()) return b;
    if (b.is_null()) return a;

    json merged = a;
    merged["merged_with"] = b.value("tx_hash", "");

    // Combine inputs/outputs if present, handling both array and object types
    auto merge_field = [&](const std::string& field) {
        if (a.contains(field) && b.contains(field)) {
            auto& target = merged[field];
            const auto& source = b[field];
            if (target.is_array() && source.is_array()) {
                // Array: append items
                for (const auto& item : source) {
                    target.push_back(item);
                }
            } else if (target.is_object() && source.is_object()) {
                // Object: merge key-value pairs (sum values for numeric)
                for (auto& [key, val] : source.items()) {
                    if (target.contains(key) && target[key].is_number() && val.is_number()) {
                        target[key] = target[key].get<uint64_t>() + val.get<uint64_t>();
                    } else {
                        target[key] = val;
                    }
                }
            }
            // Mixed types: keep the first, skip merge
        }
    };

    merge_field("inputs");
    merge_field("outputs");

    // Sum fees
    uint64_t fee_a = a.value("fee", uint64_t(0));
    uint64_t fee_b = b.value("fee", uint64_t(0));
    merged["fee"] = fee_a + fee_b;

    return merged;
}

BalancingRecipe WalletFacade::balance_finalized_transaction(
    const json& finalized_tx,
    std::chrono::system_clock::time_point ttl,
    TokenKindsToBalance token_kinds)
{
    ensure_not_cleared();

    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::FinalizedTransaction;
    recipe.original_transaction = finalized_tx;

    if (ttl == std::chrono::system_clock::time_point{}) {
        ttl = std::chrono::system_clock::now() + std::chrono::hours(1);
    }

    // Step 1: Calculate amounts from finalized TX
    uint64_t input_total = 0;
    uint64_t output_total = 0;

    if (finalized_tx.contains("inputs")) {
        for (const auto& inp : finalized_tx["inputs"]) {
            input_total += inp.value("amount", uint64_t(0));
        }
    }

    if (finalized_tx.contains("outputs")) {
        for (const auto& out : finalized_tx["outputs"]) {
            output_total += out.value("amount", uint64_t(0));
        }
    }

    uint64_t shortfall = input_total > output_total ? input_total - output_total : 0;

    // Step 2: Create TransactionBalancer with available UTXOs
    TransactionBalancer balancer(state_.unshielded.available_coins, shortfall, "NIGHT");

    // Step 3: Calculate fee for balancing transaction
    size_t input_count = finalized_tx.contains("inputs")
        ? (finalized_tx["inputs"].is_array() ? finalized_tx["inputs"].size() : 1)
        : 1;
    size_t output_count = finalized_tx.contains("outputs")
        ? (finalized_tx["outputs"].is_array() ? finalized_tx["outputs"].size() : 1)
        : 1;
    uint64_t fee = TransactionBalancer::calculate_fee(input_count, output_count, false);

    // Step 4: Select UTXOs if unshielded balancing is needed
    json balancing_tx;
    uint64_t total_fee = fee;

    if (token_kinds.should_balance_unshielded && shortfall > 0) {
        auto selection = balancer.select_utxos(shortfall + fee);

        if (selection.selected.empty()) {
            recipe.error = "Insufficient UTXOs to balance finalized transaction";
            return recipe;
        }

        // Calculate actual fee based on selected UTXOs
        uint64_t actual_fee = TransactionBalancer::calculate_fee(selection.selected.size(), 1, false);
        total_fee = actual_fee;

        // Create balancing transaction with selected inputs
        balancing_tx = create_balancing_tx(finalized_tx, night_addr_, actual_fee, ttl);
        balancing_tx["balancing_type"] = "unshielded";

        // Add selected inputs
        json inputs_arr = json::array();
        for (const auto& utxo : selection.selected) {
            inputs_arr.push_back({
                {"utxo_hash", utxo.utxo_hash},
                {"amount", utxo.amount},
                {"token_type", utxo.token_type}
            });
        }
        balancing_tx["inputs"] = inputs_arr;

        // Add output to cover shortfall
        json outputs_arr = json::array();
        outputs_arr.push_back({
            {"receiver", night_addr_},
            {"amount", selection.total_amount - actual_fee},
            {"token_type", "NIGHT"}
        });
        balancing_tx["outputs"] = outputs_arr;
    }

    // Step 5: Shielded balancing (placeholder — requires Zswap circuit for full impl)
    json shielded_tx;
    if (token_kinds.should_balance_shielded) {
        uint64_t shielded_fee = TransactionBalancer::calculate_fee(1, 1, true);
        shielded_tx = create_balancing_tx(finalized_tx, night_addr_, shielded_fee, ttl);
        shielded_tx["balancing_type"] = "shielded";
        total_fee += shielded_fee;
    }

    // Merge unshielded + shielded
    json merged = merge_transactions(balancing_tx, shielded_tx);

    // Step 6: Dust/fee balancing
    if (token_kinds.should_balance_dust) {
        uint64_t dust_fee = TransactionBalancer::calculate_fee(1, 1, false);
        json fee_tx = create_balancing_tx(finalized_tx, dust_addr_, dust_fee, ttl);
        fee_tx["balancing_type"] = "dust_fee";
        merged = merge_transactions(merged, fee_tx);
        total_fee += dust_fee;
    }

    if (merged.is_null()) {
        recipe.error = "No balancing transaction was created. Please check your transaction.";
        return recipe;
    }

    recipe.balancing_transaction = merged;
    recipe.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("Balanced finalized TX: shortfall=" +
                                  std::to_string(shortfall) +
                                  ", total_fee=" + std::to_string(total_fee));
    }

    return recipe;
}

BalancingRecipe WalletFacade::balance_unbound_transaction(
    const json& unbound_tx,
    std::chrono::system_clock::time_point ttl,
    TokenKindsToBalance token_kinds)
{
    ensure_not_cleared();

    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::UnboundTransaction;

    if (ttl == std::chrono::system_clock::time_point{}) {
        ttl = std::chrono::system_clock::now() + std::chrono::hours(1);
    }

    // Step 1: Calculate amounts from unbound TX
    uint64_t input_total = 0;
    uint64_t output_total = 0;

    if (unbound_tx.contains("inputs")) {
        for (const auto& inp : unbound_tx["inputs"]) {
            input_total += inp.value("amount", uint64_t(0));
        }
    }

    if (unbound_tx.contains("outputs")) {
        for (const auto& out : unbound_tx["outputs"]) {
            output_total += out.value("amount", uint64_t(0));
        }
    }

    uint64_t shortfall = input_total > output_total ? input_total - output_total : 0;

    // For unbound TXs, unshielded balancing happens IN-PLACE (modifies the base TX)
    json base_tx = unbound_tx;
    uint64_t total_fee = 0;

    if (token_kinds.should_balance_unshielded && shortfall > 0) {
        uint64_t fee_estimate = TransactionBalancer::calculate_fee(1, 1, false);
        const uint64_t target = shortfall + fee_estimate;

        std::vector<UtxoWithMeta> selected;
        uint64_t selected_total = 0;
        for (const auto& utxo : state_.unshielded.available_coins) {
            if (utxo.token_type != "NIGHT") {
                continue;
            }
            selected.push_back(utxo);
            selected_total += utxo.amount;
            if (selected_total >= target) {
                break;
            }
        }

        if (selected.empty() || selected_total < target) {
            recipe.error = "Insufficient UTXOs for unshielded balancing";
            return recipe;
        }

        uint64_t actual_fee = TransactionBalancer::calculate_fee(selected.size(), 1, false);
        total_fee = actual_fee;

        // Add inputs to base transaction IN-PLACE
        json inputs_arr = json::array();
        if (base_tx.contains("inputs") && base_tx["inputs"].is_array()) {
            for (const auto& inp : base_tx["inputs"]) {
                inputs_arr.push_back(inp);
            }
        }
        for (const auto& utxo : selected) {
            inputs_arr.push_back({
                {"utxo_hash", utxo.utxo_hash},
                {"amount", utxo.amount},
                {"token_type", utxo.token_type}
            });
        }
        base_tx["inputs"] = inputs_arr;

        // Mark as balanced
        base_tx["balanced_unshielded"] = true;
        base_tx["balancing_fee_unshielded"] = actual_fee;
    }

    recipe.base_transaction = base_tx;

    // Step 3: Shielded balancing (separate TX)
    json shielded_tx;
    if (token_kinds.should_balance_shielded) {
        uint64_t shielded_fee = TransactionBalancer::calculate_fee(1, 1, true);
        shielded_tx = create_balancing_tx(unbound_tx, night_addr_, shielded_fee, ttl);
        shielded_tx["balancing_type"] = "shielded";
        total_fee += shielded_fee;
    }

    // Step 4: Dust/fee balancing
    json fee_tx;
    if (token_kinds.should_balance_dust) {
        uint64_t dust_fee = TransactionBalancer::calculate_fee(1, 1, false);
        fee_tx = create_balancing_tx(unbound_tx, dust_addr_, dust_fee, ttl);
        fee_tx["balancing_type"] = "dust_fee";
        total_fee += dust_fee;
    }

    // Merge shielded + fee
    json balancing = merge_transactions(shielded_tx, fee_tx);

    if (balancing.is_null() && !token_kinds.should_balance_unshielded) {
        recipe.error = "No balancing transaction was created. Please check your transaction.";
        return recipe;
    }

    if (!balancing.is_null()) {
        recipe.balancing_transaction = balancing;
    }

    recipe.success = true;

    if (midnight::g_logger) {
        const std::string tx_hash = unbound_tx.value("tx_hash", std::string("?"));
        midnight::g_logger->info("Balanced unbound TX: " +
                                  tx_hash.substr(0, std::min<size_t>(16, tx_hash.size())) +
                                  ", total_fee=" + std::to_string(total_fee));
    }

    return recipe;
}

BalancingRecipe WalletFacade::balance_unproven_transaction(
    const json& unproven_tx,
    std::chrono::system_clock::time_point ttl,
    TokenKindsToBalance token_kinds)
{
    ensure_not_cleared();

    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::UnprovenTransaction;

    if (ttl == std::chrono::system_clock::time_point{}) {
        ttl = std::chrono::system_clock::now() + std::chrono::hours(1);
    }

    // Step 1: Calculate amounts from unproven TX
    uint64_t input_total = 0;
    uint64_t output_total = 0;

    if (unproven_tx.contains("inputs")) {
        for (const auto& inp : unproven_tx["inputs"]) {
            input_total += inp.value("amount", uint64_t(0));
        }
    }

    if (unproven_tx.contains("outputs")) {
        for (const auto& out : unproven_tx["outputs"]) {
            output_total += out.value("amount", uint64_t(0));
        }
    }

    uint64_t shortfall = input_total > output_total ? input_total - output_total : 0;

    // Step 2: Create TransactionBalancer with available UTXOs
    TransactionBalancer balancer(state_.unshielded.available_coins, shortfall, "NIGHT");

    // Start with the base TX
    json base_tx = unproven_tx;
    uint64_t total_fee = 0;

    // Step 3: Unshielded balancing (IN-PLACE for unproven)
    if (token_kinds.should_balance_unshielded && shortfall > 0) {
        uint64_t fee_estimate = TransactionBalancer::calculate_fee(1, 1, false);
        auto selection = balancer.select_utxos(shortfall + fee_estimate);

        if (selection.selected.empty()) {
            recipe.error = "Insufficient UTXOs for unshielded balancing";
            return recipe;
        }

        uint64_t actual_fee = TransactionBalancer::calculate_fee(selection.selected.size(), 1, false);
        total_fee = actual_fee;

        // Add inputs to base transaction IN-PLACE
        json inputs_arr = json::array();
        if (base_tx.contains("inputs") && base_tx["inputs"].is_array()) {
            for (const auto& inp : base_tx["inputs"]) {
                inputs_arr.push_back(inp);
            }
        }
        for (const auto& utxo : selection.selected) {
            inputs_arr.push_back({
                {"utxo_hash", utxo.utxo_hash},
                {"amount", utxo.amount},
                {"token_type", utxo.token_type}
            });
        }
        base_tx["inputs"] = inputs_arr;

        base_tx["balanced_unshielded"] = true;
        base_tx["balancing_fee_unshielded"] = actual_fee;
    }

    // Step 4: Shielded balancing (merged into base TX)
    if (token_kinds.should_balance_shielded) {
        uint64_t shielded_fee = TransactionBalancer::calculate_fee(1, 1, true);
        total_fee += shielded_fee;
        base_tx["needs_shielded_proof"] = true;
        base_tx["shielded_fee"] = shielded_fee;
    }

    // Step 5: Merge shielded into base TX
    json merged_tx = base_tx;

    // Step 6: Dust/fee balancing
    if (token_kinds.should_balance_dust) {
        uint64_t dust_fee = TransactionBalancer::calculate_fee(1, 1, false);
        total_fee += dust_fee;
        merged_tx["needs_dust_fee"] = true;
        merged_tx["dust_fee"] = dust_fee;
    }

    // Update total fee
    merged_tx["total_fee"] = total_fee;

    // Re-hash the merged transaction
    std::string tx_str = merged_tx.dump();
    std::vector<uint8_t> tx_bytes(tx_str.begin(), tx_str.end());
    uint8_t hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, tx_bytes.data(), tx_bytes.size());
    std::ostringstream oss;
    for (size_t i = 0; i < crypto_hash_sha256_BYTES; ++i)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    merged_tx["tx_hash"] = oss.str();

    recipe.transaction = merged_tx;
    recipe.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("Balanced unproven TX: total_fee=" +
                                  std::to_string(total_fee));
    }

    return recipe;
}

// ─── Atomic Swap (SDK #14) ───────────────────────────────────

BalancingRecipe WalletFacade::init_swap(
    const SwapInputs& desired_inputs,
    const std::vector<SwapOutputs>& desired_outputs,
    bool pay_fees,
    std::chrono::system_clock::time_point ttl)
{
    ensure_not_cleared();

    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::UnprovenTransaction;

    if (ttl == std::chrono::system_clock::time_point{}) {
        ttl = std::chrono::system_clock::now() + std::chrono::hours(1);
    }

    bool has_shielded = !desired_inputs.shielded.empty();
    bool has_unshielded = !desired_inputs.unshielded.empty();

    // Check that outputs exist too
    for (const auto& out : desired_outputs) {
        if (!out.shielded_outputs.empty()) has_shielded = true;
        if (!out.unshielded_outputs.empty()) has_unshielded = true;
    }

    if (!has_shielded && !has_unshielded) {
        recipe.error = "At least one shielded or unshielded swap is required.";
        return recipe;
    }

    auto ttl_sec = std::chrono::duration_cast<std::chrono::seconds>(
                       ttl.time_since_epoch()).count();

    // Build shielded swap TX
    json shielded_tx;
    if (has_shielded) {
        shielded_tx["type"] = "swap_shielded";
        shielded_tx["sender"] = night_addr_;
        shielded_tx["ttl"] = ttl_sec;

        json inputs_arr = json::object();
        for (const auto& [token, amount] : desired_inputs.shielded) {
            inputs_arr[token] = amount;
        }
        shielded_tx["inputs"] = inputs_arr;

        json outputs_arr = json::array();
        for (const auto& out_group : desired_outputs) {
            for (const auto& out : out_group.shielded_outputs) {
                outputs_arr.push_back({
                    {"receiver", out.receiver_address},
                    {"amount", out.amount},
                    {"token_type", out.token_type}
                });
            }
        }
        shielded_tx["outputs"] = outputs_arr;

        // Hash
        std::string s = shielded_tx.dump();
        std::vector<uint8_t> bytes(s.begin(), s.end());
        shielded_tx["tx_hash"] = sha256_hex_internal(bytes);
    }

    // Build unshielded swap TX
    json unshielded_tx;
    if (has_unshielded) {
        unshielded_tx["type"] = "swap_unshielded";
        unshielded_tx["sender"] = night_addr_;
        unshielded_tx["ttl"] = ttl_sec;

        json inputs_arr = json::object();
        for (const auto& [token, amount] : desired_inputs.unshielded) {
            inputs_arr[token] = amount;
        }
        unshielded_tx["inputs"] = inputs_arr;

        json outputs_arr = json::array();
        for (const auto& out_group : desired_outputs) {
            for (const auto& out : out_group.unshielded_outputs) {
                outputs_arr.push_back({
                    {"receiver", out.receiver_address},
                    {"amount", out.amount},
                    {"token_type", out.token_type}
                });
            }
        }
        unshielded_tx["outputs"] = outputs_arr;

        // Hash
        std::string s = unshielded_tx.dump();
        std::vector<uint8_t> bytes(s.begin(), s.end());
        unshielded_tx["tx_hash"] = sha256_hex_internal(bytes);
    }

    // Merge shielded + unshielded
    json combined = merge_transactions(shielded_tx, unshielded_tx);

    if (combined.is_null()) {
        recipe.error = "Unexpected transaction state.";
        return recipe;
    }

    // Optional fee payment
    if (pay_fees) {
        // TODO: Replace 200 with fee from ProtocolParams (min_fee_a * tx_size + min_fee_b)
        json fee_tx = create_balancing_tx(combined, dust_addr_, 200, ttl);
        fee_tx["balancing_type"] = "swap_fee";
        combined = merge_transactions(combined, fee_tx);
    }

    recipe.transaction = combined;
    recipe.success = true;

    if (midnight::g_logger) {
        std::string desc;
        if (has_shielded && has_unshielded) desc = "shielded+unshielded";
        else if (has_shielded) desc = "shielded";
        else desc = "unshielded";
        midnight::g_logger->info("Swap initiated: " + desc +
                                  " (" + std::to_string(desired_outputs.size()) +
                                  " output groups, fees=" +
                                  std::string(pay_fees ? "yes" : "no") + ")");
    }

    return recipe;
}

// ─── Shielded Address ─────────────────────────────────────────

std::string WalletFacade::shielded_address() const {
    ensure_not_cleared();
    return shielded_addr_;
}

std::optional<ShieldedAddress> WalletFacade::get_shielded_address() const {
    ensure_not_cleared();

    if (shielded_addr_.empty()) {
        return std::nullopt;
    }

    // The shielded address is already stored as a string
    // We need to reconstruct the ShieldedAddress object
    // by decoding the bech32m format
    try {
        auto decoded = address::decode_shielded(shielded_addr_);
        if (decoded.coin_public_key.size() != 32 ||
            decoded.encryption_public_key.size() != 32) {
            return std::nullopt;
        }

        ShieldedCoinPublicKey coin_key(decoded.coin_public_key);
        ShieldedEncryptionPublicKey enc_key(decoded.encryption_public_key);

        return ShieldedAddress(coin_key, enc_key, network_);
    } catch (const std::exception& e) {
        if (midnight::g_logger) {
            midnight::g_logger->warn("Failed to parse shielded address: " +
                                   std::string(e.what()));
        }
        return std::nullopt;
    }
}

// ─── Serialization ───────────────────────────────────────────

std::string WalletFacade::serialize() const {
    json j;
    j["version"] = 3; // v3 includes shielded address
    j["network"] = address::network_to_string(network_);
    j["indexer_url"] = indexer_url_;
    j["unshielded_address"] = night_addr_;
    j["dust_address"] = dust_addr_;
    j["shielded_address"] = shielded_addr_;
    j["balances"] = state_.unshielded.balances;
    j["dust_registered"] = state_.dust.registered;
    j["dust_registered_night_amount"] = state_.dust.registered_night_amount;

    j["sync_progress"] = {
        {"applied_id", state_.unshielded.progress.applied_id},
        {"highest_tx_id", state_.unshielded.progress.highest_transaction_id},
        {"is_connected", state_.unshielded.progress.is_connected}
    };

    json utxos = json::array();
    for (const auto& coin : state_.unshielded.available_coins) {
        utxos.push_back({
            {"utxo_hash", coin.utxo_hash},
            {"output_index", coin.output_index},
            {"amount", coin.amount},
            {"token_type", coin.token_type},
            {"tx_hash", coin.tx_hash},
            {"registered_for_dust", coin.registered_for_dust}
        });
    }
    j["available_utxos"] = utxos;

    json pending = json::array();
    for (const auto& coin : state_.unshielded.pending_coins) {
        pending.push_back({
            {"utxo_hash", coin.utxo_hash},
            {"amount", coin.amount},
            {"token_type", coin.token_type}
        });
    }
    j["pending_utxos"] = pending;

    auto now = std::chrono::system_clock::now();
    j["serialized_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                             now.time_since_epoch()).count();

    return j.dump(2);
}

// ─── Base64 Helpers ──────────────────────────────────────────

static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* b64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    int i = 0;
    int j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    
    for (size_t pos = 0; pos < data.size(); ++pos) {
        char_array_3[i++] = data[pos];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                result += b64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    
    if (i > 0) {
        for (j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (j = 0; j < i + 1; j++) {
            result += b64_chars[char_array_4[j]];
        }
        while (i++ < 3) {
            result += '=';
        }
    }
    
    return result;
}

static std::optional<std::vector<uint8_t>> base64_decode(const std::string& input) {
    static int b64_table[256] = {};
    static bool b64_table_initialized = false;
    
    if (!b64_table_initialized) {
        for (int i = 0; i < 256; i++) {
            b64_table[i] = -1;
        }
        const char* b64_chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";
        for (int i = 0; b64_chars[i] != '\0'; i++) {
            b64_table[(unsigned char)b64_chars[i]] = i;
        }
        b64_table_initialized = true;
    }
    
    std::vector<uint8_t> result;
    int i = 0;
    uint8_t char_array_4[4];
    
    for (char c : input) {
        if (c == '=') break;
        if (!isalnum((unsigned char)c) && c != '+' && c != '/') continue;
        
        char_array_4[i++] = (unsigned char)c;
        if (i == 4) {
            for (int k = 0; k < 4; k++) {
                char_array_4[k] = (unsigned char)b64_table[char_array_4[k]];
            }
            result.push_back((char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4));
            result.push_back(((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2));
            result.push_back(((char_array_4[2] & 0x3) << 6) + char_array_4[3]);
            i = 0;
        }
    }
    
    if (i > 0) {
        for (int k = i; k < 4; k++) {
            char_array_4[k] = 0;
        }
        for (int k = 0; k < i - 1; k++) {
            result.push_back((char_array_4[k] << 2) + ((char_array_4[k+1] & 0x30) >> 4));
        }
    }
    
    return result;
}

// ─── Encrypted Serialization ─────────────────────────────────

std::string WalletFacade::serialize_encrypted(const std::string& password) const {
    // 1. Get JSON serialization
    std::string json_state = serialize();
    
    // 2. Encrypt the JSON
    auto encrypted = midnight::crypto::EncryptedState::create(json_state, password);
    
    // 3. Return base64 encoded result
    return base64_encode(encrypted);
}

bool WalletFacade::restore_from_encrypted(
    const std::string& encrypted_base64,
    const std::string& password) 
{
    // 1. Decode base64
    auto encrypted = base64_decode(encrypted_base64);
    if (!encrypted) {
        if (midnight::g_logger) {
            midnight::g_logger->error("Failed to decode base64 encrypted state");
        }
        return false;
    }
    
    // 2. Decrypt the state
    auto json_state = midnight::crypto::EncryptedState::decrypt(*encrypted, password);
    if (!json_state) {
        if (midnight::g_logger) {
            midnight::g_logger->error("Failed to decrypt wallet state - wrong password or corrupted data");
        }
        return false;
    }
    
    // 3. Restore from JSON
    auto restored = restore(*json_state);
    // Copy state and keys only (not mutexes)
    hd_wallet_ = std::move(restored.hd_wallet_);
    night_key_ = std::move(restored.night_key_);
    night_internal_key_ = std::move(restored.night_internal_key_);
    dust_key_ = std::move(restored.dust_key_);
    zswap_key_ = std::move(restored.zswap_key_);
    metadata_key_ = std::move(restored.metadata_key_);
    night_addr_ = std::move(restored.night_addr_);
    dust_addr_ = std::move(restored.dust_addr_);
    shielded_addr_ = std::move(restored.shielded_addr_);
    indexer_url_ = std::move(restored.indexer_url_);
    network_ = restored.network_;
    cleared_ = restored.cleared_;
    running_ = restored.running_;
    coin_strategy_ = restored.coin_strategy_;
    state_ = std::move(restored.state_);
    submission_service_ = std::move(restored.submission_service_);
    proving_service_ = std::move(restored.proving_service_);
    pending_tx_service_ = std::move(restored.pending_tx_service_);

    if (midnight::g_logger) {
        midnight::g_logger->info("WalletFacade restored from encrypted state");
    }

    return true;
}

// ─── Memory Wipe ─────────────────────────────────────────────

void WalletFacade::clear() {
    auto wipe_key = [](KeyPair& kp) {
        if (!kp.secret_key.empty()) {
            sodium_memzero(kp.secret_key.data(), kp.secret_key.size());
            kp.secret_key.clear();
        }
        if (!kp.public_key.empty()) {
            sodium_memzero(kp.public_key.data(), kp.public_key.size());
            kp.public_key.clear();
        }
        kp.address.clear();
    };

    wipe_key(night_key_);
    wipe_key(night_internal_key_);
    wipe_key(dust_key_);
    wipe_key(zswap_key_);
    wipe_key(metadata_key_);

    cleared_ = true;

    // Stop services
    stop();

    if (midnight::g_logger) {
        midnight::g_logger->info("WalletFacade: all key material securely wiped");
    }
}

void WalletFacade::ensure_not_cleared() const {
    if (cleared_) {
        throw std::runtime_error(
            "WalletFacade has been cleared — key material is no longer available. "
            "Re-create via from_mnemonic() or from_wallet().");
    }
}

// ─── Sync ────────────────────────────────────────────────────

void WalletFacade::sync() {
    try {
        //
        // ═══════════════════════════════════════════════════════════════════════════════════
        // OPTIMIZED SYNC STRATEGY
        //
        // Approach (in priority order):
        //   1. WebSocket unshieldedTransactions subscription → fast + real-time
        //   2. HTTP concurrent block scan → fallback (~40s for full chain)
        //
        // CONCURRENCY STRATEGY (HTTP fallback):
        //   - Submit N block-query tasks to a thread pool (bounded by kMaxConcurrency)
        //   - Each task: query one block via IndexerClient, return UTXOs
        //   - Main thread collects results as they complete
        //   - This is the SAME approach that worked in Python (~6.8s for 200 blocks)
        //
        // KEY INSIGHT: cpp-httplib synchronous HTTP is thread-safe.
        //   Multiple threads calling graphql_query() simultaneously = multiple
        //   concurrent TCP connections = TRUE parallelism.
        //
        // ═══════════════════════════════════════════════════════════════════════════════════

        std::string http_url = indexer_url_;
        std::string ws_url = http_url;
        if (ws_url.find("https://") == 0) {
            ws_url = "wss://" + ws_url.substr(8);
        } else if (ws_url.find("http://") == 0) {
            ws_url = "ws://" + ws_url.substr(7);
        }
        if (ws_url.find("/api/v4/graphql") == std::string::npos) {
            ws_url += (ws_url.back() == '/') ? "api/v4/graphql" : "/api/v4/graphql";
        }

        // ── Attempt 1: WebSocket subscription ──────────────────────────────────────
        midnight::g_logger->info("Sync attempt 1: WebSocket unshieldedTransactions subscription...");
        bool sync_ok = false;

        {
            network::RealtimeIndexerClient rtc(ws_url, http_url);

            std::atomic<bool> catchup_done{false};
            std::atomic<bool> has_error{false};
            std::string error_msg;

            rtc.on_progress([&](uint64_t highest_tx_id) {
                state_.unshielded.progress.highest_transaction_id = highest_tx_id;
                midnight::g_logger->info("WS progress: highest tx " + std::to_string(highest_tx_id));
                if (highest_tx_id > 0 && state_.unshielded.progress.applied_id > 0) {
                    catchup_done = true;
                }
            });

            rtc.on_utxo([&](const network::UnshieldedTxEvent& evt) {
                for (const network::UtxoEvent& utxo : evt.created) {
                    UtxoWithMeta meta;
                    meta.utxo_hash = utxo.tx_hash + ":" + std::to_string(utxo.output_index);
                    meta.output_index = utxo.output_index;
                    meta.amount = utxo.amount;
                    meta.token_type = utxo.token_type.empty() ? "NIGHT" : utxo.token_type;
                    meta.tx_hash = utxo.tx_hash;
                    meta.ctime = std::chrono::system_clock::now();
                    meta.registered_for_dust = utxo.registered_for_dust_generation;
                    state_.unshielded.available_coins.push_back(meta);
                }
                for (const network::UtxoEvent& utxo : evt.spent) {
                    std::string hash = utxo.tx_hash + ":" + std::to_string(utxo.output_index);
                    auto it = std::remove_if(state_.unshielded.available_coins.begin(),
                                             state_.unshielded.available_coins.end(),
                                             [&](const UtxoWithMeta& m) { return m.utxo_hash == hash; });
                    if (it != state_.unshielded.available_coins.end())
                        state_.unshielded.available_coins.erase(it, state_.unshielded.available_coins.end());
                }
                uint64_t total = 0;
                for (const auto& c : state_.unshielded.available_coins)
                    if (c.token_type == "NIGHT") total += c.amount;
                state_.unshielded.balances["NIGHT"] = total;
                if (evt.transaction_id > 0)
                    state_.unshielded.progress.applied_id = evt.transaction_id;
            });

            rtc.on_connection([&](bool ok, const std::string& msg) {
                if (!ok) { has_error = true; error_msg = msg; catchup_done = true; }
            });

            uint64_t resume_from = state_.unshielded.progress.applied_id;
            midnight::g_logger->info("WS: subscribing for " +
                                      night_addr_.substr(0, 16) + "... from tx " +
                                      std::to_string(resume_from));

            if (rtc.subscribe_resume(night_addr_, resume_from)) {
                auto start = std::chrono::steady_clock::now();
                while (!catchup_done && !has_error) {
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (elapsed > std::chrono::seconds(20)) {
                        midnight::g_logger->warn("WS sync timeout after 20s");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                sync_ok = !has_error && state_.unshielded.progress.applied_id > 0;
            }
        } // rtc destroyed here

        // ── Attempt 2: HTTP concurrent block scan ─────────────────────────────────
        if (!sync_ok) {
            midnight::g_logger->info("Sync attempt 2: HTTP concurrent block scan...");

            constexpr uint32_t kMaxConcurrency = 16;   // parallel HTTP workers
            constexpr uint32_t kBatchReport = 200;     // progress log interval

            // Get chain tip and determine scan range
            network::IndexerClient indexer(http_url);
            uint32_t tip = indexer.get_block_height();
            uint32_t from_block = state_.unshielded.progress.applied_id > 0
                                      ? static_cast<uint32_t>(state_.unshielded.progress.applied_id)
                                      : 609178u;  // first indexed block
            midnight::g_logger->info("HTTP scan: blocks " + std::to_string(from_block) +
                                    " → " + std::to_string(tip) +
                                    " (" + std::to_string(tip - from_block) + " blocks)");

            // ── Thread-safe shared state for worker threads ──────────────────────────
            std::mutex results_mutex;
            std::vector<std::pair<uint32_t, blockchain::UTXO>> all_utxos;  // (block_height, utxo)
            std::atomic<uint32_t> next_block{from_block};
            std::atomic<uint32_t> completed_blocks{0};
            std::atomic<bool> stop_flag{false};

            // ── Worker function: each thread runs this ───────────────────────────
            auto worker_fn = [&]() {
                network::IndexerClient local_indexer(http_url);
                while (!stop_flag.load()) {
                    uint32_t block_height = next_block.fetch_add(1);
                    if (block_height > tip) break;

                    std::vector<blockchain::UTXO> block_utxos =
                        local_indexer.query_utxos(night_addr_, block_height, block_height);

                    std::lock_guard<std::mutex> lock(results_mutex);
                    for (auto& utxo : block_utxos) {
                        all_utxos.emplace_back(block_height, std::move(utxo));
                    }
                    ++completed_blocks;

                    // Progress log every kBatchReport blocks
                    uint32_t done = completed_blocks.load();
                    if (done % kBatchReport == 0) {
                        midnight::g_logger->info("  Scan: " + std::to_string(done) +
                                                " blocks done, current=" +
                                                std::to_string(block_height) +
                                                "/" + std::to_string(tip));
                    }
                }
            };

            // ── Launch thread pool and submit ALL work ───────────────────────────
            midnight::g_logger->info("Launching " + std::to_string(kMaxConcurrency) +
                                    " concurrent HTTP workers...");
            std::vector<std::thread> workers;
            workers.reserve(kMaxConcurrency);
            auto pool_start = std::chrono::steady_clock::now();

            for (uint32_t i = 0; i < kMaxConcurrency; ++i) {
                workers.emplace_back(worker_fn);
            }

            // Wait for ALL workers to finish
            for (auto& t : workers) {
                t.join();
            }

            auto pool_end = std::chrono::steady_clock::now();
            double elapsed_s = std::chrono::duration<double>(pool_end - pool_start).count();
            midnight::g_logger->info("HTTP scan complete: " +
                                    std::to_string(all_utxos.size()) + " UTXOs from " +
                                    std::to_string(completed_blocks.load()) + " blocks in " +
                                    std::to_string(static_cast<int>(elapsed_s)) + "s");

            // ── Populate wallet state ─────────────────────────────────────────────
            uint64_t total_night = 0;
            for (auto& [blk, utxo] : all_utxos) {
                UtxoWithMeta meta;
                meta.utxo_hash = utxo.tx_hash + ":" + std::to_string(utxo.output_index);
                meta.output_index = utxo.output_index;
                meta.amount = utxo.value;
                meta.token_type = utxo.token_type.empty() ? "NIGHT" : utxo.token_type;
                meta.tx_hash = utxo.tx_hash;
                meta.ctime = std::chrono::system_clock::now();
                meta.registered_for_dust = utxo.registered_for_dust_generation;
                state_.unshielded.available_coins.push_back(meta);
                if (meta.token_type == "NIGHT") total_night += meta.amount;
                (void)blk;  // silence unused warning
            }

            // Update progress: store last scanned block
            if (!all_utxos.empty()) {
                state_.unshielded.progress.applied_id = all_utxos.back().first;
            } else {
                state_.unshielded.progress.applied_id = tip;
            }

            state_.unshielded.balances["NIGHT"] = total_night;
            midnight::g_logger->info("Balance: " + std::to_string(total_night) + " NIGHT from " +
                                    std::to_string(all_utxos.size()) + " UTXOs");
            sync_ok = true;
        }

        // ── DUST status (HTTP one-shot) ─────────────────────────────────────────
        state_.dust.registered = false;
        state_.dust.registered_night_amount = 0;
        for (const auto& coin : state_.unshielded.available_coins) {
            if (coin.registered_for_dust) {
                state_.dust.registered = true;
                state_.dust.registered_night_amount += coin.amount;
            }
        }

        state_.unshielded.synced = sync_ok;
        state_.unshielded.progress.is_connected = sync_ok;

        midnight::g_logger->info("Sync complete: " +
                                  std::to_string(state_.night_balance()) + " NIGHT, " +
                                  std::to_string(state_.unshielded.available_coins.size()) +
                                  " UTXOs, DUST: " + (state_.dust.registered ? "registered" : "not registered"));

    } catch (const std::exception& e) {
        if (midnight::g_logger) {
            midnight::g_logger->error("Wallet sync failed: " + std::string(e.what()));
        }
        state_.unshielded.synced = false;
        state_.unshielded.progress.is_connected = false;
    }
}

// ─── Raw Indexer ─────────────────────────────────────────────

json WalletFacade::query_indexer(const std::string& graphql_query,
                                  const json& variables) {
    network::IndexerClient indexer(indexer_url_);
    return indexer.graphql_query(graphql_query, variables);
}

} // namespace midnight::wallet
