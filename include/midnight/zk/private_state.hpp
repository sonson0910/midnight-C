#pragma once

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "midnight/zk/proof_types.hpp"

namespace midnight::zk
{

    using json = nlohmann::json;

    /**
     * @brief Secure storage for private witness keys
     *
     * Witnesses like localSecretKey() need access to user's private data.
     * This store manages secure storage and retrieval of secrets that should
     * never be revealed on-chain.
     *
     * Example:
     * ```cpp
     * SecretKeyStore store;
     * store.store_secret("localSecretKey", secret_key_32_bytes);
     * auto retrieved = store.retrieve_secret("localSecretKey");
     * ```
     */
    class SecretKeyStore
    {
    public:
        /**
         * @brief Store a secret key
         *
         * @param name Secret identifier (e.g., "localSecretKey")
         * @param data Secret bytes (typically 32 bytes for Ed25519)
         * @return true if stored successfully
         */
        bool store_secret(
            const std::string &name,
            const std::vector<uint8_t> &data);

        /**
         * @brief Retrieve a stored secret
         *
         * @param name Secret identifier
         * @return Secret bytes
         * @throws std::runtime_error if secret not found
         */
        std::vector<uint8_t> retrieve_secret(const std::string &name) const;

        /**
         * @brief Check if secret exists
         *
         * @param name Secret identifier
         * @return true if secret is stored
         */
        bool has_secret(const std::string &name) const;

        /**
         * @brief Remove a stored secret
         *
         * @param name Secret identifier
         * @return true if removed, false if not found
         */
        bool remove_secret(const std::string &name);

        /**
         * @brief Clear all secrets
         *
         * Useful for cleanup or testing.
         */
        void clear();

        /**
         * @brief Get count of stored secrets
         *
         * @return Number of secrets in storage
         */
        size_t size() const;

        /**
         * @brief List all secret names
         *
         * @return Vector of secret identifiers
         */
        std::vector<std::string> list_secrets() const;

    private:
        // Map of secret_name -> secret_bytes
        std::map<std::string, std::vector<uint8_t>> secrets_;
    };

    /**
     * @brief Manages private state on ledger
     *
     * Distinguishes between public (on-chain) and private (off-chain) state.
     *
     * Public Ledger State (visible to everyone):
     * - Commitments, state enums, counters
     * - Example: owner = sha256(secret_key)
     *
     * Private State (stored locally, never on-chain):
     * - Secret keys, private inputs
     * - Example: secret_key = [0xAA, 0xBB, ...]
     *
     * The PrivateStateManager keeps both synchronized.
     */
    class PrivateStateManager
    {
    public:
        /**
         * @brief Get private state for a contract
         *
         * @param contract_address Midnight smart contract address
         * @return Private state (off-chain fields)
         */
        LedgerState get_private_state(const std::string &contract_address) const;

        /**
         * @brief Update private state
         *
         * After circuit execution, private state may need update.
         *
         * @param contract_address Contract address
         * @param state New private state
         */
        void update_private_state(
            const std::string &contract_address,
            const LedgerState &state);

        /**
         * @brief Get public state from blockchain
         *
         * Fetches current on-chain state for a contract.
         *
         * @param contract_address Contract address
         * @param rpc_endpoint Midnight RPC endpoint
         * @return Public ledger state
         */
        LedgerState get_public_state(
            const std::string &contract_address,
            const std::string &rpc_endpoint);

        /**
         * @brief Synchronize private state with public state
         *
         * After a transaction confirms on-chain, update local private state
         * to reflect the changes.
         *
         * @param contract_address Contract address
         * @param public_state New public state from blockchain
         */
        void sync_state(
            const std::string &contract_address,
            const LedgerState &public_state);

        /**
         * @brief Check if contract state is known
         *
         * @param contract_address Contract address
         * @return true if we have state for this contract
         */
        bool has_state(const std::string &contract_address) const;

        /**
         * @brief Clear all private state
         *
         * Useful for cleanup or resetting.
         */
        void clear();

        /**
         * @brief List all contracts with stored state
         *
         * @return Vector of contract addresses
         */
        std::vector<std::string> list_contracts() const;

    private:
        // Map of contract_address -> private_state
        std::map<std::string, LedgerState> private_states_;
    };

    /**
     * @brief Input data to witness function execution
     *
     * Witnesses receive a context with access to:
     * - Public ledger state (for reading current values)
     * - Private state (secrets, private inputs)
     * - Contract address (identity context)
     *
     * Witness context is passed to TypeScript witness functions.
     */
    struct WitnessContext
    {
        // Public state visible on blockchain
        LedgerState public_ledger_state;

        // Private secrets and witness function outputs
        std::map<std::string, std::vector<uint8_t>> private_data;

        // Smart contract address
        std::string contract_address;

        // Block height for context (prevents replay)
        uint64_t block_height;

        // Serialization for TypeScript bridge
        json to_json() const;
        static WitnessContext from_json(const json &j);
    };

    /**
     * @brief Builder for witness execution contexts
     *
     * Prepares witness functions for execution by:
     * 1. Loading public state from blockchain
     * 2. Loading private secrets from storage
     * 3. Building execution context
     * 4. Passing to TypeScript for witness function execution
     *
     * Example:
     * ```cpp
     * WitnessContextBuilder builder;
     * builder.set_contract_address("contract_addr");
     * builder.add_private_secret("localSecretKey", secret_bytes);
     *
     * WitnessContext ctx = builder.build();
     * // Pass ctx to TypeScript witness function
     * // TypeScript returns witness outputs
     * ```
     */
    class WitnessContextBuilder
    {
    public:
        /**
         * @brief Set contract address for context
         *
         * @param address Midnight contract address
         * @return this for chaining
         */
        WitnessContextBuilder &set_contract_address(const std::string &address);

        /**
         * @brief Set public ledger state
         *
         * @param state Current on-chain state
         * @return this for chaining
         */
        WitnessContextBuilder &set_public_state(const LedgerState &state);

        /**
         * @brief Add private secret to context
         *
         * @param name Secret identifier (e.g., "localSecretKey")
         * @param data Secret bytes
         * @return this for chaining
         */
        WitnessContextBuilder &add_private_secret(
            const std::string &name,
            const std::vector<uint8_t> &data);

        /**
         * @brief Add multiple secrets at once
         *
         * @param secrets Map of name -> secret_bytes
         * @return this for chaining
         */
        WitnessContextBuilder &add_private_secrets(
            const std::map<std::string, std::vector<uint8_t>> &secrets);

        /**
         * @brief Set block height (for replay prevention)
         *
         * @param height Current block height
         * @return this for chaining
         */
        WitnessContextBuilder &set_block_height(uint64_t height);

        /**
         * @brief Build the witness context
         *
         * Validates that all required fields are set before building.
         *
         * @return Constructed WitnessContext
         * @throws std::runtime_error if required fields missing
         */
        WitnessContext build() const;

        /**
         * @brief Build context from managers
         *
         * Convenience method that loads state from managers:
         * - Public state from PrivateStateManager
         * - Private secrets from SecretKeyStore
         *
         * @param contract_address Contract to load state for
         * @param state_mgr PrivateStateManager with ledger state
         * @param secret_store SecretKeyStore with private data
         * @param required_secrets List of secret names to load
         * @return Constructed WitnessContext
         */
        static WitnessContext build_from_managers(
            const std::string &contract_address,
            const PrivateStateManager &state_mgr,
            const SecretKeyStore &secret_store,
            const std::vector<std::string> &required_secrets);

    private:
        std::string contract_address_;
        LedgerState public_state_;
        std::map<std::string, std::vector<uint8_t>> private_data_;
        uint64_t block_height_ = 0;
        bool contract_set_ = false;
        bool public_state_set_ = false;

        void validate_for_build() const;
    };

    /**
     * @brief Witness execution result
     *
     * After witness function executes, returns outputs for proof generation.
     */
    struct WitnessExecutionResult
    {
        enum class Status
        {
            SUCCESS,
            VALIDATION_FAILED,
            EXECUTION_ERROR,
            TIMEOUT
        };

        Status status = Status::SUCCESS;
        std::map<std::string, WitnessOutput> witness_outputs;
        std::string error_message;
        uint64_t execution_time_ms = 0;

        bool is_success() const { return status == Status::SUCCESS; }
        json to_json() const;
    };

    /**
     * @brief Helper functions for private state management
     */
    namespace private_state_util
    {
        /**
         * @brief Create empty witness context for testing
         *
         * @param contract_address Contract address
         * @return Test WitnessContext
         */
        WitnessContext create_test_context(const std::string &contract_address);

        /**
         * @brief Create secret for testing (32 zero bytes)
         *
         * @return 32-byte test secret
         */
        std::vector<uint8_t> create_test_secret();

        /**
         * @brief Validate secret size
         *
         * @param secret Secret bytes
         * @param expected_size Expected size (typically 32)
         * @return true if size matches
         */
        bool validate_secret_size(
            const std::vector<uint8_t> &secret,
            size_t expected_size = 32);

        /**
         * @brief Hash private data for storage key
         *
         * @param secret Secret to hash
         * @return SHA256 hash as hex string
         */
        std::string hash_secret(const std::vector<uint8_t> &secret);
    }

} // namespace midnight::zk
