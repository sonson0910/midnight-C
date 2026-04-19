#ifndef MIDNIGHT_NODE_RPC_HPP
#define MIDNIGHT_NODE_RPC_HPP

#include "network_client.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include <vector>
#include <string>

namespace midnight::network
{

    using json = nlohmann::json;

    /**
     * @brief Midnight node JSON-RPC client
     *
     * Implements JSON-RPC 2.0 methods for interacting with a Midnight blockchain node.
     * Handles serialization/deserialization of blockchain types (transactions, UTXOs, etc).
     *
     * Example usage:
     * @code
     *   MidnightNodeRPC rpc("http://localhost:5678");
     *
     *   // Query UTXOs
     *   auto utxos = rpc.get_utxos("addr_test...");
     *
     *   // Get protocol parameters
     *   auto params = rpc.get_protocol_params();
     *
     *   // Submit transaction
     *   auto result = rpc.submit_transaction(tx_hex);
     * @endcode
     */
    class MidnightNodeRPC
    {
    public:
        /**
         * @brief Construct RPC client
         * @param node_url URL of Midnight node RPC endpoint
         * @param timeout_ms Request timeout in milliseconds
         */
        MidnightNodeRPC(const std::string &node_url, uint32_t timeout_ms = 5000);

        /**
         * @brief Query UTXOs for a given address
         * @param address Midnight address
         * @return Vector of UTXOs owned by address
         * @throws std::runtime_error on RPC error
         */
        std::vector<midnight::blockchain::UTXO> get_utxos(
            const std::string &address);

        /**
         * @brief Get protocol parameters
         * @return Protocol parameters for transaction building
         * @throws std::runtime_error on RPC error
         */
        midnight::blockchain::ProtocolParams get_protocol_params();

        /**
         * @brief Get current chain tip
         * @return Pair of (block height, block hash)
         * @throws std::runtime_error on RPC error
         */
        std::pair<uint64_t, std::string> get_chain_tip();

        /**
         * @brief Submit signed transaction to mempool
         * @param tx_hex CBOR hex-encoded signed transaction
         * @return Transaction ID if successful
         * @throws std::runtime_error on RPC error (e.g., validation failure)
         */
        std::string submit_transaction(const std::string &tx_hex);

        /**
         * @brief Get transaction by ID
         * @param tx_id Transaction ID (hash)
         * @return JSON representation of transaction
         * @throws std::runtime_error if not found or on RPC error
         */
        json get_transaction(const std::string &tx_id);

        /**
         * @brief Wait for transaction to be confirmed
         * @param tx_id Transaction ID
         * @param max_blocks Maximum number of blocks to wait
         * @return true if confirmed, false if timeout
         */
        bool wait_for_confirmation(
            const std::string &tx_id,
            uint32_t max_blocks = 10);

        /**
         * @brief Get account balance
         * @param address Address to query
         * @return Balance in base units
         */
        uint64_t get_balance(const std::string &address);

        /**
         * @brief Evaluate script (for smart contract validation)
         * @param script Plutus/Compact script bytecode
         * @param redeemer Redeemer data
         * @return Execution result
         */
        json evaluate_script(
            const std::string &script,
            const std::string &redeemer);

        /**
         * @brief Get node version info
         * @return JSON object with version, network, etc.
         */
        json get_node_info();

        /**
         * @brief Check if node is ready
         * @return true if node is synchronized and ready for transactions
         */
        bool is_ready();

    private:
        std::unique_ptr<NetworkClient> client_;
        uint32_t request_id_counter_ = 1;
        std::vector<std::string> rpc_paths_ = {"/", "/rpc"};

        /**
         * @brief Make JSON-RPC 2.0 call
         * @param method RPC method name
         * @param params Parameters object or array
         * @return Result field from JSON-RPC response
         * @throws std::runtime_error on RPC error
         */
        json rpc_call(
            const std::string &method,
            const json &params = json::object());

        /**
         * @brief Convert JSON UTXO to blockchain::UTXO
         */
        static midnight::blockchain::UTXO json_to_utxo(const json &js);

        /**
         * @brief Convert JSON to ProtocolParams
         */
        static midnight::blockchain::ProtocolParams json_to_protocol_params(
            const json &js);

        /**
         * @brief Get next RPC request ID
         */
        uint32_t get_next_request_id() { return request_id_counter_++; }
    };

} // namespace midnight::network

#endif // MIDNIGHT_NODE_RPC_HPP
