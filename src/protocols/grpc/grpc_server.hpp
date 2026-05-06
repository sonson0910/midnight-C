#pragma once
#include "midnight.grpc.pb.h"

#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace midnight::protocols::grpc
{

    /**
     * @brief Local gRPC server implementing MidnightNode service
     *
     * Runs an embedded gRPC server that processes blockchain operations.
     * Delegates actual work to existing SDK components (MidnightNodeRPC, IndexerClient, etc.)
     */
    class MidnightGrpcServer : public midnight::MidnightNode::Service
    {
    public:
        /**
         * @brief Construct gRPC server
         * @param listen_address Address to listen on (e.g., "[::1]:50051")
         */
        explicit MidnightGrpcServer(const std::string& listen_address = "[::1]:50051");
        ~MidnightGrpcServer();

        // Start/stop the server
        /**
         * @brief Start the gRPC server
         * @return true if server started successfully
         */
        bool start();

        /**
         * @brief Shutdown the gRPC server gracefully
         */
        void shutdown();

        /**
         * @brief Check if server is running
         * @return true if server is currently running
         */
        bool is_running() const;

        // Dependency injection (set before start)

        /**
         * @brief Set the MidnightNodeRPC client for blockchain operations
         * @param rpc Shared pointer to MidnightNodeRPC
         */
        void set_node_rpc(const std::shared_ptr<network::MidnightNodeRPC>& rpc);

        /**
         * @brief Set the IndexerClient for UTXO and state queries
         * @param idx Shared pointer to IndexerClient
         */
        void set_indexer(const std::shared_ptr<network::IndexerClient>& idx);

        /**
         * @brief Set the ContractManager for contract operations
         * @param mgr Shared pointer to ContractManager
         */
        void set_contract_manager(const std::shared_ptr<contract::ContractManager>& mgr);

        // gRPC Service overrides

        grpc::Status SubmitTransaction(
            grpc::ServerContext* context,
            const midnight::SubmitTxRequest* request,
            midnight::SubmitTxResponse* response) override;

        grpc::Status QueryTransaction(
            grpc::ServerContext* context,
            const midnight::TxQuery* request,
            midnight::TxResponse* response) override;

        grpc::Status QueryUTXOs(
            grpc::ServerContext* context,
            const midnight::UtxoQuery* request,
            midnight::UtxoResponse* response) override;

        grpc::Status GetBlock(
            grpc::ServerContext* context,
            const midnight::BlockQuery* request,
            midnight::BlockResponse* response) override;

        grpc::Status GetProtocolParams(
            grpc::ServerContext* context,
            const midnight::ProtocolParamsRequest* request,
            midnight::ProtocolParamsResponse* response) override;

        grpc::Status GetNodeInfo(
            grpc::ServerContext* context,
            const midnight::NodeInfoRequest* request,
            midnight::NodeInfoResponse* response) override;

        grpc::Status DeployContract(
            grpc::ServerContext* context,
            const midnight::ContractDeployRequest* request,
            midnight::ContractResponse* response) override;

        grpc::Status CallContract(
            grpc::ServerContext* context,
            const midnight::ContractCallRequest* request,
            midnight::ContractResponse* response) override;

        grpc::Status GetBalance(
            grpc::ServerContext* context,
            const midnight::BalanceQuery* request,
            midnight::BalanceResponse* response) override;

        grpc::Status SubscribeBlocks(
            grpc::ServerContext* context,
            const midnight::Empty* request,
            grpc::ServerWriter<midnight::BlockHeader>* writer) override;

        grpc::Status WatchTransactions(
            grpc::ServerContext* context,
            const midnight::UtxoQuery* request,
            grpc::ServerWriter<midnight::TxResponse>* writer) override;

    private:
        // Convert bytes to hex string (without 0x prefix)
        static std::string bytes_to_hex(const std::vector<uint8_t>& bytes);

        // Convert hex string to bytes
        static std::vector<uint8_t> hex_to_bytes(const std::string& hex);

        std::string listen_address_;
        std::unique_ptr<grpc::Server> server_;
        std::unique_ptr<grpc::ServerBuilder> builder_;

        std::shared_ptr<network::MidnightNodeRPC> node_rpc_;
        std::shared_ptr<network::IndexerClient> indexer_;
        std::shared_ptr<contract::ContractManager> contract_mgr_;

        mutable std::mutex mu_;
        std::atomic<bool> running_{false};
        std::condition_variable shutdown_cv_;
    };

} // namespace midnight::protocols::grpc
