#pragma once

#include "midnight/wallet/wallet_facade.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <random>

namespace midnight::wallet {

using json = nlohmann::json;

// ─── Transaction Types ─────────────────────────────────────────

/**
 * @brief Balancing types matching the three transaction states
 *
 * FINALIZED:    Already on-chain, just needs balancing (creates separate balancing TX)
 * UNBOUND:      In mempool, unshielded changes happen in-place
 * UNPROVEN:     Not submitted yet, all changes merged into single TX
 */
enum class BalancingType {
    Finalized,  ///< Already on-chain, needs balancing transaction
    Unbound,    ///< In mempool, unshielded changes in-place
    Unproven     ///< Not submitted, all changes merged
};

// ─── Fee Constants ──────────────────────────────────────────────

namespace fee_constants {
    constexpr uint64_t BASE_TX_SIZE = 64;           ///< Base transaction overhead (bytes)
    constexpr uint64_t PER_INPUT_SIZE = 96;         ///< Each input requires signature (bytes)
    constexpr uint64_t PER_OUTPUT_SIZE = 80;        ///< Each output (bytes)
    constexpr uint64_t PER_PROOF_SIZE = 128;        ///< ZK proof overhead (bytes)
    constexpr uint64_t FEE_PER_BYTE = 1;             ///< Fee rate per byte (nano DUST)
    constexpr uint64_t MIN_DUST_OUTPUT = 1000;      ///< Minimum dust output (dust threshold)
}

// ─── UTXO Output (for change) ─────────────────────────────────

struct UtxoOutput {
    std::string receiver;
    uint64_t amount;
    std::string token_type = "NIGHT";
    bool is_change = true;
};

// ─── Balancing Recipe (simplified struct) ──────────────────────

struct SimpleBalancingRecipe {
    BalancingType type;
    std::vector<json> transactions;
    bool needs_proof = false;
    bool needs_signature = false;
    uint64_t total_fee = 0;

    bool success = false;
    std::string error;
};

// ─── UTXO Selection Result ─────────────────────────────────────

struct UtxoSelectionResult {
    std::vector<UtxoWithMeta> selected;
    uint64_t total_amount = 0;
    uint64_t excess = 0;  ///< Amount above target (for change)
};

// ─── Transaction Balancer ───────────────────────────────────────

/**
 * @brief Transaction Balancer for Midnight UTXO management
 *
 * Implements UTXO selection, fee estimation, and change management
 * for transaction balancing operations.
 *
 * Uses a Random-Improve coin selection strategy:
 * 1. Randomly shuffle available UTXOs
 * 2. Accumulate until target + fee is reached
 * 3. Improvement pass: try to reduce waste by replacing largest UTXO
 */
class TransactionBalancer {
public:
    /**
     * @brief Construct a TransactionBalancer
     * @param available_utxos Available UTXOs to select from
     * @param target_amount Amount we need to cover
     * @param token_type Token type for selection
     */
    TransactionBalancer(
        const std::vector<UtxoWithMeta>& available_utxos,
        uint64_t target_amount,
        const std::string& token_type = "NIGHT");

    // ─── Balance Finalized Transaction ─────────────────────────

    /**
     * @brief Balance a finalized transaction
     *
     * Creates a balancing transaction to cover inputs/outputs.
     * For finalized TXs, a separate balancing transaction is created.
     *
     * @param finalized_tx The finalized transaction to balance
     * @param kinds Token kinds to balance (unshielded, shielded, dust)
     * @param ttl Transaction time-to-live
     * @return SimpleBalancingRecipe with all transactions to submit
     */
    SimpleBalancingRecipe balance_finalized(
        const json& finalized_tx,
        TokenKindsToBalance kinds,
        std::chrono::system_clock::time_point ttl);

    // ─── Balance Unbound Transaction ───────────────────────────

    /**
     * @brief Balance an unbound (mempool) transaction
     *
     * For unbound TXs, unshielded balancing happens IN-PLACE.
     * Shielded and dust balancing creates separate transactions.
     *
     * @param unbound_tx The unbound transaction to balance
     * @param kinds Token kinds to balance
     * @param ttl Transaction time-to-live
     * @return SimpleBalancingRecipe
     */
    SimpleBalancingRecipe balance_unbound(
        const json& unbound_tx,
        TokenKindsToBalance kinds,
        std::chrono::system_clock::time_point ttl);

    // ─── Balance Unproven Transaction ─────────────────────────

    /**
     * @brief Balance an unproven transaction
     *
     * Merges all balancing into a single UnprovenTransaction.
     *
     * @param unproven_tx The unproven transaction to balance
     * @param kinds Token kinds to balance
     * @param ttl Transaction time-to-live
     * @return SimpleBalancingRecipe with single merged transaction
     */
    SimpleBalancingRecipe balance_unproven(
        const json& unproven_tx,
        TokenKindsToBalance kinds,
        std::chrono::system_clock::time_point ttl);

    // ─── UTXO Selection ──────────────────────────────────────

    /**
     * @brief Select UTXOs using Random-Improve strategy
     * @param amount Target amount to reach
     * @return UtxoSelectionResult with selected UTXOs and totals
     */
    UtxoSelectionResult select_utxos(uint64_t amount);

    // ─── Fee Calculation ──────────────────────────────────────

    /**
     * @brief Calculate transaction fee based on inputs/outputs
     * @param input_count Number of inputs
     * @param output_count Number of outputs
     * @param needs_proof Whether ZK proof is required
     * @return Estimated fee in nano DUST
     */
    static uint64_t calculate_fee(size_t input_count, size_t output_count, bool needs_proof = false);

    /**
     * @brief Estimate fee for a transaction JSON
     * @param tx Transaction JSON
     * @return Estimated fee in nano DUST
     */
    static uint64_t estimate_fee_from_tx(const json& tx);

    // ─── Change Management ────────────────────────────────────

    /**
     * @brief Create change output if amount exceeds target
     * @param change_amount Amount of change
     * @param min_dust Minimum dust threshold
     * @param receiver Address to send change to
     * @param token_type Token type for change
     * @return Optional UtxoOutput if change is valid (> min_dust)
     */
    static std::optional<UtxoOutput> create_change(
        uint64_t change_amount,
        uint64_t min_dust,
        const std::string& receiver,
        const std::string& token_type = "NIGHT");

    /**
     * @brief Split large change to avoid creating dust
     * @param change_amount Total change amount
     * @param min_dust Minimum dust threshold
     * @param receiver Change receiver address
     * @param token_type Token type
     * @return Vector of change outputs (may split into multiple)
     */
    static std::vector<UtxoOutput> split_change(
        uint64_t change_amount,
        uint64_t min_dust,
        const std::string& receiver,
        const std::string& token_type = "NIGHT");

    // ─── Transaction Merging ─────────────────────────────────

    /**
     * @brief Merge multiple transactions into one
     * @param txs Vector of transactions to merge
     * @return Merged transaction JSON
     */
    static json merge_transactions(const std::vector<json>& txs);

    /**
     * @brief Add inputs/outputs from one TX to another
     * @param target Target transaction to modify
     * @param source Source transaction to merge from
     * @return Modified target transaction
     */
    static json merge_into(json target, const json& source);

    // ─── Accessors ───────────────────────────────────────────

    const std::vector<UtxoWithMeta>& available() const { return available_; }
    uint64_t target() const { return target_; }
    const std::string& token_type() const { return token_type_; }

private:
    std::vector<UtxoWithMeta> available_;
    uint64_t target_;
    std::string token_type_;
    mutable std::mt19937 rng_;

    // Internal UTXO filtering
    std::vector<UtxoWithMeta> filter_by_token_type() const;

    // Random shuffle helper
    void shuffle_utxos(std::vector<UtxoWithMeta>& utxos) const;

    // Accumulation with random improve
    UtxoSelectionResult random_improve_select(uint64_t target, uint64_t fee_per_input, uint64_t fee_per_output);

    // Create balancing transaction JSON
    json create_balancing_transaction(
        const json& original_tx,
        const std::string& sender,
        uint64_t fee,
        const std::string& balancing_type,
        std::chrono::system_clock::time_point ttl);

    // Extract amounts from transaction
    static uint64_t extract_total_from_inputs(const json& tx);
    static uint64_t extract_total_from_outputs(const json& tx);
    static size_t count_inputs(const json& tx);
    static size_t count_outputs(const json& tx);

    // Hash transaction for ID
    static std::string hash_transaction(const json& tx);
};

// ─── Fee Calculator Helper ─────────────────────────────────────

/**
 * @brief Utility class for fee calculations
 */
class FeeCalculator {
public:
    static uint64_t calculate_tx_fee(
        size_t input_count,
        size_t output_count,
        bool needs_proof = false)
    {
        uint64_t fee = fee_constants::BASE_TX_SIZE;
        fee += input_count * fee_constants::PER_INPUT_SIZE;
        fee += output_count * fee_constants::PER_OUTPUT_SIZE;
        if (needs_proof) {
            fee += fee_constants::PER_PROOF_SIZE;
        }
        return fee * fee_constants::FEE_PER_BYTE;
    }

    static bool is_dust(uint64_t amount) {
        return amount < fee_constants::MIN_DUST_OUTPUT;
    }

    static uint64_t dust_threshold() {
        return fee_constants::MIN_DUST_OUTPUT;
    }
};

} // namespace midnight::wallet
