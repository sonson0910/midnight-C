#include "midnight/protocols/grpc/grpc_client.hpp"
#include "midnight/core/logger.hpp"

#include <grpc++/grpc++.h>
#include <grpc++/channel.h>

namespace midnight::protocols::grpc
{

    using grpc::Channel;
    using grpc::ClientContext;
    using grpc::Status;
    using grpc::ClientReader;

    // ── Impl ─────────────────────────────────────────────────────

    class MidnightGrpcClient::Impl
    {
    public:
        explicit Impl(const std::string& address)
            : channel_(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()))
            , stub_(midnight::MidnightNode::NewStub(channel_))
        {}

        bool is_connected() const
        {
            auto state = channel_->GetState(false);
            return state == grpc::ChannelReady || state == grpc::CONNECTING;
        }

        std::unique_ptr<grpc::Channel> channel_;
        std::unique_ptr<midnight::MidnightNode::Stub> stub_;

        // Streaming state
        std::atomic<bool> streaming_{false};
        std::unique_ptr<ClientReader<midnight::BlockHeader>> block_reader_;
        std::unique_ptr<ClientReader<midnight::TxResponse>> tx_reader_;
        std::unique_ptr<ClientContext> block_context_{std::make_unique<ClientContext>()};
        std::unique_ptr<ClientContext> tx_context_{std::make_unique<ClientContext>()};
        std::thread reader_thread_;
        mutable std::mutex mu_;
        std::string last_error_;
    };

    // ── Client ─────────────────────────────────────────────────

    MidnightGrpcClient::MidnightGrpcClient(const std::string& server_address)
        : pImpl_(std::make_unique<Impl>(server_address))
    {
        auto logger = midnight::g_logger;
        if (logger)
        {
            logger->info("MidnightGrpcClient: connecting to " + server_address);
        }
    }

    MidnightGrpcClient::~MidnightGrpcClient()
    {
        cancel_subscriptions();
    }

    bool MidnightGrpcClient::is_connected() const
    {
        return pImpl_->is_connected();
    }

    // ── Transaction Operations ───────────────────────────────────

    std::string MidnightGrpcClient::submit_transaction(const midnight::SignedTransaction& tx)
    {
        midnight::SubmitTxRequest request;
        *request.mutable_transaction() = tx;

        midnight::SubmitTxResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->SubmitTransaction(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("SubmitTransaction failed: " + status.error_message());
            }
            return {};
        }

        if (!response.success())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = response.error();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("SubmitTransaction error: " + response.error());
            }
            return {};
        }

        return response.tx_hash();
    }

    midnight::TxResponse MidnightGrpcClient::query_transaction(const std::string& tx_hash)
    {
        midnight::TxQuery request;
        request.set_tx_hash(tx_hash);

        midnight::TxResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->QueryTransaction(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("QueryTransaction failed: " + status.error_message());
            }
        }

        return response;
    }

    // ── UTXO Operations ────────────────────────────────────────

    midnight::UtxoResponse MidnightGrpcClient::query_utxos(const std::string& address,
                                                          uint32_t limit,
                                                          uint64_t after_block)
    {
        midnight::UtxoQuery request;
        request.set_address(address);
        request.set_limit(limit);
        request.set_after_block(after_block);

        midnight::UtxoResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->QueryUTXOs(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("QueryUTXOs failed: " + status.error_message());
            }
        }

        return response;
    }

    // ── Block Operations ────────────────────────────────────────

    midnight::BlockResponse MidnightGrpcClient::get_block(uint32_t height)
    {
        midnight::BlockQuery request;
        request.set_by_height(height);

        midnight::BlockResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->GetBlock(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("GetBlock(height=" + std::to_string(height) + ") failed: " + status.error_message());
            }
        }

        return response;
    }

    midnight::BlockResponse MidnightGrpcClient::get_block(const std::string& hash)
    {
        midnight::BlockQuery request;
        request.set_by_hash(hash);

        midnight::BlockResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->GetBlock(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("GetBlock(hash=" + hash + ") failed: " + status.error_message());
            }
        }

        return response;
    }

    midnight::ProtocolParamsResponse MidnightGrpcClient::get_protocol_params()
    {
        midnight::ProtocolParamsRequest request;
        midnight::ProtocolParamsResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->GetProtocolParams(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("GetProtocolParams failed: " + status.error_message());
            }
        }

        return response;
    }

    midnight::NodeInfoResponse MidnightGrpcClient::get_node_info()
    {
        midnight::NodeInfoRequest request;
        midnight::NodeInfoResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->GetNodeInfo(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("GetNodeInfo failed: " + status.error_message());
            }
        }

        return response;
    }

    // ── Contract Operations ────────────────────────────────────

    midnight::ContractResponse MidnightGrpcClient::deploy_contract(
        const std::vector<uint8_t>& code,
        const std::vector<uint8_t>& constructor_args,
        uint64_t gas_limit,
        uint64_t storage_limit,
        uint64_t value)
    {
        midnight::ContractDeployRequest request;
        request.set_code(code.data(), code.size());
        request.set_constructor_args(constructor_args.data(), constructor_args.size());
        request.set_gas_limit(gas_limit);
        request.set_storage_limit(storage_limit);
        request.set_value(value);

        midnight::ContractResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->DeployContract(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("DeployContract failed: " + status.error_message());
            }
        }

        return response;
    }

    midnight::ContractResponse MidnightGrpcClient::call_contract(
        const std::string& contract_address,
        const std::string& circuit_name,
        const std::vector<uint8_t>& call_data,
        uint64_t gas_limit,
        uint64_t value)
    {
        midnight::ContractCallRequest request;
        request.set_contract_address(contract_address);
        request.set_circuit_name(circuit_name);
        request.set_call_data(call_data.data(), call_data.size());
        request.set_gas_limit(gas_limit);
        request.set_value(value);

        midnight::ContractResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->CallContract(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("CallContract failed: " + status.error_message());
            }
        }

        return response;
    }

    // ── Balance ────────────────────────────────────────────────

    midnight::BalanceResponse MidnightGrpcClient::get_balance(const std::string& address)
    {
        midnight::BalanceQuery request;
        request.set_address(address);

        midnight::BalanceResponse response;
        ClientContext context;

        auto stub = pImpl_->stub_.get();
        Status status = stub->GetBalance(&context, request, &response);

        if (!status.ok())
        {
            std::lock_guard<std::mutex> lock(pImpl_->mu);
            pImpl_->last_error_ = status.error_message();
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->error("GetBalance failed: " + status.error_message());
            }
        }

        return response;
    }

    // ── Streaming ─────────────────────────────────────────────

    void MidnightGrpcClient::subscribe_blocks(BlockCallback callback)
    {
        std::lock_guard<std::mutex> lock(pImpl_->mu);

        if (pImpl_->streaming_.load())
        {
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->warn("subscribe_blocks: already streaming, canceling previous subscription");
            }
            cancel_subscriptions();
        }

        pImpl_->streaming_.store(true);
        pImpl_->block_context_ = std::make_unique<ClientContext>();

        pImpl_->reader_thread_ = std::thread([this, cb = std::move(callback)]()
        {
            midnight::Empty request;
            midnight::BlockHeader header;

            {
                std::lock_guard<std::mutex> lock(pImpl_->mu);
                pImpl_->block_reader_ = pImpl_->stub_->SubscribeBlocks(pImpl_->block_context_.get(), request);
            }

            while (pImpl_->streaming_.load())
            {
                {
                    std::lock_guard<std::mutex> lock(pImpl_->mu);
                    if (!pImpl_->block_reader_ || !pImpl_->block_reader_->Read(&header))
                    {
                        break;
                    }
                }

                cb(header);
            }

            {
                std::lock_guard<std::mutex> lock(pImpl_->mu);
                if (pImpl_->block_context_)
                {
                    pImpl_->block_context_->TryCancel();
                }
                pImpl_->block_reader_.reset();
                pImpl_->block_context_.reset();
            }

            pImpl_->streaming_.store(false);
        });
    }

    void MidnightGrpcClient::watch_transactions(const std::string& address, TxCallback callback)
    {
        std::lock_guard<std::mutex> lock(pImpl_->mu);

        if (pImpl_->streaming_.load())
        {
            auto logger = midnight::g_logger;
            if (logger)
            {
                logger->warn("watch_transactions: already streaming, canceling previous subscription");
            }
            cancel_subscriptions();
        }

        pImpl_->streaming_.store(true);
        pImpl_->tx_context_ = std::make_unique<ClientContext>();

        pImpl_->reader_thread_ = std::thread([this, addr = address, cb = std::move(callback)]()
        {
            midnight::UtxoQuery request;
            request.set_address(addr);
            request.set_limit(100);
            request.set_after_block(0);

            midnight::TxResponse tx_response;

            {
                std::lock_guard<std::mutex> lock(pImpl_->mu);
                pImpl_->tx_reader_ = pImpl_->stub_->WatchTransactions(pImpl_->tx_context_.get(), request);
            }

            while (pImpl_->streaming_.load())
            {
                {
                    std::lock_guard<std::mutex> lock(pImpl_->mu);
                    if (!pImpl_->tx_reader_ || !pImpl_->tx_reader_->Read(&tx_response))
                    {
                        break;
                    }
                }

                cb(tx_response);
            }

            {
                std::lock_guard<std::mutex> lock(pImpl_->mu);
                if (pImpl_->tx_context_)
                {
                    pImpl_->tx_context_->TryCancel();
                }
                pImpl_->tx_reader_.reset();
                pImpl_->tx_context_.reset();
            }

            pImpl_->streaming_.store(false);
        });
    }

    void MidnightGrpcClient::cancel_subscriptions()
    {
        std::lock_guard<std::mutex> lock(pImpl_->mu);

        pImpl_->streaming_.store(false);

        if (pImpl_->block_context_)
        {
            pImpl_->block_context_->TryCancel();
        }
        if (pImpl_->tx_context_)
        {
            pImpl_->tx_context_->TryCancel();
        }

        if (pImpl_->reader_thread_.joinable())
        {
            pImpl_->reader_thread_.join();
        }

        pImpl_->block_reader_.reset();
        pImpl_->tx_reader_.reset();
        pImpl_->block_context_.reset();
        pImpl_->tx_context_.reset();
    }

} // namespace midnight::protocols::grpc
