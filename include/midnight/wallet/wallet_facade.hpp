#pragma once

#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/wallet/wallet_services.hpp"
#include "midnight/network/indexer_client.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <map>
#include <chrono>
#include <memory>

namespace midnight::wallet {

using json = nlohmann::json;

// ─── Sync Progress ──────────────────────────────────────────

struct SyncProgress {
    uint64_t applied_id = 0;
    uint64_t highest_transaction_id = 0;
    bool is_connected = false;

    bool is_strictly_complete() const {
        return is_connected && applied_id >= highest_transaction_id;
    }

    bool is_complete_within(uint64_t max_gap = 0) const {
        return is_connected &&
               (highest_transaction_id <= applied_id + max_gap);
    }
};

// ─── UTXO With Metadata ─────────────────────────────────────

struct UtxoWithMeta {
    std::string utxo_hash;
    uint32_t output_index = 0;
    uint64_t amount = 0;
    std::string token_type = "NIGHT";
    std::string tx_hash;
    std::chrono::system_clock::time_point ctime;
    bool registered_for_dust = false;
};

// ─── Token Transfer ─────────────────────────────────────────

struct TokenTransfer {
    uint64_t amount;
    std::string token_type = "NIGHT";
    std::string receiver_address;
};

// ─── Combined Token Transfer (SDK CombinedTokenTransfer) ────

struct CombinedTokenTransfer {
    uint64_t amount;
    std::string token_type = "NIGHT";
    std::string receiver_unshielded;       ///< Unshielded receiver (optional)
    std::string receiver_shielded;         ///< Shielded receiver (optional)
};

// ─── Terms and Conditions (SDK TermsAndConditions) ──────────

struct TermsAndConditions {
    std::string hash;                      ///< Hex-encoded SHA-256 hash of the T&C document
    std::string url;                       ///< URL pointing to the T&C document
};

// ─── Token Kinds To Balance (SDK TokenKindsToBalance) ───────

enum class TokenKind {
    Dust,
    Shielded,
    Unshielded
};

struct TokenKindsToBalance {
    bool should_balance_unshielded = true;
    bool should_balance_shielded = true;
    bool should_balance_dust = true;

    /// Create with all token kinds enabled (SDK: 'all')
    static TokenKindsToBalance all() { return {true, true, true}; }

    /// Create from specific token kinds
    static TokenKindsToBalance from_kinds(const std::vector<TokenKind>& kinds) {
        TokenKindsToBalance result{false, false, false};
        for (auto k : kinds) {
            switch (k) {
                case TokenKind::Unshielded: result.should_balance_unshielded = true; break;
                case TokenKind::Shielded:   result.should_balance_shielded = true; break;
                case TokenKind::Dust:       result.should_balance_dust = true; break;
            }
        }
        return result;
    }
};

// ─── Balancing Recipe (SDK BalancingRecipe) ──────────────────

enum class BalancingRecipeType {
    FinalizedTransaction,
    UnboundTransaction,
    UnprovenTransaction
};

struct BalancingRecipe {
    BalancingRecipeType type;
    json original_transaction;             ///< For FINALIZED_TRANSACTION
    json base_transaction;                 ///< For UNBOUND_TRANSACTION
    json balancing_transaction;            ///< Merged balancing TX (unshielded + shielded + dust)
    json transaction;                      ///< For UNPROVEN_TRANSACTION (single merged TX)
    bool success = false;
    std::string error;

    /// Get all transactions in the recipe (SDK: BalancingRecipe.getTransactions)
    std::vector<json> get_transactions() const {
        std::vector<json> txs;
        switch (type) {
            case BalancingRecipeType::FinalizedTransaction:
                txs.push_back(original_transaction);
                if (!balancing_transaction.is_null())
                    txs.push_back(balancing_transaction);
                break;
            case BalancingRecipeType::UnboundTransaction:
                txs.push_back(base_transaction);
                if (!balancing_transaction.is_null())
                    txs.push_back(balancing_transaction);
                break;
            case BalancingRecipeType::UnprovenTransaction:
                txs.push_back(transaction);
                break;
        }
        return txs;
    }
};

// ─── Swap Types (SDK CombinedSwapInputs / CombinedSwapOutputs) ──

struct SwapInputs {
    std::map<std::string, uint64_t> shielded;     ///< token_type → amount
    std::map<std::string, uint64_t> unshielded;   ///< token_type → amount
};

struct SwapOutputs {
    std::vector<TokenTransfer> shielded_outputs;
    std::vector<TokenTransfer> unshielded_outputs;
};

// ─── Transfer Result ────────────────────────────────────────

struct TransferResult {
    bool success = false;
    std::string tx_hash;
    std::string error;
    std::vector<UtxoWithMeta> inputs_used;
    std::vector<UtxoWithMeta> change_outputs;
    uint64_t fee_estimate = 0;
    SubmissionEvent submission_event;      ///< Submission status from SubmissionService
};

// ─── Dust Registration ──────────────────────────────────────

struct DustRegistrationResult {
    bool success = false;
    std::string tx_hash;
    std::string error;
    uint64_t estimated_dust_per_epoch = 0;
};

// ─── Fee Estimation ─────────────────────────────────────────

struct FeeEstimation {
    uint64_t tx_fee = 0;                   ///< Fee for this transaction only
    uint64_t total_fee = 0;                ///< Total fee including balancing tx
    uint64_t dust_generation_rate = 0;     ///< Estimated DUST/epoch for registration
};

// ─── Coin Selection Strategy ────────────────────────────────

enum class CoinSelectionStrategy {
    LargestFirst,      ///< Greedy largest-first (default, matches SDK)
    SmallestFirst,     ///< Greedy smallest-first
    RandomImprove      ///< Random selection with improvement step
};

using Balances = std::map<std::string, uint64_t>;

// ─── Wallet States ──────────────────────────────────────────

struct UnshieldedWalletState {
    std::string address;
    Balances balances;
    std::vector<UtxoWithMeta> available_coins;
    std::vector<UtxoWithMeta> pending_coins;
    bool synced = false;
    SyncProgress progress;
};

struct DustWalletState {
    std::string address;
    uint64_t balance = 0;
    bool registered = false;
    uint64_t registered_night_amount = 0;
};

struct ShieldedWalletState {
    std::string address;
};

// ─── Combined Facade State ──────────────────────────────────

struct FacadeState {
    UnshieldedWalletState unshielded;
    DustWalletState dust;
    ShieldedWalletState shielded;

    bool is_synced() const { return unshielded.synced; }

    uint64_t night_balance() const {
        auto it = unshielded.balances.find("NIGHT");
        return (it != unshielded.balances.end()) ? it->second : 0;
    }
};

// ─── Wallet Facade Configuration ────────────────────────────
// Matching SDK DefaultConfiguration

struct WalletFacadeConfig {
    std::string indexer_http_url;           ///< Indexer GraphQL endpoint
    std::string indexer_ws_url;             ///< Indexer WebSocket (optional)
    std::string relay_url;                  ///< Node relay for TX submission
    std::string proving_server_url;         ///< Proof server (http://localhost:6300)
    address::Network network = address::Network::Preview;
    CoinSelectionStrategy coin_selection = CoinSelectionStrategy::LargestFirst;
};

// ─── Wallet Facade ──────────────────────────────────────────

class WalletFacade {
public:
    // ─── Factory Methods ─────────────────────────────────────

    static WalletFacade from_mnemonic(
        const std::string& mnemonic,
        const std::string& indexer_url,
        address::Network network = address::Network::Preview);

    static WalletFacade from_wallet(
        const HDWallet& hd_wallet,
        const std::string& indexer_url,
        address::Network network = address::Network::Preview);

    /// Fetch Terms and Conditions from network indexer (SDK static method)
    /// Pre-initialization utility — no wallet instance required
    static TermsAndConditions fetch_terms_and_conditions(const std::string& indexer_url);

    /// Create with full configuration (all services)
    static WalletFacade from_mnemonic_with_config(
        const std::string& mnemonic,
        const WalletFacadeConfig& config);

    /// Restore from serialized state
    static WalletFacade restore(const std::string& serialized_state);

    // ─── Lifecycle (matching SDK start/stop) ─────────────────

    /// Start wallet services (subscription, pending TX tracking)
    void start();

    /// Stop all wallet services
    void stop();

    /// Check if services are running
    bool is_running() const { return running_; }

    // ─── State Queries ───────────────────────────────────────

    FacadeState state() const;
    void sync();
    std::string unshielded_address() const;
    std::string dust_address() const;
    SyncProgress sync_progress() const;

    // ─── Key Access ──────────────────────────────────────────

    const KeyPair& night_key() const { return night_key_; }
    const KeyPair& dust_key() const { return dust_key_; }
    const KeyPair& zswap_key() const { return zswap_key_; }

    // ─── Balance & Coins ─────────────────────────────────────

    std::vector<UtxoWithMeta> available_coins() const;
    std::vector<UtxoWithMeta> pending_coins() const;
    uint64_t balance(const std::string& token_type = "NIGHT") const;
    Balances all_balances() const;

    // ─── Coin Selection ──────────────────────────────────────

    std::vector<UtxoWithMeta> select_coins(
        uint64_t amount,
        const std::string& token_type = "NIGHT") const;

    void set_coin_selection_strategy(CoinSelectionStrategy strategy);

    // ─── Transfer Pipeline (matching SDK transferTransaction) ─

    /**
     * @brief Build + Sign + Prove + Balance + Submit a transfer
     *
     * Full pipeline matching WalletFacade.transferTransaction():
     *   1. build: select UTXOs, create inputs/outputs
     *   2. sign: Ed25519 sign input segments
     *   3. prove: generate ZK proof via proof server
     *   4. balance: pay DUST fees via dust wallet
     *   5. submit: send to relay node
     *
     * @param outputs Transfer outputs
     * @param pay_fees Whether to auto-pay DUST fees (default: true)
     * @param ttl Transaction time-to-live
     * @return TransferResult with full pipeline result
     */
    TransferResult transfer_transaction(
        const std::vector<TokenTransfer>& outputs,
        bool pay_fees = true,
        std::chrono::system_clock::time_point ttl = {});

    /// Build only (step 1) — no signing/proving/submitting
    TransferResult build_transfer(
        const std::vector<TokenTransfer>& outputs,
        std::chrono::system_clock::time_point ttl = {});

    /// Sign a built transfer (step 2)
    TransferResult sign_transaction(TransferResult& tx_result) const;

    /// Submit a signed+proved transaction (step 5)
    SubmissionEvent submit_transaction(const TransferResult& tx_result);

    // ─── Fee Estimation ──────────────────────────────────────

    FeeEstimation calculate_transaction_fee(const TransferResult& tx) const;
    FeeEstimation estimate_transaction_fee(const std::vector<TokenTransfer>& outputs) const;

    // ─── Dust Registration ───────────────────────────────────

    DustRegistrationResult register_for_dust(uint64_t night_amount);
    bool deregister_from_dust();
    uint64_t estimate_dust_generation(uint64_t night_amount) const;
    FeeEstimation estimate_registration(const std::vector<UtxoWithMeta>& night_utxos) const;

    // ─── Transaction Balancing (SDK balanceFinalizedTransaction) ─

    /**
     * @brief Balance a finalized transaction (SDK #8)
     *
     * Runs unshielded, shielded, and dust/fee balancing on a finalized TX.
     * Produces a FinalizedTransactionRecipe containing the original TX
     * and the merged balancing TX.
     *
     * SDK equivalent: WalletFacade.balanceFinalizedTransaction(tx, secretKeys, options)
     */
    BalancingRecipe balance_finalized_transaction(
        const json& finalized_tx,
        std::chrono::system_clock::time_point ttl = {},
        TokenKindsToBalance token_kinds = TokenKindsToBalance::all());

    /**
     * @brief Balance an unbound transaction
     *
     * For unbound TXs, unshielded balancing happens in-place.
     * SDK equivalent: WalletFacade.balanceUnboundTransaction()
     */
    BalancingRecipe balance_unbound_transaction(
        const json& unbound_tx,
        std::chrono::system_clock::time_point ttl = {},
        TokenKindsToBalance token_kinds = TokenKindsToBalance::all());

    /**
     * @brief Balance an unproven transaction
     *
     * Merges all balancing into a single UnprovenTransactionRecipe.
     * SDK equivalent: WalletFacade.balanceUnprovenTransaction()
     */
    BalancingRecipe balance_unproven_transaction(
        const json& unproven_tx,
        std::chrono::system_clock::time_point ttl = {},
        TokenKindsToBalance token_kinds = TokenKindsToBalance::all());

    // ─── Atomic Swap (SDK initSwap #14) ──────────────────────

    /**
     * @brief Initialize an atomic swap transaction
     *
     * Creates a swap TX combining shielded and/or unshielded
     * inputs and outputs, with optional fee payment.
     *
     * SDK equivalent: WalletFacade.initSwap(desiredInputs, desiredOutputs, secretKeys, options)
     */
    BalancingRecipe init_swap(
        const SwapInputs& desired_inputs,
        const std::vector<SwapOutputs>& desired_outputs,
        bool pay_fees = false,
        std::chrono::system_clock::time_point ttl = {});

    // ─── Transaction Revert (matching SDK revert) ────────────

    /// Revert a pending transaction (roll back UTXO changes)
    bool revert_transaction(const TransferResult& tx_result);

    /// Revert from a BalancingRecipe (SDK revert(txOrRecipe))
    bool revert(const BalancingRecipe& recipe);

    // ─── Signing ─────────────────────────────────────────────

    std::vector<uint8_t> sign(const std::vector<uint8_t>& data) const;

    // ─── Serialization ───────────────────────────────────────

    std::string serialize() const;

    // ─── Memory Wipe ─────────────────────────────────────────

    void clear();
    bool is_cleared() const { return cleared_; }

    // ─── Raw Indexer ─────────────────────────────────────────

    json query_indexer(const std::string& graphql_query,
                       const json& variables = json::object());

    // ─── Service Access ──────────────────────────────────────

    SubmissionService* submission_service() { return submission_service_.get(); }
    ProvingService* proving_service() { return proving_service_.get(); }
    PendingTransactionsService* pending_transactions_service() { return pending_tx_service_.get(); }

private:
    HDWallet hd_wallet_;
    KeyPair night_key_;
    KeyPair night_internal_key_;
    KeyPair dust_key_;
    KeyPair zswap_key_;
    KeyPair metadata_key_;
    std::string night_addr_;
    std::string dust_addr_;
    std::string indexer_url_;
    address::Network network_ = address::Network::Preview;
    FacadeState state_;
    bool cleared_ = false;
    bool running_ = false;
    CoinSelectionStrategy coin_strategy_ = CoinSelectionStrategy::LargestFirst;

    // Services
    std::unique_ptr<SubmissionService> submission_service_;
    std::unique_ptr<ProvingService> proving_service_;
    std::unique_ptr<PendingTransactionsService> pending_tx_service_;

    WalletFacade() = default;
    void ensure_not_cleared() const;
};

} // namespace midnight::wallet
