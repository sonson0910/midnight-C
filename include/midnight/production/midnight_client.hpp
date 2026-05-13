#pragma once

#include "midnight/network/indexer_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/ledger/transaction_builder.hpp"
#include "midnight/zk/proof_server_client.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace midnight::production
{
    using json = nlohmann::json;

    struct ClientConfig
    {
        std::string node_url;
        std::string indexer_url;
        std::string proof_server_url;
        uint32_t node_timeout_ms = 10000;
        uint32_t proof_timeout_ms = 120000;
    };

    struct SubmitResult
    {
        bool success = false;
        std::string extrinsic_hash;
        std::string error;
    };

    struct BuildSubmitResult
    {
        bool success = false;
        ledger::BuildResult build;
        SubmitResult submit;
        std::string error;
    };

    /**
     * @brief Production-safe Midnight network client.
     *
     * This class intentionally accepts only canonical bytes produced by the
     * Midnight ledger/Compact toolchain. It does not construct ledger
     * transactions, proofs, or Compact deploy/call payloads locally.
     */
    class MidnightClient
    {
    public:
        explicit MidnightClient(const ClientConfig &config);

        /**
         * @brief Submit midnight-ledger serialized transaction bytes.
         *
         * The bytes are wrapped in RuntimeCall::Midnight(send_mn_transaction)
         * as an unsigned extrinsic and submitted through author_submitExtrinsic.
         */
        SubmitResult submit_midnight_transaction(const std::vector<uint8_t> &transaction_bytes);
        SubmitResult submit_midnight_transaction_hex(const std::string &transaction_hex);

        /**
         * @brief Submit an already SCALE-encoded extrinsic.
         */
        SubmitResult submit_extrinsic_hex(const std::string &extrinsic_hex);

        /**
         * @brief Build and submit a NIGHT transfer using official ledger tooling.
         */
        BuildSubmitResult transfer_night(
            const ledger::ToolkitConfig &toolkit,
            const ledger::TransferNightParams &params);

        /**
         * @brief Build and submit DUST address registration.
         */
        BuildSubmitResult register_dust(
            const ledger::ToolkitConfig &toolkit,
            const ledger::RegisterDustParams &params);

        BuildSubmitResult deregister_dust(
            const ledger::ToolkitConfig &toolkit,
            const ledger::DeregisterDustParams &params);

        /**
         * @brief Build and submit toolkit built-in Compact contract deployment.
         */
        BuildSubmitResult deploy_simple_contract(
            const ledger::ToolkitConfig &toolkit,
            const ledger::SimpleContractDeployParams &params);

        /**
         * @brief Build and submit toolkit built-in Compact contract call.
         */
        BuildSubmitResult call_simple_contract(
            const ledger::ToolkitConfig &toolkit,
            const ledger::SimpleContractCallParams &params);

        /**
         * @brief Build and submit a custom Compact transaction from intent files.
         */
        BuildSubmitResult submit_custom_contract_intents(
            const ledger::ToolkitConfig &toolkit,
            const ledger::CustomContractIntentParams &params);

        std::vector<uint8_t> check_payload(const std::vector<uint8_t> &check_payload);
        std::vector<uint8_t> prove_payload(const std::vector<uint8_t> &proving_payload);
        std::vector<uint8_t> prove_transaction_payload(const std::vector<uint8_t> &prove_tx_payload);

        json query_transaction(const std::string &tx_hash);
        json query_block(uint64_t height);
        json query_block(const std::string &block_hash);
        network::WalletState query_wallet_state(const std::string &address);
        json query_contract_state(const std::string &contract_address_hex);

    private:
        ClientConfig config_;
        std::unique_ptr<network::MidnightNodeRPC> node_;
        std::unique_ptr<network::IndexerClient> indexer_;
        std::unique_ptr<zk::ProofServerClient> proof_server_;
    };

} // namespace midnight::production
