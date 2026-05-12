#pragma once

#include "midnight/network/network_client.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace midnight::network
{

    using json = nlohmann::json;

    /**
     * @brief DUST registration status on the Midnight network
     */
    enum class DustRegistrationStatus
    {
        NOT_REGISTERED,     ///< No DUST registration found
        PENDING,            ///< Registration submitted, awaiting confirmation
        REGISTERED,         ///< DUST is active
        EXPIRED,            ///< Registration expired (NIGHT withdrawn)
        UNKNOWN             ///< Unable to determine status
    };

    /**
     * @brief DUST generation status from the indexer
     */
    struct DustGenerationStatus
    {
        std::string cardano_reward_address;
        std::string dust_address;
        bool registered = false;
        std::string night_balance;
        std::string generation_rate;
        std::string max_capacity;
        std::string current_capacity;
        std::string utxo_tx_hash;
        uint32_t utxo_output_index = 0;
    };

    /**
     * @brief DUST registration record
     */
    struct DustRegistration
    {
        std::string dust_address;
        bool valid = false;
        std::string night_balance;
        std::string generation_rate;
    };

    /**
     * @brief Wallet state from the indexer
     */
    struct WalletState
    {
        std::string address;
        std::string unshielded_balance = "0";   ///< NIGHT balance (public)
        std::string shielded_balance = "0";      ///< NIGHT balance (shielded/private)
        std::string dust_balance = "0";         ///< DUST balance
        uint64_t available_utxo_count = 0;
        std::string session_id;                  ///< Active shielded session ID
        bool synced = false;
        std::string error;
    };

    /**
     * @brief Shielded wallet session information
     */
    struct ShieldedSession
    {
        std::string session_id;
        bool is_connected = false;
        uint64_t start_index = 0;
    };

    /**
     * @brief Contract ledger state from the indexer
     */
    struct ContractState
    {
        std::string contract_address;
        json ledger_state = json::object();     ///< All exported ledger fields
        json unshielded_balances = json::array(); ///< Token balances held by contract
        bool exists = false;
        uint64_t block_height = 0;
        std::string block_hash;
        std::string error;
    };

    /**
     * @brief Contract action result from the indexer (union type)
     *
     * Represents deploy, call, or update actions on a contract.
     */
    struct ContractAction
    {
        std::string address;                      ///< Contract address
        std::string action_type;                  ///< "deploy", "call", or "update"
        json state;                               ///< Current contract state
        json unshielded_balances = json::array(); ///< Token balances {tokenType, amount}
        std::string entry_point;                 ///< Entry point name (for calls)
        uint64_t block_height = 0;
        std::string block_hash;
        std::string error;
    };

    /**
     * @brief Contract balance entry
     */
    struct ContractBalance
    {
        std::string token_type;  ///< Token type identifier (hex)
        std::string amount;      ///< Balance amount (u128 as string)
    };

    /**
     * @brief DUST nullifier transaction record
     */
    struct DustNullifierTransaction
    {
        std::string nullifier;
        std::string commitment;
        uint64_t transaction_id = 0;
        uint64_t block_height = 0;
        std::string block_hash;
    };

    /**
     * @brief Shielded nullifier transaction record
     */
    struct ShieldedNullifierTransaction
    {
        uint64_t transaction_id = 0;
        std::string block_hash;
        uint64_t block_height = 0;
        std::string nullifier;
    };

    /**
     * @brief Native GraphQL client for the Midnight Indexer
     *
     * Replaces Node.js dependency for blockchain state queries.
     * Communicates directly with the indexer GraphQL endpoint.
     *
     * Architecture (following cardano-c pattern):
     *   C++ code → cpp-httplib HTTP POST → Indexer GraphQL → JSON response
     *
     * Performance Optimizations:
     *   - LRU cache for UTXO results (default: 100 addresses)
     *   - Block cache to avoid re-fetching same blocks
     *   - Batch query support for scanning multiple blocks efficiently
     *   - Cache invalidation based on block height changes
     *
     * @code
     *   IndexerClient indexer("https://indexer.testnet.midnight.network/api/v4/graphql");
     *   auto state = indexer.query_contract_state("contract_addr_...");
     *   auto wallet = indexer.query_wallet_state("mn_addr_...");
     * @endcode
     */
    class IndexerClient
    {
    public:
        /**
         * @brief Construct indexer client
         * @param graphql_url Full URL of the indexer GraphQL endpoint
         * @param timeout_ms HTTP request timeout in milliseconds
         */
        explicit IndexerClient(const std::string &graphql_url,
                               uint32_t timeout_ms = 15000);

        /**
         * @brief Enable or disable caching
         * @param enabled True to enable cache, false to disable
         */
        void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }

        /**
         * @brief Set cache TTL in seconds
         * @param ttl_seconds Cache TTL in seconds (default: 60)
         */
        void set_cache_ttl(uint32_t ttl_seconds) { cache_ttl_seconds_ = ttl_seconds; }

        /**
         * @brief Clear all caches
         */
        void clear_cache();

        /**
         * @brief Query wallet balance and state (optimized with caching)
         * @param address Midnight address (mn_addr_...)
         * @param use_cache Use cached result if available (default: true)
         * @return WalletState with balance information
         */
        WalletState query_wallet_state(const std::string &address, bool use_cache = true);

        /**
         * @brief Query UTXOs for an address (optimized with caching)
         * @param address Midnight address
         * @param use_cache Use cached result if available (default: true)
         * @return Vector of UTXOs owned by address
         */
        std::vector<blockchain::UTXO> query_utxos(const std::string &address, bool use_cache = true);

        /**
         * @brief Query UTXOs for an address within a block range (optimized)
         * @param address Midnight address
         * @param from_block Start block (inclusive)
         * @param to_block End block (inclusive)
         * @return Vector of UTXOs created within the block range
         */
        std::vector<blockchain::UTXO> query_utxos(const std::string &address,
                                                  uint32_t from_block,
                                                  uint32_t to_block);

        /**
         * @brief Query contract ledger state
         * @param contract_address Contract address
         * @return ContractState with all exported ledger fields and balances
         */
        ContractState query_contract_state(const std::string &contract_address);

        /**
         * @brief Query contract action (deploy/call/update)
         * @param contract_address Contract address
         * @return ContractAction with action type, state, and balances
         */
        ContractAction query_contract_action(const std::string &contract_address);

        /**
         * @brief Query contract unshielded balances
         * @param contract_address Contract address
         * @return Vector of token balances held by the contract
         */
        std::vector<ContractBalance> query_contract_balance(const std::string &contract_address);

        /**
         * @brief Query contract ledger state with specific fields
         * @param contract_address Contract address
         * @param fields List of field names to query
         * @return JSON object with requested fields
         */
        json query_contract_fields(const std::string &contract_address,
                                  const std::vector<std::string> &fields);

        /**
         * @brief Query a transaction by hash and extract contract-related data
         * @param tx_hash Transaction hash (hex)
         * @return JSON with contract actions and metadata
         */
        json query_transaction(const std::string &tx_hash);

        /**
         * @brief Query DUST registration status
         * @param address Address to check
         * @return DustRegistrationStatus
         */
        DustRegistrationStatus query_dust_status(const std::string &address);

        /**
         * @brief Query DUST generation status by Cardano stake address
         * @param cardano_reward_address Cardano stake address (stake_test1... or stake1...)
         * @return DustGenerationStatus with full DUST generation details
         */
        DustGenerationStatus query_dust_generation_status(const std::string &cardano_reward_address);

        /**
         * @brief Query DUST generation status for multiple Cardano stake addresses
         * @param cardano_reward_addresses Vector of Cardano stake addresses
         * @return Vector of DustGenerationStatus for each address
         */
        std::vector<DustGenerationStatus> query_dust_generation_statuses(
            const std::vector<std::string> &cardano_reward_addresses);

        /**
         * @brief Query all DUST registrations for a Cardano stake address
         * @param cardano_reward_address Cardano stake address
         * @return Vector of DustRegistration records
         */
        std::vector<DustRegistration> query_dust_registrations(const std::string &cardano_reward_address);

        /**
         * @brief Get current block height
         * @return Latest block height
         */
        uint64_t get_block_height();

        /**
         * @brief Query dust nullifiers by prefix
         * @param nullifier_prefixes Vector of nullifier prefixes to search for
         * @param from_block Start block height (inclusive, default: 0)
         * @param to_block End block height (inclusive, default: 0 means no limit)
         * @return Vector of DustNullifierTransaction matching the criteria
         */
        std::vector<DustNullifierTransaction> query_dust_nullifiers(
            const std::vector<std::string> &nullifier_prefixes,
            uint64_t from_block = 0,
            uint64_t to_block = 0);

        /**
         * @brief Query shielded nullifiers by prefix
         * @param nullifier_prefixes Vector of nullifier prefixes to search for
         * @param from_block Start block height (inclusive, default: 0)
         * @param to_block End block height (inclusive, default: 0 means no limit)
         * @return Vector of ShieldedNullifierTransaction matching the criteria
         */
        std::vector<ShieldedNullifierTransaction> query_shielded_nullifiers(
            const std::vector<std::string> &nullifier_prefixes,
            uint64_t from_block = 0,
            uint64_t to_block = 0);

        /**
         * @brief Check if the indexer is synced
         * @return true if the indexer is fully synced with the network
         */
        bool is_synced();

        /**
         * @brief Execute a raw GraphQL query
         * @param query GraphQL query string
         * @param variables Optional variables object
         * @return JSON response data
         */
        json graphql_query(const std::string &query,
                           const json &variables = json::object());

        /**
         * @brief Get the base URL of the indexer
         */
        std::string get_url() const { return graphql_url_; }

        /**
         * @brief Connect a shielded wallet using a viewing key
         * @param viewing_key The viewing key (mn_shield-esk_dev1...)
         * @param start_index Starting index for scanning (default: 0)
         * @return ShieldedSession with session_id on success, empty session_id on failure
         */
        ShieldedSession connect_shielded_wallet(const std::string &viewing_key,
                                               uint64_t start_index = 0);

        /**
         * @brief Disconnect a shielded wallet session
         * @param session_id The session ID to disconnect
         * @return true if disconnected successfully
         */
        bool disconnect_shielded_wallet(const std::string &session_id);

        /**
         * @brief Query shielded balance for a session
         * @param session_id The active session ID
         * @return Shielded balance as string
         */
        std::string query_shielded_balance(const std::string &session_id);

    private:
        std::string graphql_url_;
        uint32_t timeout_ms_;
        std::unique_ptr<NetworkClient> client_;

        bool cache_enabled_ = true;
        uint32_t cache_ttl_seconds_ = 60;
        uint64_t last_tip_height_ = 0;

        struct CacheEntry {
            json data;
            std::chrono::steady_clock::time_point timestamp;
        };

        mutable std::shared_mutex cache_mutex_;
        std::unordered_map<std::string, CacheEntry> utxo_cache_;
        std::unordered_map<std::string, CacheEntry> wallet_cache_;
        std::unordered_map<uint32_t, CacheEntry> block_cache_;

        std::unordered_set<std::string> known_spent_hashes_;
        std::unordered_map<std::string, uint32_t> address_last_scan_block_;

        bool is_cache_valid(const CacheEntry& entry) const;
        std::string get_cache_key(const std::string& prefix, const std::string& value) const;
        void prune_expired_cache();

        /**
         * @brief Execute GraphQL POST request
         * @param body JSON request body
         * @return Parsed JSON response
         */
        json execute_graphql(const json &body);

        /**
         * @brief Parse URL into host, port, path components
         */
        static void parse_url(const std::string &url,
                              std::string &scheme,
                              std::string &host,
                              uint16_t &port,
                              std::string &path);

        std::vector<blockchain::UTXO> scan_utxos_internal(const std::string &address,
                                                          uint32_t from_block,
                                                          uint32_t to_block,
                                                          bool use_cache);
    };

} // namespace midnight::network
