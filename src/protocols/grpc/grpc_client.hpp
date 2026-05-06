#pragma once
#include "midnight.grpc.pb.h"

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

namespace midnight::protocols::grpc
{

    using BlockCallback = std::function<void(const midnight::BlockHeader& block)>;
    using TxCallback = std::function<void(const midnight::TxResponse& tx)>;

    /**
     * @brief gRPC client for Midnight node communication
     *
     * Provides a type-safe gRPC client wrapping the MidnightNode service.
     * Supports:
     *   - Transaction submission
     *   - UTXO queries
     *   - Block data
     *   - Contract deployment and calls
     *   - Balance queries
     *   - Server-side streaming (block/tx subscriptions)
     *
     * Requires a Midnight node with gRPC support running at the specified address.
     */
    class MidnightGrpcClient
    {
    public:
        /**
         * @brief Construct gRPC client
         * @param server_address Address of the Midnight gRPC server (e.g., "localhost:50051")
         */
        explicit MidnightGrpcClient(const std::string& server_address);
        ~MidnightGrpcClient();

        // Non-copyable
        MidnightGrpcClient(const MidnightGrpcClient&) = delete;
        MidnightGrpcClient& operator=(const MidnightGrpcClient&) = delete;

        /**
         * @brief Check if the client is connected to the server
         * @return true if channel is ready
         */
        bool is_connected() const;

        // ── Transaction Operations ───────────────────────────────

        /**
         * @brief Submit a signed transaction to the network
         * @param tx Signed transaction protobuf message
         * @return Transaction hash if successful, empty string on failure
         */
        std::string submit_transaction(const midnight::SignedTransaction& tx);

        /**
         * @brief Query transaction status by hash
         * @param tx_hash Transaction hash (hex)
         * @return Transaction details including block height and confirmations
         */
        midnight::TxResponse query_transaction(const std::string& tx_hash);

        // ── UTXO Operations ────────────────────────────────────

        /**
         * @brief Query UTXOs for an address
         * @param address Midnight address
         * @param limit Maximum number of UTXOs to return
         * @param after_block Only return UTXOs confirmed after this block
         * @return UTXO list and total count
         */
        midnight::UtxoResponse query_utxos(const std::string& address,
                                           uint32_t limit = 100,
                                           uint64_t after_block = 0);

        // ── Block Operations ─────────────────────────────────

        /**
         * @brief Get block by height
         * @param height Block height
         * @return Block with header and transaction summaries
         */
        midnight::BlockResponse get_block(uint32_t height);

        /**
         * @brief Get block by hash
         * @param hash Block hash (hex)
         * @return Block with header and transaction summaries
         */
        midnight::BlockResponse get_block(const std::string& hash);

        /**
         * @brief Get current protocol parameters
         * @return Protocol parameters (fees, limits, etc.)
         */
        midnight::ProtocolParamsResponse get_protocol_params();

        /**
         * @brief Get node information
         * @return Node details (version, sync status, peers)
         */
        midnight::NodeInfoResponse get_node_info();

        // ── Contract Operations ────────────────────────────────

        /**
         * @brief Deploy a smart contract
         * @param code Contract bytecode
         * @param constructor_args SCALE-encoded constructor arguments
         * @param gas_limit Gas limit for deployment
         * @param storage_limit Storage deposit limit
         * @param value Value to transfer with deployment
         * @return Deployment result with contract address on success
         */
        midnight::ContractResponse deploy_contract(
            const std::vector<uint8_t>& code,
            const std::vector<uint8_t>& constructor_args,
            uint64_t gas_limit = 5'000'000'000,
            uint64_t storage_limit = 0,
            uint64_t value = 0);

        /**
         * @brief Call a contract circuit function
         * @param contract_address Target contract address
         * @param circuit_name Circuit/function name to call
         * @param call_data SCALE-encoded call arguments
         * @param gas_limit Gas limit for call
         * @param value Value to transfer with call
         * @return Call result with return data on success
         */
        midnight::ContractResponse call_contract(
            const std::string& contract_address,
            const std::string& circuit_name,
            const std::vector<uint8_t>& call_data,
            uint64_t gas_limit = 5'000'000'000,
            uint64_t value = 0);

        // ── Balance ──────────────────────────────────────────

        /**
         * @brief Get address balance
         * @param address Midnight address
         * @return Balance breakdown (unshielded, dust, total)
         */
        midnight::BalanceResponse get_balance(const std::string& address);

    // ── Streaming ─────────────────────────────────────────────

    /**
     * @brief Subscribe to new block headers
     * @param callback Called for each new block header received
     *
     * This launches a background thread that continuously receives
     * block headers until cancel_subscriptions() is called.
     */
    void subscribe_blocks(BlockCallback callback);

    /**
     * @brief Watch transactions for an address
     * @param address Address to watch
     * @param callback Called for each transaction involving the address
     *
     * This launches a background thread that continuously receives
     * transaction confirmations until cancel_subscriptions() is called.
     */
    void watch_transactions(const std::string& address, TxCallback callback);

    /**
     * @brief Cancel all active subscriptions
     *
     * Interrupts any active streaming calls and joins background threads.
     */
    void cancel_subscriptions();

    private:
    void block_stream_loop(BlockCallback cb);
    void tx_stream_loop(const std::string& address, TxCallback cb);

    class Impl;
    std::unique_ptr<Impl> pImpl_;
    std::unique_ptr<grpc::ClientContext> block_context_;
    std::unique_ptr<grpc::ClientContext> tx_context_;
};

} // namespace midnight::protocols::grpc
