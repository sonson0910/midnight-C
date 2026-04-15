/**
 * Phase 3: UTXO Transactions
 * Handles UTXO model on Midnight with commitments (dual-state)
 * 
 * Key differences from Cardano:
 * - Amounts are ENCRYPTED (commitments only)
 * - Commitments prove amount without revealing
 * - DUST is continuous resource (not just blocks)
 * - Public + private state (selective disclosure)
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <json/json.h>
#include <cstdint>

namespace midnight::phase3 {

enum class DataSourceMode {
    MOCK,
    REAL_RPC,
};

/**
 * UTXO Input (with commitment-based amounts)
 */
struct UtxoInput {
    std::string outpoint;              // Previous TX hash + index
    std::string address;               // Sender address
    
    // Amount is ENCRYPTED - only commitment visible on-chain
    std::string amount_commitment;     // Pedersen commitment hiding amount
    
    // Script proof (witness)
    std::string spending_witness;      // Proves right to spend
};

/**
 * UTXO Output
 */
struct UtxoOutput {
    std::string address;               // Recipient address
    
    // Amount is ENCRYPTED
    std::string amount_commitment;     // Commitment to output amount
    
    // Optional: can be locked to future block
    uint32_t lock_height = 0;         // 0 = spendable immediately
};

/**
 * Transaction Structure
 */
struct Transaction {
    std::string hash;                  // blake2_256 hash
    uint32_t version = 1;
    
    // UTXO inputs
    std::vector<UtxoInput> inputs;
    
    // UTXO outputs
    std::vector<UtxoOutput> outputs;
    
    // DUST accounting (resource management)
    uint64_t input_dust = 0;          // Total DUST from inputs
    uint64_t output_dust = 0;         // Total DUST to outputs
    uint64_t fees = 0;                // Fee (input_dust - output_dust - output_amount)
    
    // Proof of correctness (zk-SNARK)
    std::vector<std::string> proofs;   // 128-byte zk-SNARKs
    
    // Signature
    std::string signature;             // sr25519 signature
    
    // Public metadata (not encrypted)
    uint64_t nonce = 0;
    uint64_t timestamp = 0;
};

/**
 * UTXO Set Entry
 */
struct Utxo {
    std::string tx_hash;              // Transaction creating this UTXO
    uint32_t output_index;            // Which output in transaction
    
    std::string address;              // Recipient
    std::string amount_commitment;    // Encrypted amount
    
    uint32_t confirmed_height = 0;    // Block height where confirmed
    uint32_t finality_depth = 0;      // GRANDPA finality depth
    
    bool is_spent = false;
    std::string spending_tx_hash;     // If spent
};

/**
 * Account state on Midnight (balance tracking)
 */
struct AccountBalance {
    std::string address;
    
    // UTXOs controlled by this address
    std::vector<Utxo> unspent_utxos;
    
    // Local private data (not on-chain)
    std::map<std::string, std::string> private_amounts; // UTXO hash -> amount (local only!)
};

/**
 * UTXO Set Manager
 * Queries and manages UTXOs from the blockchain
 */
class UtxoSetManager {
public:
    /**
     * Constructor
     * @param indexer_graphql_url: Midnight Indexer endpoint
     * @param node_rpc_url: Midnight Node RPC endpoint
     */
    UtxoSetManager(const std::string& indexer_graphql_url,
                   const std::string& node_rpc_url,
                   DataSourceMode mode = DataSourceMode::MOCK);

    void set_data_source_mode(DataSourceMode mode);
    DataSourceMode get_data_source_mode() const;
    
    /**
     * Query UTXOs for an address
     * Returns unspent (available) UTXOs only
     */
    std::optional<std::vector<Utxo>> query_unspent_utxos(const std::string& address);
    
    /**
     * Query total spendable balance
     * Note: Amounts are commitments (encrypted), so only count is accurate
     */
    uint32_t count_unspent_utxos(const std::string& address);
    
    /**
     * Query specific UTXO by outpoint
     */
    std::optional<Utxo> query_utxo(const std::string& tx_hash, uint32_t output_index);
    
    /**
     * Check if UTXO is spent
     */
    bool is_spent(const std::string& tx_hash, uint32_t output_index);
    
    /**
     * Synchronize local account state with on-chain
     */
    std::optional<AccountBalance> sync_account(const std::string& address);
    
private:
    std::string indexer_url_;
    std::string rpc_url_;
    DataSourceMode data_source_mode_;
    
    /**
     * GraphQL query
     */
    Json::Value graphql_query(const std::string& query);
};

/**
 * Transaction Builder
 * Creates well-formed transactions for submission
 */
class TransactionBuilder {
public:
    /**
     * Constructor
     * Creates blank transaction
     */
    TransactionBuilder();
    
    /**
     * Add input to transaction
     */
    void add_input(const UtxoInput& input);
    
    /**
     * Add output to transaction
     */
    void add_output(const UtxoOutput& output);
    
    /**
     * Set fees (in DUST)
     */
    void set_fees(uint64_t fee_amount);
    
    /**
     * Finalize transaction (compute hash)
     */
    std::optional<Transaction> build();
    
    /**
     * Validate transaction structure
     */
    bool validate();
    
    /**
     * Get current transaction state
     */
    Transaction get_working_transaction() const;
    
private:
    Transaction tx_;
    
    /**
     * Compute blake2_256 hash of transaction
     */
    std::string compute_hash();
};

/**
 * DUST Accounting
 * Manages resource tokens (continuous generation)
 */
class DustAccounting {
public:
    /**
     * Calculate DUST requirement for transaction
     * DUST = input_dust - output_dust - output_commitment_size
     */
    static uint64_t calculate_dust_fee(
        uint64_t input_dust,
        uint64_t output_dust,
        size_t output_count,
        bool has_witnesses);
    
    /**
     * Check if transaction is valid per DUST accounting
     */
    static bool verify_dust_balance(const Transaction& tx);
    
    /**
     * Get current DUST generation rate per block
     */
    static double get_dust_generation_rate();
    
private:
    static constexpr double DUST_PER_NIGHT = 100.0; // Per NIGHT token staked
    static constexpr uint64_t DUST_PER_BLOCK = 1000; // Base generation
};

/**
 * Transaction Validator
 * Validates transactions before submission
 */
class TransactionValidator {
public:
    /**
     * Validate complete transaction
     */
    static bool validate_transaction(const Transaction& tx);
    
    /**
     * Validate inputs
     * - All outpoints exist and unspent
     * - Signatures correct
     * - Witnesses valid
     */
    static bool validate_inputs(const Transaction& tx);
    
    /**
     * Validate outputs
     * - All commitments well-formed
     * - At least 1 output
     */
    static bool validate_outputs(const Transaction& tx);
    
    /**
     * Validate DUST accounting
     */
    static bool validate_dust_accounting(const Transaction& tx);
    
    /**
     * Validate proofs (zk-SNARKs)
     */
    static bool validate_proofs(const Transaction& tx);
    
    /**
     * Validate signature
     */
    static bool validate_signature(const Transaction& tx);
    
private:
    static constexpr size_t MAX_TX_SIZE = 32 * 1024; // 32 KB
    static constexpr uint32_t MAX_INPUTS = 256;
    static constexpr uint32_t MAX_OUTPUTS = 256;
};

/**
 * UTXO Selection
 * Selects UTXOs for spending (coin selection strategy)
 */
class UtxoSelector {
public:
    /**
     * Select UTXOs for spending
     * Strategy: Smallest sufficient (minimize remaining UTXOs)
     */
    static std::optional<std::vector<Utxo>> select_utxos(
        const std::vector<Utxo>& available,
        uint32_t output_count,
        bool needs_witnesses);
    
    /**
     * Calculate total DUST available in UTXO set
     * Note: We can only estimate since amounts are encrypted
     */
    static uint64_t estimate_dust_available(const std::vector<Utxo>& utxos);
};

/**
 * Transaction Fee Estimator
 */
class FeeEstimator {
public:
    /**
     * Estimate fee for transaction
     * Based on: transaction size, proof count, DUST required
     */
    static uint64_t estimate_fee(uint32_t input_count, 
                                uint32_t output_count,
                                size_t proof_size);
    
    /**
     * Get current fee rate (DUST per byte)
     */
    static double get_current_fee_rate();
};

} // namespace midnight::phase3
