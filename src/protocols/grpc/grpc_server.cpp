#include "midnight/protocols/grpc/grpc_server.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/network/indexer_client.hpp"
#include "midnight/contract/contract_manager.hpp"

#include <grpc++/grpc++.h>

#include <chrono>

namespace midnight::protocols::grpc
{

    using grpc::ServerContext;
    using grpc::Status;
    using grpc::ServerWriter;

    // ── Utility Functions ─────────────────────────────────────

    std::string MidnightGrpcServer::bytes_to_hex(const std::vector<uint8_t>& bytes)
    {
        static const char* kHex = "0123456789abcdef";
        std::string encoded;
        encoded.reserve(bytes.size() * 2);
        for (uint8_t byte : bytes)
        {
            encoded.push_back(kHex[(byte >> 4) & 0x0F]);
            encoded.push_back(kHex[byte & 0x0F]);
        }
        return encoded;
    }

    std::vector<uint8_t> MidnightGrpcServer::hex_to_bytes(const std::string& hex)
    {
        std::vector<uint8_t> bytes;
        std::string clean_hex = hex;
        if (clean_hex.rfind("0x", 0) == 0 || clean_hex.rfind("0X", 0) == 0)
        {
            clean_hex = clean_hex.substr(2);
        }
        bytes.reserve(clean_hex.size() / 2);
        for (size_t i = 0; i + 1 < clean_hex.size(); i += 2)
        {
            auto hex_nibble = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                char lower = static_cast<char>(std::tolower(c));
                if (lower >= 'a' && lower <= 'f') return 10 + (lower - 'a');
                return 0;
            };
            uint8_t byte = static_cast<uint8_t>(
                (hex_nibble(clean_hex[i]) << 4) | hex_nibble(clean_hex[i + 1]));
            bytes.push_back(byte);
        }
        return bytes;
    }

    // ── Server Lifecycle ─────────────────────────────────────

    MidnightGrpcServer::MidnightGrpcServer(const std::string& listen_address)
        : listen_address_(listen_address)
    {
        builder_ = std::make_unique<grpc::ServerBuilder>();
        builder_->AddListeningPort(listen_address_, grpc::InsecureServerCredentials());
        builder_->RegisterService(this);

        auto logger = midnight::g_logger;
        if (logger)
        {
            logger->info("MidnightGrpcServer: configured for " + listen_address);
        }
    }

    MidnightGrpcServer::~MidnightGrpcServer()
    {
        if (running_.load())
        {
            shutdown();
        }
    }

    bool MidnightGrpcServer::start()
    {
        std::lock_guard<std::mutex> lock(mu_);

        if (running_.load())
        {
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->warn("MidnightGrpcServer: already running");
            }
            return true;
        }

        server_ = builder_->BuildAndStart();
        if (!server_)
        {
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("MidnightGrpcServer: failed to start");
            }
            return false;
        }

        running_.store(true);

        auto logger = midnight::g_logger;
        if (logger)
        {
            logger->info("MidnightGrpcServer: started on " + listen_address_);
        }

        return true;
    }

    void MidnightGrpcServer::shutdown()
    {
        std::unique_lock<std::mutex> lock(mu_);

        if (!running_.load())
        {
            return;
        }

        running_.store(false);

        if (server_)
        {
            server_->Shutdown();
            server_->Wait();
            server_.reset();
        }

        auto logger = midnight::g_logger;
        if (logger)
        {
            logger->info("MidnightGrpcServer: shutdown complete");
        }
    }

    bool MidnightGrpcServer::is_running() const
    {
        return running_.load();
    }

    // ── Dependency Injection ──────────────────────────────────

    void MidnightGrpcServer::set_node_rpc(const std::shared_ptr<network::MidnightNodeRPC>& rpc)
    {
        std::lock_guard<std::mutex> lock(mu_);
        node_rpc_ = rpc;
        auto logger = midnight::g_logger;
        if (logger)
        {
            logger->info("MidnightGrpcServer: MidnightNodeRPC injected");
        }
    }

    void MidnightGrpcServer::set_indexer(const std::shared_ptr<network::IndexerClient>& idx)
    {
        std::lock_guard<std::mutex> lock(mu_);
        indexer_ = idx;
        auto logger = midnight::g_logger;
        if (logger)
        {
            logger->info("MidnightGrpcServer: IndexerClient injected");
        }
    }

    void MidnightGrpcServer::set_contract_manager(const std::shared_ptr<contract::ContractManager>& mgr)
    {
        std::lock_guard<std::mutex> lock(mu_);
        contract_mgr_ = mgr;
        auto logger = midnight::g_logger;
        if (logger)
        {
            logger->info("MidnightGrpcServer: ContractManager injected");
        }
    }

    // ── RPC Implementations ─────────────────────────────────

    Status MidnightGrpcServer::SubmitTransaction(
        ServerContext* context,
        const midnight::SubmitTxRequest* request,
        midnight::SubmitTxResponse* response)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto logger = midnight::g_logger;

        if (!node_rpc_)
        {
            response->set_success(false);
            response->set_error("MidnightNodeRPC not configured");
            if (logger)
            {
                logger->error("SubmitTransaction: MidnightNodeRPC not configured");
            }
            return Status::OK;
        }

        try
        {
            // Extract transaction from request
            const auto& signed_tx = request->transaction();
            std::string tx_hex = bytes_to_hex(
                std::vector<uint8_t>(signed_tx.transaction().begin(), signed_tx.transaction().end()));

            // Submit via RPC
            std::string tx_hash = node_rpc_->submit_transaction(tx_hex);

            response->set_tx_hash(tx_hash);
            response->set_success(true);

            if (logger)
            {
                logger->info("SubmitTransaction: submitted tx " + tx_hash);
            }
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error(e.what());
            if (logger)
            {
                logger->error(std::string("SubmitTransaction error: ") + e.what());
            }
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::QueryTransaction(
        ServerContext* context,
        const midnight::TxQuery* request,
        midnight::TxResponse* response)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto logger = midnight::g_logger;
        const std::string& tx_hash = request->tx_hash();

        if (logger)
        {
            logger->debug("QueryTransaction: querying " + tx_hash);
        }

        // Try indexer first, then node RPC
        if (indexer_)
        {
            try
            {
                auto tx_json = indexer_->query_transaction(tx_hash);
                if (!tx_json.empty())
                {
                    if (tx_json.contains("blockHeight"))
                    {
                        response->set_block_height(tx_json["blockHeight"].get<uint32_t>());
                    }
                    if (tx_json.contains("blockHash"))
                    {
                        response->set_block_hash(tx_json["blockHash"].get<std::string>());
                    }
                    if (tx_json.contains("finalized"))
                    {
                        response->set_finalized(tx_json["finalized"].get<bool>());
                    }
                }
            }
            catch (const std::exception& e)
            {
                if (logger)
                {
                    logger->warn("QueryTransaction indexer error: " + std::string(e.what()));
                }
            }
        }

        response->set_tx_hash(tx_hash);

        // If we have a node RPC, also query it for more details
        if (node_rpc_)
        {
            try
            {
                auto tx_json = node_rpc_->get_transaction(tx_hash);
                if (!tx_json.empty())
                {
                    if (tx_json.contains("blockNumber"))
                    {
                        response->set_block_height(tx_json["blockNumber"].get<uint32_t>());
                    }
                }
            }
            catch (const std::exception& e)
            {
                if (logger)
                {
                    logger->warn("QueryTransaction RPC error: " + std::string(e.what()));
                }
            }
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::QueryUTXOs(
        ServerContext* context,
        const midnight::UtxoQuery* request,
        midnight::UtxoResponse* response)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto logger = midnight::g_logger;
        const std::string& address = request->address();

        if (logger)
        {
            logger->debug("QueryUTXOs: querying for " + address);
        }

        if (!indexer_)
        {
            if (logger)
            {
                logger->error("QueryUTXOs: IndexerClient not configured");
            }
            return Status::OK;
        }

        try
        {
            auto utxos = indexer_->query_utxos(address);

            uint64_t total = 0;
            for (const auto& utxo : utxos)
            {
                auto* proto_utxo = response->add_utxos();
                proto_utxo->set_tx_hash(utxo.tx_hash);
                proto_utxo->set_output_index(utxo.output_index);
                proto_utxo->set_address(utxo.address);
                proto_utxo->set_amount_commitment(utxo.amount_commitment);
                proto_utxo->set_value(utxo.value);
                proto_utxo->set_token_type(utxo.token_type);
                proto_utxo->set_block_height(utxo.block_height);
                total += utxo.value;
            }

            response->set_total(total);

            if (logger)
            {
                logger->info("QueryUTXOs: returned " + std::to_string(utxos.size()) + " UTXOs");
            }
        }
        catch (const std::exception& e)
        {
            if (logger)
            {
                logger->error(std::string("QueryUTXOs error: ") + e.what());
            }
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::GetBlock(
        ServerContext* context,
        const midnight::BlockQuery* request,
        midnight::BlockResponse* response)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto logger = midnight::g_logger;

        if (!node_rpc_)
        {
            if (logger)
            {
                logger->error("GetBlock: MidnightNodeRPC not configured");
            }
            return Status::OK;
        }

        try
        {
            uint32_t block_height = 0;
            std::string block_hash;

            if (request->has_by_height())
            {
                block_height = request->by_height();
                if (logger)
                {
                    logger->debug("GetBlock: querying by height " + std::to_string(block_height));
                }
                // MidnightNodeRPC does not expose a get_block_by_height RPC method.
                // The IndexerClient similarly lacks a direct block-by-height query.
                // As a best-effort, return the chain tip hash paired with the requested height.
                // Full block data (header, transactions) requires a dedicated block query API
                // that is not yet available in the RPC or indexer.
                auto [tip_height, tip_hash] = node_rpc_->get_chain_tip();
                (void)tip_height;
                block_hash = tip_hash;
                if (logger)
                {
                    logger->warn("GetBlock: height " + std::to_string(block_height) +
                               " queried; MidnightNodeRPC lacks get_block_by_height — "
                               "returning tip hash. Implement get_block_by_height in MidnightNodeRPC "
                               "or add IndexerClient::query_block_by_height() for full block data.");
                }
            }
            else if (request->has_by_hash())
            {
                block_hash = request->by_hash();
                if (logger)
                {
                    logger->debug("GetBlock: querying by hash " + block_hash);
                }
                // MidnightNodeRPC does not expose a get_block_by_hash RPC method.
                // Return the requested hash in the response and note the limitation.
                if (logger)
                {
                    logger->warn("GetBlock: block-by-hash queried; "
                               "MidnightNodeRPC lacks get_block_by_hash — "
                               "hash set from request. Implement get_block_by_hash in MidnightNodeRPC "
                               "or add IndexerClient::query_block_by_hash() for full block data.");
                }
                // Set height from chain tip as best-effort reference
                auto [tip_height, tip_hash] = node_rpc_->get_chain_tip();
                (void)tip_hash;
                block_height = static_cast<uint32_t>(tip_height);
            }
            else
            {
                // No query specified — return chain tip
                auto [tip_height, tip_hash] = node_rpc_->get_chain_tip();
                block_height = static_cast<uint32_t>(tip_height);
                block_hash = tip_hash;
                if (logger)
                {
                    logger->debug("GetBlock: no query specified, returning chain tip height " +
                                  std::to_string(tip_height));
                }
            }

            auto* header = response->mutable_header();
            header->set_height(block_height);
            header->set_hash(block_hash);

            if (logger)
            {
                logger->info("GetBlock: returned block header for height " +
                           std::to_string(block_height) + ", hash " +
                           (block_hash.size() > 16 ? block_hash.substr(0, 16) + "..." : block_hash));
            }
        }
        catch (const std::exception& e)
        {
            if (logger)
            {
                logger->error(std::string("GetBlock error: ") + e.what());
            }
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::GetProtocolParams(
        ServerContext* context,
        const midnight::ProtocolParamsRequest* request,
        midnight::ProtocolParamsResponse* response)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto logger = midnight::g_logger;

        if (!node_rpc_)
        {
            if (logger)
            {
                logger->error("GetProtocolParams: MidnightNodeRPC not configured");
            }
            return Status::OK;
        }

        try
        {
            auto params = node_rpc_->get_protocol_params();

            response->set_min_fee_a(params.min_fee_a);
            response->set_min_fee_b(params.min_fee_b);
            response->set_max_tx_size(params.max_tx_size);
            response->set_max_block_size(params.max_block_size);

            if (logger)
            {
                logger->info("GetProtocolParams: returned params");
            }
        }
        catch (const std::exception& e)
        {
            if (logger)
            {
                logger->error(std::string("GetProtocolParams error: ") + e.what());
            }
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::GetNodeInfo(
        ServerContext* context,
        const midnight::NodeInfoRequest* request,
        midnight::NodeInfoResponse* response)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto logger = midnight::g_logger;

        if (!node_rpc_)
        {
            if (logger)
            {
                logger->error("GetNodeInfo: MidnightNodeRPC not configured");
            }
            return Status::OK;
        }

        try
        {
            auto info = node_rpc_->get_node_info();

            if (info.contains("chain"))
            {
                response->set_chain(info["chain"].get<std::string>());
            }
            if (info.contains("name"))
            {
                response->set_name(info["name"].get<std::string>());
            }
            if (info.contains("version"))
            {
                response->set_version(info["version"].get<std::string>());
            }
            if (info.contains("synced"))
            {
                response->set_synced(info["synced"].get<bool>());
            }
            if (info.contains("peers"))
            {
                response->set_peers(info["peers"].get<uint32_t>());
            }
            if (info.contains("bestHeight"))
            {
                response->set_best_height(info["bestHeight"].get<uint32_t>());
            }

            response->set_timestamp(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            if (logger)
            {
                logger->info("GetNodeInfo: returned node info");
            }
        }
        catch (const std::exception& e)
        {
            if (logger)
            {
                logger->error(std::string("GetNodeInfo error: ") + e.what());
            }
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::DeployContract(
        ServerContext* context,
        const midnight::ContractDeployRequest* request,
        midnight::ContractResponse* response)
    {
        auto logger = midnight::g_logger;

        if (!contract_mgr_)
        {
            response->set_success("false");
            response->set_error("ContractManager not configured");
            if (logger)
            {
                logger->error("DeployContract: ContractManager not configured");
            }
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                               "ContractManager not configured — cannot deploy contract via gRPC. "
                               "Use ContractInteractor::deploy() directly for contract deployment.");
        }

        try
        {
            std::vector<uint8_t> code(request->code().begin(), request->code().end());
            std::vector<uint8_t> args(request->constructor_args().begin(),
                                      request->constructor_args().end());

            if (logger)
            {
                logger->info("DeployContract: deploying contract with " +
                             std::to_string(code.size()) + " bytes");
            }

            // Deployment via gRPC requires the full signing pipeline to be injected.
            // This stub cannot complete deployment without wallet/key injection.
            // Return UNIMPLEMENTED so clients know this path is not yet wired.
            if (logger)
            {
                logger->warn("DeployContract: gRPC path not implemented — "
                             "wallet signer integration required");
            }

            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                               "Contract deployment via gRPC is not implemented. "
                               "Use ContractInteractor::deploy() directly with a wallet seed set via set_seed_hex().");
        }
        catch (const std::exception& e)
        {
            response->set_success("false");
            response->set_error(e.what());
            if (logger)
            {
                logger->error(std::string("DeployContract error: ") + e.what());
            }
            return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
        }
    }

    Status MidnightGrpcServer::CallContract(
        ServerContext* context,
        const midnight::ContractCallRequest* request,
        midnight::ContractResponse* response)
    {
        auto logger = midnight::g_logger;

        if (!contract_mgr_)
        {
            response->set_success("false");
            response->set_error("ContractManager not configured");
            if (logger)
            {
                logger->error("CallContract: ContractManager not configured");
            }
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                               "ContractManager not configured — cannot call contract via gRPC.");
        }

        try
        {
            const std::string& contract_addr = request->contract_address();
            const std::string& circuit = request->circuit_name();
            std::vector<uint8_t> call_data(request->call_data().begin(),
                                           request->call_data().end());

            if (logger)
            {
                logger->info("CallContract: calling " + circuit + " on " + contract_addr);
            }

            // Contract calls via gRPC require wallet/key injection that is not yet wired.
            // Return UNIMPLEMENTED so clients know this path is not available.
            if (logger)
            {
                logger->warn("CallContract: gRPC path not implemented — "
                             "wallet signer integration required");
            }

            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                               "Contract calls via gRPC are not implemented. "
                               "Use ContractInteractor::call_circuit() directly with a wallet seed set.");
        }
        catch (const std::exception& e)
        {
            response->set_success("false");
            response->set_error(e.what());
            if (logger)
            {
                logger->error(std::string("CallContract error: ") + e.what());
            }
            return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
        }
    }

    Status MidnightGrpcServer::GetBalance(
        ServerContext* context,
        const midnight::BalanceQuery* request,
        midnight::BalanceResponse* response)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto logger = midnight::g_logger;
        const std::string& address = request->address();

        if (logger)
        {
            logger->debug("GetBalance: querying balance for " + address);
        }

        if (!node_rpc_)
        {
            if (logger)
            {
                logger->error("GetBalance: MidnightNodeRPC not configured");
            }
            return Status::OK;
        }

        try
        {
            uint64_t balance = node_rpc_->get_balance(address);
            response->set_unshielded(balance);
            response->set_dust(0);
            response->set_total(balance);

            if (logger)
            {
                logger->info("GetBalance: returned " + std::to_string(balance));
            }
        }
        catch (const std::exception& e)
        {
            if (logger)
            {
                logger->error(std::string("GetBalance error: ") + e.what());
            }
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::SubscribeBlocks(
        ServerContext* context,
        const midnight::Empty* request,
        ServerWriter<midnight::BlockHeader>* writer)
    {
        auto logger = midnight::g_logger;

        if (logger)
        {
            logger->info("SubscribeBlocks: streaming block headers");
        }

        const auto poll_interval = std::chrono::seconds(2);
        auto next_poll = std::chrono::steady_clock::now();

        while (!context->IsCancelled())
        {
            auto now = std::chrono::steady_clock::now();

            if (now >= next_poll)
            {
                std::lock_guard<std::mutex> lock(mu_);

                if (node_rpc_)
                {
                    try
                    {
                        auto [height, hash] = node_rpc_->get_chain_tip();

                        midnight::BlockHeader header;
                        header.set_height(height);
                        header.set_hash(hash);
                        header.set_timestamp(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count());

                        writer->Write(header);

                        if (logger)
                        {
                            logger->debug("SubscribeBlocks: streamed height " + std::to_string(height));
                        }
                    }
                    catch (const std::exception& e)
                    {
                        if (logger)
                        {
                            logger->warn(std::string("SubscribeBlocks poll error: ") + e.what());
                        }
                    }
                }

                next_poll = now + poll_interval;
            }

            // Small sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (logger)
        {
            logger->info("SubscribeBlocks: stream ended");
        }

        return Status::OK;
    }

    Status MidnightGrpcServer::WatchTransactions(
        ServerContext* context,
        const midnight::UtxoQuery* request,
        ServerWriter<midnight::TxResponse>* writer)
    {
        auto logger = midnight::g_logger;
        const std::string& address = request->address();

        if (logger)
        {
            logger->info("WatchTransactions: watching transactions for " + address);
        }

        const auto poll_interval = std::chrono::seconds(5);
        auto next_poll = std::chrono::steady_clock::now();
        std::set<std::string> seen_txs;

        while (!context->IsCancelled())
        {
            auto now = std::chrono::steady_clock::now();

            if (now >= next_poll)
            {
                std::lock_guard<std::mutex> lock(mu_);

                if (indexer_)
                {
                    try
                    {
                        auto utxos = indexer_->query_utxos(address);

                        for (const auto& utxo : utxos)
                        {
                            if (seen_txs.find(utxo.tx_hash) == seen_txs.end())
                            {
                                seen_txs.insert(utxo.tx_hash);

                                midnight::TxResponse tx_response;
                                tx_response.set_tx_hash(utxo.tx_hash);
                                tx_response.set_block_height(utxo.block_height);
                                tx_response.set_confirmations(1);
                                tx_response.set_finalized(true);

                                writer->Write(tx_response);

                                if (logger)
                                {
                                    logger->debug("WatchTransactions: new tx " + utxo.tx_hash);
                                }
                            }
                        }
                    }
                    catch (const std::exception& e)
                    {
                        if (logger)
                        {
                            logger->warn(std::string("WatchTransactions poll error: ") + e.what());
                        }
                    }
                }

                next_poll = now + poll_interval;
            }

            // Small sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (logger)
        {
            logger->info("WatchTransactions: stream ended");
        }

        return Status::OK;
    }

} // namespace midnight::protocols::grpc
