#pragma once

#include "midnight/network/network_client.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>

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
     * @brief Wallet state from the indexer
     */
    struct WalletState
    {
        std::string address;
        std::string unshielded_balance = "0";   ///< NIGHT balance (public)
        std::string dust_balance = "0";         ///< DUST balance
        uint64_t available_utxo_count = 0;
        bool synced = false;
        std::string error;
    };

    /**
     * @brief Contract ledger state from the indexer
     */
    struct ContractState
    {
        std::string contract_address;
        json ledger_state = json::object();     ///< All exported ledger fields
        bool exists = false;
        uint64_t block_height = 0;
        std::string error;
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
         * @brief Query wallet balance and state
         * @param address Midnight address (mn_addr_...)
         * @return WalletState with balance information
         */
        WalletState query_wallet_state(const std::string &address);

        /**
         * @brief Query UTXOs for an address
         * @param address Midnight address
         * @return Vector of UTXOs owned by address
         */
        std::vector<blockchain::UTXO> query_utxos(const std::string &address);

        /**
         * @brief Query contract ledger state
         * @param contract_address Contract address
         * @return ContractState with all exported ledger fields
         */
        ContractState query_contract_state(const std::string &contract_address);

        /**
         * @brief Query contract ledger state with specific fields
         * @param contract_address Contract address
         * @param fields List of field names to query
         * @return JSON object with requested fields
         */
        json query_contract_fields(const std::string &contract_address,
                                   const std::vector<std::string> &fields);

        /**
         * @brief Query DUST registration status
         * @param address Address to check
         * @return DustRegistrationStatus
         */
        DustRegistrationStatus query_dust_status(const std::string &address);

        /**
         * @brief Get current block height
         * @return Latest block height
         */
        uint64_t get_block_height();

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

    private:
        std::string graphql_url_;
        uint32_t timeout_ms_;
        std::unique_ptr<NetworkClient> client_;

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
    };

} // namespace midnight::network
