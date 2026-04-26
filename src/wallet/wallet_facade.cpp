#include "midnight/wallet/wallet_facade.hpp"
#include "midnight/core/logger.hpp"
#include <sodium.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace midnight::wallet {

// ─── Helpers ─────────────────────────────────────────────────

static std::string to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    return oss.str();
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

    // Initialize state
    facade.state_.unshielded.address = facade.night_addr_;
    facade.state_.dust.address = facade.dust_addr_;

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

    std::string net_str = j.value("network", "preview");
    facade.network_ = address::network_from_string(net_str);

    facade.state_.unshielded.address = facade.night_addr_;
    facade.state_.dust.address = facade.dust_addr_;

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
        case CoinSelectionStrategy::RandomImprove:
            // Shuffle then sort — approximation of random-improve
            // In production, use proper random-improve algorithm
            std::sort(candidates.begin(), candidates.end(),
                      [](const UtxoWithMeta& a, const UtxoWithMeta& b) {
                          return a.amount > b.amount;
                      });
            break;
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
        tx_result.error = "Cannot sign: transaction not built";
        return tx_result;
    }

    std::vector<uint8_t> hash_bytes;
    for (size_t i = 0; i < tx_result.tx_hash.size(); i += 2) {
        auto byte_str = tx_result.tx_hash.substr(i, 2);
        hash_bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
    }

    auto sig = night_key_.sign(hash_bytes);
    tx_result.tx_hash += ":signed:" + to_hex(sig);
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
        result.error = "No outputs specified";
        return result;
    }

    std::map<std::string, uint64_t> required;
    for (const auto& out : outputs) {
        required[out.token_type] += out.amount;
    }

    for (const auto& [token, amount] : required) {
        auto selected = select_coins(amount, token);
        if (selected.empty()) {
            result.error = "Insufficient " + token + " balance: need " +
                           std::to_string(amount) + ", have " +
                           std::to_string(balance(token));
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
    result.fee_estimate = 1000; // Placeholder

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
            result.error = "Proof generation failed: " + proved.error;
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
            result.error = "Submission failed: " + result.submission_event.error;
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
    fee.tx_fee = tx.inputs_used.size() * 500 + tx.change_outputs.size() * 200;
    fee.total_fee = fee.tx_fee + 500; // Balancing TX overhead
    return fee;
}

FeeEstimation WalletFacade::estimate_transaction_fee(const std::vector<TokenTransfer>& outputs) const {
    FeeEstimation fee;
    // Simplified: estimate based on output count
    fee.tx_fee = outputs.size() * 700;
    fee.total_fee = fee.tx_fee + 500;
    return fee;
}

// ─── Dust Registration ───────────────────────────────────────

DustRegistrationResult WalletFacade::register_for_dust(uint64_t night_amount) {
    ensure_not_cleared();

    DustRegistrationResult result;

    if (balance("NIGHT") < night_amount) {
        result.error = "Insufficient NIGHT balance for dust registration: need " +
                       std::to_string(night_amount) + ", have " +
                       std::to_string(balance("NIGHT"));
        return result;
    }

    auto selected = select_coins(night_amount, "NIGHT");
    if (selected.empty()) {
        result.error = "Failed to select coins for registration";
        return result;
    }

    json reg_tx;
    reg_tx["type"] = "dust_registration";
    reg_tx["night_address"] = night_addr_;
    reg_tx["dust_address"] = dust_addr_;
    reg_tx["night_amount"] = night_amount;

    json inputs = json::array();
    for (const auto& coin : selected) {
        inputs.push_back({{"utxo_hash", coin.utxo_hash}, {"amount", coin.amount}});
    }
    reg_tx["inputs"] = inputs;

    std::string tx_str = reg_tx.dump();
    std::vector<uint8_t> tx_bytes(tx_str.begin(), tx_str.end());
    result.tx_hash = sha256_hex_internal(tx_bytes);
    result.success = true;
    result.estimated_dust_per_epoch = night_amount / 1000;

    state_.dust.registered = true;
    state_.dust.registered_night_amount = night_amount;

    if (midnight::g_logger) {
        midnight::g_logger->info("Dust registration TX built: " +
                                  std::to_string(night_amount) + " NIGHT → ~" +
                                  std::to_string(result.estimated_dust_per_epoch) +
                                  " DUST/epoch");
    }

    return result;
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

    json balancing_tx;

    // Step 1: Unshielded balancing — select UTXOs to cover input requirements
    if (token_kinds.should_balance_unshielded) {
        uint64_t unshielded_fee = 500;  // Base unshielded balancing fee
        balancing_tx = create_balancing_tx(finalized_tx, night_addr_, unshielded_fee, ttl);
        balancing_tx["balancing_type"] = "unshielded";
    }

    // Step 2: Shielded balancing (placeholder — requires Zswap circuit for full impl)
    json shielded_tx;
    if (token_kinds.should_balance_shielded) {
        shielded_tx = create_balancing_tx(finalized_tx, night_addr_, 300, ttl);
        shielded_tx["balancing_type"] = "shielded";
    }

    // Merge unshielded + shielded
    json merged = merge_transactions(balancing_tx, shielded_tx);

    // Step 3: Dust/fee balancing
    if (token_kinds.should_balance_dust) {
        json fee_tx = create_balancing_tx(finalized_tx, dust_addr_, 200, ttl);
        fee_tx["balancing_type"] = "dust_fee";
        merged = merge_transactions(merged, fee_tx);
    }

    if (merged.is_null()) {
        recipe.error = "No balancing transaction was created. Please check your transaction.";
        return recipe;
    }

    recipe.balancing_transaction = merged;
    recipe.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("Balanced finalized TX: " +
                                  finalized_tx.value("tx_hash", "?").substr(0, 16) + "...");
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

    // For unbound TXs, unshielded balancing happens in-place (modifies the base TX)
    json base_tx = unbound_tx;
    if (token_kinds.should_balance_unshielded) {
        base_tx["balanced_unshielded"] = true;
        base_tx["balancing_fee_unshielded"] = 500;
    }
    recipe.base_transaction = base_tx;

    // Shielded balancing (separate TX)
    json shielded_tx;
    if (token_kinds.should_balance_shielded) {
        shielded_tx = create_balancing_tx(unbound_tx, night_addr_, 300, ttl);
        shielded_tx["balancing_type"] = "shielded";
    }

    // Dust/fee balancing
    json fee_tx;
    if (token_kinds.should_balance_dust) {
        fee_tx = create_balancing_tx(unbound_tx, dust_addr_, 200, ttl);
        fee_tx["balancing_type"] = "dust_fee";
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
        midnight::g_logger->info("Balanced unbound TX: " +
                                  unbound_tx.value("tx_hash", "?").substr(0, 16) + "...");
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

    // Start with the base TX
    json base_tx = unproven_tx;

    // Step 1: Shielded balancing
    json shielded_tx;
    if (token_kinds.should_balance_shielded) {
        shielded_tx = create_balancing_tx(unproven_tx, night_addr_, 300, ttl);
        shielded_tx["balancing_type"] = "shielded";
    }

    // Step 2: Unshielded balancing (in-place for unproven)
    if (token_kinds.should_balance_unshielded) {
        base_tx["balanced_unshielded"] = true;
        base_tx["balancing_fee_unshielded"] = 500;
    }

    // Step 3: Merge shielded into base
    json merged = merge_transactions(base_tx, shielded_tx);

    // Step 4: Dust/fee balancing
    if (token_kinds.should_balance_dust) {
        json fee_tx = create_balancing_tx(unproven_tx, dust_addr_, 200, ttl);
        fee_tx["balancing_type"] = "dust_fee";
        merged = merge_transactions(merged, fee_tx);
    }

    recipe.transaction = merged;
    recipe.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("Balanced unproven TX: " +
                                  unproven_tx.value("tx_hash", "?").substr(0, 16) + "...");
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

// ─── Serialization ───────────────────────────────────────────

std::string WalletFacade::serialize() const {
    json j;
    j["version"] = 2; // v2 includes services config
    j["network"] = address::network_to_string(network_);
    j["indexer_url"] = indexer_url_;
    j["unshielded_address"] = night_addr_;
    j["dust_address"] = dust_addr_;
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
        network::IndexerClient indexer(indexer_url_);

        auto ws = indexer.query_wallet_state(night_addr_);

        try {
            state_.unshielded.balances["NIGHT"] = std::stoull(ws.unshielded_balance);
        } catch (...) {
            state_.unshielded.balances["NIGHT"] = 0;
        }

        auto utxos = indexer.query_utxos(night_addr_);
        state_.unshielded.available_coins.clear();
        for (const auto& utxo : utxos) {
            UtxoWithMeta meta;
            meta.utxo_hash = utxo.tx_hash + ":" + std::to_string(utxo.output_index);
            meta.output_index = utxo.output_index;
            meta.amount = utxo.amount;
            meta.token_type = "NIGHT";
            meta.tx_hash = utxo.tx_hash;
            meta.ctime = std::chrono::system_clock::now();
            state_.unshielded.available_coins.push_back(meta);
        }

        auto dust_status = indexer.query_dust_status(night_addr_);
        state_.dust.registered = (dust_status == network::DustRegistrationStatus::REGISTERED);

        try {
            auto height = indexer.get_block_height();
            state_.unshielded.progress.highest_transaction_id = height;
            state_.unshielded.progress.applied_id = height;
            state_.unshielded.progress.is_connected = true;
        } catch (...) {
            state_.unshielded.progress.is_connected = false;
        }

        state_.unshielded.synced = ws.error.empty();

        if (midnight::g_logger) {
            midnight::g_logger->info("Wallet synced: " +
                                      std::to_string(state_.night_balance()) + " NIGHT, " +
                                      std::to_string(state_.unshielded.available_coins.size()) +
                                      " UTXOs");
        }

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
