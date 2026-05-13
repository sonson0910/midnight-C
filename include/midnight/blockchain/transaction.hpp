#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace midnight::blockchain
{

    /**
     * @brief Legacy local transaction model
     *
     * This class is kept for local inspection/debug data. It does not implement
     * the production midnight-ledger Transaction serializer.
     */
    class Transaction
    {
    public:
        /**
         * @brief UTXO Input with commitment-based amounts
         *
         * In Midnight, the amount is NOT revealed - only a Pedersen commitment.
         * The spending witness proves ownership without revealing the amount.
         */
        struct Input
        {
            std::string outpoint;              // Previous TX hash + output_index
            std::string address;               // Sender unshielded address
            std::string amount_commitment;     // Pedersen commitment (encrypted amount)
            std::string spending_witness;      // ZK proof for spending right
        };

        /**
         * @brief UTXO Output with commitment
         */
        struct Output
        {
            std::string address;               // Recipient unshielded address
            std::string amount_commitment;     // Pedersen commitment
            std::optional<std::string> ciphertext; // Optional encrypted note for shielded transfer
            uint32_t lock_height = 0;         // 0 = spendable immediately
        };

        /**
         * @brief Fee breakdown
         */
        struct Fee
        {
            uint64_t paid_fees = 0;            // Actual fees paid
            uint64_t estimated_fees = 0;        // Estimated fees
        };

        /**
         * @brief Transaction result from the network
         */
        struct Result
        {
            std::string status;                // "success" or "failed"
            std::vector<std::string> segment_ids;
            std::vector<bool> segment_success;
        };

        /**
         * @brief Constructor with transaction hash
         */
        explicit Transaction(const std::string &tx_hash = "");

        /**
         * @brief Add UTXO input to transaction
         */
        void add_input(const Input &input);

        /**
         * @brief Add UTXO output to transaction
         */
        void add_output(const Output &output);

        /**
         * @brief Add ZK proof for shielded transaction
         */
        void add_proof(const std::string &zk_proof);

        /**
         * @brief Set transaction version
         */
        void set_version(uint32_t version) { version_ = version; }

        /**
         * @brief Set validity interval (block numbers)
         */
        void set_validity(uint64_t invalid_hereafter, uint64_t invalid_before = 0);

        /**
         * @brief Set fee information
         */
        void set_fee(const Fee &fee) { fee_ = fee; }

        /**
         * @brief Set nonce
         */
        void set_nonce(uint64_t nonce) { nonce_ = nonce; }

        /**
         * @brief Get nonce
         */
        uint64_t get_nonce() const { return nonce_; }

        /**
         * @brief Disabled for production transaction ids.
         */
        std::string calculate_hash() const;

        /**
         * @brief Get transaction hash
         */
        const std::string &get_hash() const { return tx_hash_; }

        /**
         * @brief Get all inputs
         */
        const std::vector<Input> &get_inputs() const { return inputs_; }

        /**
         * @brief Get all outputs
         */
        const std::vector<Output> &get_outputs() const { return outputs_; }

        /**
         * @brief Get all ZK proofs
         */
        const std::vector<std::string> &get_proofs() const { return proofs_; }

        /**
         * @brief Get fee information
         */
        const Fee &get_fee() const { return fee_; }

        /**
         * @brief Get total input amount (from commitments)
         */
        uint64_t get_total_inputs() const;

        /**
         * @brief Get total output amount (from commitments)
         */
        uint64_t get_total_outputs() const;

        /**
         * @brief Get total fees
         */
        uint64_t get_total_fees() const { return fee_.paid_fees; }

        /**
         * @brief Get transaction version
         */
        uint32_t get_version() const { return version_; }

        /**
         * @brief Disabled; production bytes must come from midnight-ledger.
         */
        std::vector<uint8_t> to_cbor_bytes() const;

        /**
         * @brief Disabled; production bytes must come from midnight-ledger.
         */
        std::string to_hex() const;

        /**
         * @brief Legacy parser for old local payloads only.
         */
        static Transaction from_cbor_hex(const std::string &cbor_hex);

        /**
         * @brief Serialize to JSON for debugging/logging
         */
        std::string to_json() const;

        /**
         * @brief Get transaction size in bytes
         */
        size_t get_size() const;

        /**
         * @brief Check if transaction has ZK proofs
         */
        bool has_proofs() const { return !proofs_.empty(); }

        /**
         * @brief Check if transaction is shielded (has encrypted outputs)
         */
        bool is_shielded() const;

    private:
        std::string tx_hash_;
        uint32_t version_ = 1;
        std::vector<Input> inputs_;
        std::vector<Output> outputs_;
        std::vector<std::string> proofs_;     // ZK-SNARK proofs
        Fee fee_;
        uint64_t invalid_hereafter_ = 0;
        uint64_t invalid_before_ = 0;
        uint64_t nonce_ = 0;
        uint64_t timestamp_ = 0;
    };

    /**
     * @brief Calculate minimum transaction fee
     *
     * Formula: fee = MIN_FEE_A * tx_size + MIN_FEE_B
     *
     * @param tx_size Transaction size in bytes
     * @return Minimum fee in lovelace
     */
    uint64_t calculate_min_fee(size_t tx_size);

} // namespace midnight::blockchain
