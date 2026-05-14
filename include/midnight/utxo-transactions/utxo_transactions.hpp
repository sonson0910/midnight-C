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
#include <utility>
#include "midnight/core/config.hpp"

namespace midnight::utxo_transactions
{

    /**
     * UTXO Input (with commitment-based amounts)
     */
    struct UtxoInput
    {
        std::string outpoint; // Previous TX hash + index
        std::string address;  // Sender address
        uint32_t output_index; // Output index in the previous transaction

        // Amount is ENCRYPTED - only commitment visible on-chain
        std::string amount_commitment; // Pedersen commitment hiding amount

        // Script proof (witness)
        std::string spending_witness; // Proves right to spend
    };

    /**
     * UTXO Output
     */
    struct UtxoOutput
    {
        std::string address; // Recipient address

        // Amount is ENCRYPTED
        std::string amount_commitment; // Commitment to output amount

        // Optional: can be locked to future block
        uint32_t lock_height = 0; // 0 = spendable immediately
    };

    /**
     * Typed witness container for signatures, proof references, and metadata.
     */
    struct WitnessBundle
    {
        std::vector<std::string> signatures;
        std::vector<std::string> proof_references;
        std::map<std::string, std::string> metadata;
    };

    /**
     * Transaction Structure
     */
    struct Transaction
    {
        std::string hash; // blake2_256 hash
        uint32_t version = 1;

        // UTXO inputs
        std::vector<UtxoInput> inputs;

        // UTXO outputs
        std::vector<UtxoOutput> outputs;

        // DUST accounting (resource management)
        uint64_t input_dust = 0;  // Total DUST from inputs
        uint64_t output_dust = 0; // Total DUST to outputs
        uint64_t fees = 0;        // Fee (input_dust - output_dust - output_amount)

        // Proof of correctness (Midnight PLONK proof bytes encoded as hex)
        std::vector<std::string> proofs;

        // Signature
        std::string signature; // sr25519 signature

        // Typed witness data (preferred over legacy flat fields)
        WitnessBundle witness_bundle;

        // Public metadata (not encrypted)
        uint64_t nonce = 0;
        uint64_t timestamp = 0;
    };

    /**
     * UTXO Set Entry (matches Midnight SDK schema)
     */
    struct Utxo
    {
        // Transaction and output identifiers
        std::string tx_hash;   // Transaction creating this UTXO
        uint32_t output_index = 0; // Which output in transaction

        // Output data (matches SDK unshieldedCreatedOutputs)
        std::string address;           // Owner/recipient (UnshieldedAddress Bech32m)
        std::string token_type;        // Hex-encoded token type (default: NIGHT = 0x00)
        std::string value;             // String representation of amount (supports BigInt up to 16 bytes)
        std::string intent_hash;       // Hex-encoded intent hash
        uint64_t ctime = 0;           // Creation timestamp
        bool registered_for_dust = false; // Whether registered for dust generation

        // Commitment data (on-chain visible)
        std::string amount_commitment; // Encrypted amount (Pedersen commitment)

        // Confirmation status
        uint32_t confirmed_height = 0; // Block height where confirmed
        uint32_t finality_depth = 0;   // GRANDPA finality depth

        // Spent status
        bool is_spent = false;
        std::string spending_tx_hash; // If spent, the spending transaction hash
    };

    /**
     * Account state on Midnight (balance tracking)
     */
    struct AccountBalance
    {
        std::string address;

        // UTXOs controlled by this address
        std::vector<Utxo> unspent_utxos;

        // Local private data (not on-chain)
        std::map<std::string, std::string> private_amounts; // UTXO hash -> amount (local only!)
    };

    /**
     * Fee model knobs used by transaction building and UTXO selection heuristics.
     */
    struct FeeModelConfig
    {
        bool use_protocol_min_fee = true;

        uint32_t base_tx_size_bytes = 64;
        uint32_t bytes_per_input = 96;
        uint32_t bytes_per_output = 80;
        uint32_t bytes_per_proof = 128;

        uint64_t base_dust_per_output = 1000;
        uint64_t witness_dust_per_output = 500;
        uint64_t commitment_bytes_per_output = 128;

        // 0 means "derive from protocol params".
        uint64_t estimated_dust_per_utxo = 0;
        size_t default_proof_size = 128;
    };

    /**
     * Typed context for transaction building and fee estimation.
     */
    struct TransactionBuilderContext
    {
        midnight::ProtocolParams protocol_params{};
        uint64_t chain_tip_height = 0;
        std::string chain_tip_hash;
        FeeModelConfig fee_model{};
    };

    /**
     * Indexer data provider abstraction (GraphQL-oriented).
     */
    class IIndexerProvider
    {
    public:
        virtual ~IIndexerProvider() = default;
        virtual Json::Value execute_query(const std::string &query) = 0;
        virtual Json::Value execute_query_with_vars(const std::string &query,
                                                   const std::map<std::string, Json::Value> &variables) = 0;
    };

    /**
     * Node RPC provider abstraction.
     */
    class INodeProvider
    {
    public:
        virtual ~INodeProvider() = default;
        virtual std::optional<midnight::ProtocolParams> get_protocol_params() = 0;
        virtual std::optional<std::pair<uint64_t, std::string>> get_chain_tip() = 0;
    };

    /**
     * Default GraphQL indexer provider.
     */
    class GraphqlIndexerProvider : public IIndexerProvider
    {
    public:
        explicit GraphqlIndexerProvider(const std::string &indexer_graphql_url,
                                        uint32_t timeout_ms = 8000);

        Json::Value execute_query(const std::string &query) override;
        Json::Value execute_query_with_vars(const std::string &query,
                                            const std::map<std::string, Json::Value> &variables) override;

    private:
        std::string indexer_url_;
        uint32_t timeout_ms_;
    };

    /**
     * Default Midnight node RPC provider.
     */
    class MidnightNodeProvider : public INodeProvider
    {
    public:
        explicit MidnightNodeProvider(const std::string &node_rpc_url,
                                      uint32_t timeout_ms = 5000);

        std::optional<midnight::ProtocolParams> get_protocol_params() override;
        std::optional<std::pair<uint64_t, std::string>> get_chain_tip() override;

    private:
        std::string node_rpc_url_;
        uint32_t timeout_ms_;
    };

    /**
     * UTXO Set Manager
     * Queries and manages UTXOs from the blockchain
     */
    class UtxoSetManager
    {
    public:
        /**
         * Constructor
         * @param indexer_graphql_url: Midnight Indexer endpoint
         * @param node_rpc_url: Midnight Node RPC endpoint
         */
        UtxoSetManager(const std::string &indexer_graphql_url,
                       const std::string &node_rpc_url);

        /**
         * Constructor with explicit provider interfaces.
         */
        UtxoSetManager(std::shared_ptr<IIndexerProvider> indexer_provider,
                       std::shared_ptr<INodeProvider> node_provider);

        /**
         * Query UTXOs for an address.
         *
         * This compatibility method uses IndexerClient's guarded fast path and
         * will not perform an unbounded historical scan unless
         * MIDNIGHT_ENABLE_FULL_UTXO_SCAN=1 is set. Prefer the bounded overload
         * when the relevant block range is known.
         */
        std::optional<std::vector<Utxo>> query_unspent_utxos(const std::string &address);

        /**
         * Query unspent UTXOs in a bounded block range.
         */
        std::optional<std::vector<Utxo>> query_unspent_utxos(
            const std::string &address,
            uint32_t from_block,
            uint32_t to_block);

        /**
         * Query total spendable balance
         * Note: Amounts are commitments (encrypted), so only count is accurate
         */
        uint32_t count_unspent_utxos(const std::string &address);

        /**
         * Query specific UTXO by outpoint
         */
        std::optional<Utxo> query_utxo(const std::string &tx_hash, uint32_t output_index);

        /**
         * Check if UTXO is spent
         * @return std::nullopt if UTXO not found, true if spent, false if unspent
         */
        std::optional<bool> is_spent(const std::string &tx_hash, uint32_t output_index);

        /**
         * Synchronize local account state with on-chain
         */
        std::optional<AccountBalance> sync_account(const std::string &address);

        /**
         * Build a typed transaction context from node provider data.
         */
        std::optional<TransactionBuilderContext> create_builder_context() const;

        /**
         * Verify UTXOs exist and are unspent
         * @param inputs List of inputs to verify
         * @return true if all inputs refer to valid unspent UTXOs
         */
        bool verify_inputs_unspent(const std::vector<UtxoInput> &inputs);

        /**
         * Reserve UTXOs for a pending transaction
         * @param inputs UTXOs to reserve
         * @param tx_hash Transaction hash (used as reservation ID)
         * @param ttl_ms Time-to-live in milliseconds
         * @return true if all UTXOs were successfully reserved
         */
        bool reserve_utxos(const std::vector<UtxoInput> &inputs,
                          const std::string &tx_hash,
                          uint64_t ttl_ms);

        /**
         * Release reserved UTXOs (on transaction failure/cancellation)
         * @param tx_hash Transaction hash (reservation ID)
         */
        void release_reserved_utxos(const std::string &tx_hash);

        /**
         * Check if a specific UTXO outpoint is currently reserved
         * @param outpoint The outpoint key (tx_hash:output_index)
         * @param exclude_tx_hash Optional tx hash to exclude from check (for self-check)
         * @return true if UTXO is reserved by any transaction
         */
        bool is_reserved(const std::string &outpoint, const std::string &exclude_tx_hash = "") const;

        /**
         * Get current time in milliseconds (for TTL calculations)
         */
        static uint64_t get_current_time_ms();

    private:
        std::string indexer_url_;
        std::string rpc_url_;
        std::shared_ptr<IIndexerProvider> indexer_provider_;
        std::shared_ptr<INodeProvider> node_provider_;

        // UTXO Reservation state (thread-safe in production)
        mutable std::map<std::string, std::pair<std::string, uint64_t>> reserved_utxos_; // outpoint -> (tx_hash, expiry_ms)

        /**
         * Clean up expired reservations
         */
        void cleanup_expired_reservations();
    };

    /**
     * Local UTXO draft helper.
     *
     * This class only tracks local inputs, outputs, witnesses, and fee
     * estimates. Production Midnight transaction bytes must be produced by the
     * ledger serializer before submission.
     */
    class TransactionBuilder
    {
    public:
        /**
         * Constructor
         * Creates blank transaction
         */
        TransactionBuilder();

        /**
         * Constructor with typed context (protocol params + chain tip + fee model)
         */
        explicit TransactionBuilder(const TransactionBuilderContext &context);

        /**
         * Constructor with typed context and UTXO manager (for duplicate detection)
         */
        TransactionBuilder(const TransactionBuilderContext &context,
                         std::shared_ptr<UtxoSetManager> utxo_set_manager);

        /**
         * Replace current builder context
         */
        void set_context(const TransactionBuilderContext &context);

        /**
         * Access current builder context
         */
        const TransactionBuilderContext &get_context() const;

        /**
         * Add input to transaction
         * @throws std::invalid_argument if input is duplicate
         */
        void add_input(const UtxoInput &input);

        /**
         * Add output to transaction
         */
        void add_output(const UtxoOutput &output);

        /**
         * Set full typed witness bundle.
         */
        void set_witness_bundle(const WitnessBundle &witness_bundle);

        /**
         * Append a signature to witness bundle.
         */
        void add_signature(const std::string &signature);

        /**
         * Append a proof reference to witness bundle.
         */
        void add_proof_reference(const std::string &proof_reference);

        /**
         * Set witness metadata key/value.
         */
        void set_witness_metadata(const std::string &key, const std::string &value);

        /**
         * Set fees (in DUST)
         */
        void set_fees(uint64_t fee_amount);

        /**
         * Validate transaction structure
         */
        bool validate();

        /**
         * Get current transaction state
         */
        Transaction get_working_transaction() const;

        /**
         * Check if an input is a duplicate of an existing input
         * @param input The input to check
         * @return true if duplicate exists
         */
        bool has_duplicate_input(const UtxoInput &input) const;

    private:
        Transaction tx_;
        TransactionBuilderContext context_;
        bool fees_manually_set_ = false;
        std::shared_ptr<UtxoSetManager> utxo_set_manager_;

        /**
         * Compute blake2_256 hash of transaction
         */
        std::string compute_hash();

        /**
         * Verify input against UTXO set (existence + unspent status)
         * @return true if verified, false if verification failed
         */
        bool verify_input_utxo(const UtxoInput &input);
    };

    /**
     * DUST Accounting
     * Manages resource tokens (continuous generation)
     */
    class DustAccounting
    {
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
         * Calculate DUST requirement using typed context.
         */
        static uint64_t calculate_dust_fee(
            uint64_t input_dust,
            uint64_t output_dust,
            size_t output_count,
            bool has_witnesses,
            const TransactionBuilderContext &context);

        /**
         * Check if transaction is valid per DUST accounting
         */
        static bool verify_dust_balance(const Transaction &tx);

        /**
         * Get current DUST generation rate per block
         */
        static double get_dust_generation_rate();

    private:
        static constexpr double DUST_PER_NIGHT = 100.0;  // Per NIGHT token staked
        static constexpr uint64_t DUST_PER_BLOCK = 1000; // Base generation
    };

    /**
     * Transaction Validator
     * Validates transactions before submission
     */
    class TransactionValidator
    {
    public:
        /**
         * Default constructor with no UTXO set manager (limited validation)
         */
        TransactionValidator() = default;

        /**
         * Constructor with UTXO set manager for full validation
         */
        explicit TransactionValidator(std::shared_ptr<UtxoSetManager> utxo_set_manager);

        /**
         * Validate complete transaction
         */
        bool validate_transaction(const Transaction &tx);

        /**
         * Validate inputs
         * - All outpoints exist and unspent
         * - Signatures correct
         * - Witnesses valid
         */
        bool validate_inputs(const Transaction &tx);

        /**
         * Validate outputs
         * - All commitments well-formed
         * - At least 1 output
         */
        bool validate_outputs(const Transaction &tx);

        /**
         * Validate DUST accounting
         */
        bool validate_dust_accounting(const Transaction &tx);

        /**
         * Validate proofs (zk-SNARKs)
         */
        bool validate_proofs(const Transaction &tx);

        /**
         * Validate signature
         */
        bool validate_signature(const Transaction &tx);

        /**
         * Get the last validation error message
         */
        std::string get_last_error() const;

    private:
        std::shared_ptr<UtxoSetManager> utxo_set_manager_;
        std::string last_error_;

        static constexpr size_t MAX_TX_SIZE = 32 * 1024; // 32 KB
        static constexpr uint32_t MAX_INPUTS = 256;
        static constexpr uint32_t MAX_OUTPUTS = 256;
    };

    /**
     * UTXO Selection
     * Selects UTXOs for spending (coin selection strategy)
     */
    class UtxoSelector
    {
    public:
        /**
         * Select UTXOs for spending
         * Strategy: Smallest sufficient (minimize remaining UTXOs)
         */
        static std::optional<std::vector<Utxo>> select_utxos(
            const std::vector<Utxo> &available,
            uint32_t output_count,
            bool needs_witnesses);

        /**
         * Select UTXOs with explicit fee context.
         */
        static std::optional<std::vector<Utxo>> select_utxos(
            const std::vector<Utxo> &available,
            uint32_t output_count,
            bool needs_witnesses,
            const TransactionBuilderContext &context);

        /**
         * Calculate total DUST available in UTXO set
         * Note: We can only estimate since amounts are encrypted
         */
        static uint64_t estimate_dust_available(const std::vector<Utxo> &utxos);

        /**
         * Estimate DUST with typed context.
         */
        static uint64_t estimate_dust_available(
            const std::vector<Utxo> &utxos,
            const TransactionBuilderContext &context);
    };

    /**
     * Transaction Fee Estimator
     */
    class FeeEstimator
    {
    public:
        /**
         * Estimate fee for transaction
         * Based on: transaction size, proof count, DUST required
         */
        static uint64_t estimate_fee(uint32_t input_count,
                                     uint32_t output_count,
                                     size_t proof_size);

        /**
         * Estimate fee with protocol-aware typed context.
         */
        static uint64_t estimate_fee(uint32_t input_count,
                                     uint32_t output_count,
                                     size_t proof_size,
                                     const TransactionBuilderContext &context);

        /**
         * Estimate serialized transaction size in bytes.
         */
        static uint32_t estimate_tx_size_bytes(uint32_t input_count,
                                               uint32_t output_count,
                                               size_t proof_size,
                                               const TransactionBuilderContext &context);

        /**
         * Get current fee rate (DUST per byte)
         */
        static double get_current_fee_rate();

        /**
         * Get current fee rate from context.
         */
        static double get_current_fee_rate(const TransactionBuilderContext &context);
    };

} // namespace midnight::utxo_transactions
