#pragma once

#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/wallet/shielded_address.hpp"
#include "midnight/wallet/wallet_services.hpp"
#include "midnight/network/indexer_client.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/errors/wallet_errors.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <map>
#include <chrono>
#include <memory>
#include <mutex>

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
    std::string receiver_address;  // Address that can spend this UTXO
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

// ─── Transfer Result ────────────────────────────────────────

struct TransferResult {
    bool success = false;
    std::string tx_hash;
    std::string tx_signature;  // BIP-340/Ed25519 signature (stored separately, not in tx_hash)
    std::vector<UtxoWithMeta> inputs_used;
    std::vector<UtxoWithMeta> change_outputs;
    uint64_t fee_estimate = 0;
    SubmissionEvent submission_event;
    
    using ErrorType = errors::WalletError;
    std::optional<ErrorType> error;
    
    std::string error_message() const {
        if (error) {
            return errors::get_message(*error);
        }
        return {};
    }
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
    std::vector<blockchain::UTXO> utxos;
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
    address::Network network = address::Network::PreProd;
    CoinSelectionStrategy coin_selection = CoinSelectionStrategy::LargestFirst;
};

// ─── Wallet Facade ──────────────────────────────────────────

class WalletFacade {
public:
    // Move constructor and assignment (required because of mutex members)
    WalletFacade(WalletFacade&& other) noexcept
        : hd_wallet_(std::move(other.hd_wallet_))
        , night_key_(std::move(other.night_key_))
        , night_internal_key_(std::move(other.night_internal_key_))
        , dust_key_(std::move(other.dust_key_))
        , zswap_key_(std::move(other.zswap_key_))
        , metadata_key_(std::move(other.metadata_key_))
        , night_addr_(std::move(other.night_addr_))
        , dust_addr_(std::move(other.dust_addr_))
        , shielded_addr_(std::move(other.shielded_addr_))
        , indexer_url_(std::move(other.indexer_url_))
        , network_(other.network_)
        , state_(std::move(other.state_))
        , cleared_(other.cleared_)
        , running_(other.running_)
        , coin_strategy_(other.coin_strategy_)
        , submission_service_(std::move(other.submission_service_))
        , proving_service_(std::move(other.proving_service_))
        , pending_tx_service_(std::move(other.pending_tx_service_))
        , state_mutex_()
        , pending_mutex_()
    {}

    WalletFacade& operator=(WalletFacade&& other) noexcept {
        if (this != &other) {
            hd_wallet_ = std::move(other.hd_wallet_);
            night_key_ = std::move(other.night_key_);
            night_internal_key_ = std::move(other.night_internal_key_);
            dust_key_ = std::move(other.dust_key_);
            zswap_key_ = std::move(other.zswap_key_);
            metadata_key_ = std::move(other.metadata_key_);
            night_addr_ = std::move(other.night_addr_);
            dust_addr_ = std::move(other.dust_addr_);
            shielded_addr_ = std::move(other.shielded_addr_);
            indexer_url_ = std::move(other.indexer_url_);
            network_ = other.network_;
            state_ = std::move(other.state_);
            cleared_ = other.cleared_;
            running_ = other.running_;
            coin_strategy_ = other.coin_strategy_;
            submission_service_ = std::move(other.submission_service_);
            proving_service_ = std::move(other.proving_service_);
            pending_tx_service_ = std::move(other.pending_tx_service_);
            // Mutexes are default-constructed (not copied/moved)
        }
        return *this;
    }

    // Allow test fixtures to inject state for testing (thread-safe)
    void set_available_coins(const std::vector<UtxoWithMeta>& coins) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.unshielded.available_coins = coins;
    }
    void set_balance(const std::string& token, uint64_t amount) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.unshielded.balances[token] = amount;
    }

    // Thread-safe state accessors
    FacadeState get_state() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_;
    }

    void update_state(const FacadeState& new_state) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = new_state;
    }

    // ─── Factory Methods ─────────────────────────────────────

    static WalletFacade from_mnemonic(
        const std::string& mnemonic,
        const std::string& indexer_url,
        address::Network network = address::Network::PreProd);

    static WalletFacade from_wallet(
        const HDWallet& hd_wallet,
        const std::string& indexer_url,
        address::Network network = address::Network::PreProd);

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

    // ─── Transaction Revert (matching SDK revert) ────────────

    /// Revert a pending transaction (roll back UTXO changes)
    bool revert_transaction(const TransferResult& tx_result);

    /// Revert from a BalancingRecipe (SDK revert(txOrRecipe))
    bool revert(const BalancingRecipe& recipe);

    // ─── Signing ─────────────────────────────────────────────

    std::vector<uint8_t> sign(const std::vector<uint8_t>& data) const;

    // ─── Shielded Address ────────────────────────────────────────
    std::string shielded_address() const;
    std::optional<ShieldedAddress> get_shielded_address() const;

    // ─── Serialization ───────────────────────────────────────

    std::string serialize() const;
    
    /**
     * @brief Serialize wallet state and encrypt with password
     * @param password Encryption password
     * @return Base64-encoded encrypted state
     */
    std::string serialize_encrypted(const std::string& password) const;
    
    /**
     * @brief Restore wallet from encrypted state
     * @param encrypted_base64 Base64-encoded encrypted state
     * @param password Decryption password
     * @return true if restored successfully
     */
    bool restore_from_encrypted(
        const std::string& encrypted_base64,
        const std::string& password);

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
    std::string shielded_addr_;
    std::string indexer_url_;
    address::Network network_ = address::Network::PreProd;
    FacadeState state_;
    bool cleared_ = false;
    bool running_ = false;
    CoinSelectionStrategy coin_strategy_ = CoinSelectionStrategy::LargestFirst;

    // Services
    std::unique_ptr<SubmissionService> submission_service_;
    std::unique_ptr<ProvingService> proving_service_;
    std::unique_ptr<PendingTransactionsService> pending_tx_service_;

    mutable std::mutex state_mutex_;  // Protects state_ for thread-safe access
    mutable std::mutex pending_mutex_;  // Protects pending operations

    WalletFacade() = default;
    void ensure_not_cleared() const;
};

} // namespace midnight::wallet
