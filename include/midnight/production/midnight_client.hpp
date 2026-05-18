#pragma once

#include "midnight/network/indexer_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/network/network_config.hpp"
#include "midnight/ledger/ledger_backend.hpp"
#include "midnight/production/artifact_manager.hpp"
#include "midnight/production/errors.hpp"
#include "midnight/production/state_backend.hpp"
#include "midnight/zk/proof_server_client.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace midnight::production
{
    using json = nlohmann::json;

    struct ClientConfig
    {
        std::string network_name = "preview";
        std::string node_url;
        std::string indexer_url;
        std::string proof_server_url;
        std::string ledger_ffi_library;
        uint32_t node_timeout_ms = 10000;
        uint32_t proof_timeout_ms = 120000;
        StateBackendMode state_backend_mode = StateBackendMode::LocalCache;
        ManagedStateServiceConfig managed_state;

        /**
         * @brief Build canonical service URLs for a named Midnight network.
         */
        static ClientConfig for_network(
            network::NetworkConfig::Network network =
                network::NetworkConfig::Network::PREVIEW);

        /**
         * @brief Build config from MIDNIGHT_* environment variables.
         *
         * Reads MIDNIGHT_NETWORK, MIDNIGHT_NODE_URL, MIDNIGHT_INDEXER_URL,
         * MIDNIGHT_PROOF_SERVER_URL, MIDNIGHT_LEDGER_FFI_LIBRARY,
         * MIDNIGHT_NODE_TIMEOUT_MS, MIDNIGHT_PROOF_TIMEOUT_MS,
         * MIDNIGHT_STATE_MODE, MIDNIGHT_STATE_SERVICE_URL,
         * MIDNIGHT_STATE_SERVICE_API_KEY, and MIDNIGHT_STATE_SERVICE_TIMEOUT_MS.
         */
        static ClientConfig from_environment();
    };

    struct SubmitResult
    {
        bool success = false;
        std::string extrinsic_hash;
        std::string error;
        SdkError error_info;
    };

    struct BuildSubmitResult
    {
        bool success = false;
        ledger::BuildResult build;
        SubmitResult submit;
        std::string error;
        SdkError error_info;
    };

    struct ConfirmationConfig
    {
        std::chrono::milliseconds timeout{120000};
        std::chrono::milliseconds poll_interval{2000};
    };

    struct ConfirmationResult
    {
        bool success = false;
        bool found = false;
        std::string transaction_hash;
        uint64_t transaction_id = 0;
        uint64_t block_height = 0;
        std::string block_hash;
        json transaction = json::object();
        SdkError error;
    };

    struct PipelineOptions
    {
        ConfirmationConfig confirmation;
        ArtifactConfig artifacts;
        bool save_artifacts = true;
        bool wait_for_confirmation = true;
    };

    struct PipelineResult
    {
        bool success = false;
        BuildSubmitResult transaction;
        ConfirmationResult confirmation;
        ArtifactSaveResult artifacts;
        SdkError error;
    };

    /**
     * @brief Production-safe Midnight network client.
     *
     * This class intentionally accepts only canonical bytes produced by the
     * Midnight ledger/Compact backend. The C++ layer owns transport,
     * validation, artifact persistence, submission, and confirmation; ledger
     * transaction construction is delegated to an explicit ILedgerBackend so
     * production code does not depend on CLI/toolkit process execution.
     */
    class MidnightClient
    {
    public:
        explicit MidnightClient(const ClientConfig &config);
        MidnightClient(
            const ClientConfig &config,
            std::shared_ptr<ledger::ILedgerBackend> ledger_backend);

        const ClientConfig &config() const { return config_; }
        void set_ledger_backend(std::shared_ptr<ledger::ILedgerBackend> ledger_backend);
        void set_state_backend(std::shared_ptr<IStateBackend> state_backend);

        /**
         * @brief Check node, indexer, proof-server, and ledger backend readiness.
         *
         * Returns structured status for SDK boot diagnostics. Network checks are
         * best-effort and do not throw.
         */
        json health_check();

        /**
         * @brief Native ledger backend version/capability metadata.
         */
        ledger::LedgerBackendInfo ledger_backend_info();

        /**
         * @brief Validate address/token/seed formats with native ledger parsers.
         */
        ledger::ValidationResult validate_ledger_value(
            const std::string &kind,
            const std::string &value,
            const std::string &network = {});

        /**
         * @brief Hash/verify a ledger-serialized Midnight transaction.
         */
        ledger::TransactionInspectionResult inspect_transaction_hex(
            const std::string &transaction_hex,
            uint8_t ledger_version = 8);

        /**
         * @brief Derive canonical Midnight wallet addresses through ledger FFI.
         */
        ledger::WalletAddressDerivationResult derive_wallet_addresses(
            const std::string &seed_hex_or_mnemonic,
            const std::string &network = {});

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
         * @brief Build/prove canonical Midnight transactions without submitting.
         *
         * These methods call the configured native ledger backend and return
         * ledger-serialized Midnight transaction bytes plus metadata. Use
         * submit_build_result() to submit a successful BuildResult later.
         */
        ledger::BuildResult build_transfer_night(const ledger::TransferNightParams &params);
        ledger::BuildResult build_register_dust(const ledger::RegisterDustParams &params);
        ledger::BuildResult build_deregister_dust(const ledger::DeregisterDustParams &params);
        ledger::BuildResult build_simple_contract_deploy(const ledger::SimpleContractDeployParams &params);
        ledger::BuildResult build_simple_contract_call(const ledger::SimpleContractCallParams &params);
        ledger::BuildResult build_custom_contract_transaction(const ledger::CustomContractIntentParams &params);
        ledger::BuildResult sync_ledger_state(const ledger::SyncLedgerStateParams &params);

        /**
         * @brief Inspect local or managed wallet ledger-state readiness.
         *
         * A ready state means transaction building can run in LocalCache mode
         * without secretly performing a long full-source fetch.
         */
        StateStatus state_status(
            const ledger::SourceConfig &source,
            uint64_t max_lag_blocks = 100);

        /**
         * @brief Refresh the configured state backend.
         *
         * Local backend calls the native ledger FFI sync operation. Managed
         * backend calls a state service that owns the sync workload.
         */
        StateRefreshResult refresh_state(
            const ledger::SyncLedgerStateParams &params,
            const StateRefreshOptions &options = {});

        /**
         * @brief Ensure state is usable before build/submit workflows.
         */
        StateRefreshResult ensure_state_fresh(
            const ledger::SyncLedgerStateParams &params,
            const StateRefreshOptions &options = {});

        SnapshotResult export_state_snapshot(
            const ledger::SourceConfig &source,
            const std::filesystem::path &snapshot_dir);

        SnapshotResult import_state_snapshot(
            const std::filesystem::path &snapshot_dir,
            const ledger::SourceConfig &destination);

        BuildSubmitResult submit_build_result(ledger::BuildResult build);

        /**
         * @brief Verify that a built transaction's ledger context is canonical
         * on the configured node before submission.
         *
         * Returns an empty SdkError on success or when the build result does not
         * expose context metadata. A non-empty error means submitting this
         * transaction would likely be rejected by the node.
         */
        SdkError validate_built_transaction_context(const ledger::BuildResult &build);

        /**
         * @brief Build and submit a NIGHT transfer using configured native ledger backend.
         */
        BuildSubmitResult transfer_night(const ledger::TransferNightParams &params);

        /**
         * @brief Build and submit DUST address registration using configured native ledger backend.
         */
        BuildSubmitResult register_dust(const ledger::RegisterDustParams &params);

        BuildSubmitResult deregister_dust(const ledger::DeregisterDustParams &params);

        BuildSubmitResult deploy_simple_contract(const ledger::SimpleContractDeployParams &params);
        BuildSubmitResult call_simple_contract(const ledger::SimpleContractCallParams &params);
        BuildSubmitResult submit_custom_contract_intents(const ledger::CustomContractIntentParams &params);

        PipelineResult transfer_night(
            const PipelineOptions &options,
            const ledger::TransferNightParams &params);

        PipelineResult register_dust(
            const PipelineOptions &options,
            const ledger::RegisterDustParams &params);

        PipelineResult deregister_dust(
            const PipelineOptions &options,
            const ledger::DeregisterDustParams &params);

        PipelineResult deploy_simple_contract(
            const PipelineOptions &options,
            const ledger::SimpleContractDeployParams &params);

        PipelineResult call_simple_contract(
            const PipelineOptions &options,
            const ledger::SimpleContractCallParams &params);

        PipelineResult submit_custom_contract_intents(
            const PipelineOptions &options,
            const ledger::CustomContractIntentParams &params);

        ConfirmationResult wait_for_transaction(
            const std::string &transaction_hash,
            const ConfirmationConfig &confirmation = {});
        ConfirmationResult wait_for_inclusion(
            const std::string &transaction_hash,
            const ConfirmationConfig &confirmation = {});
        ConfirmationResult wait_for_finality(
            const std::string &transaction_hash,
            const ConfirmationConfig &confirmation = {});

        std::vector<uint8_t> check_payload(const std::vector<uint8_t> &check_payload);
        std::vector<uint8_t> prove_payload(const std::vector<uint8_t> &proving_payload);
        std::vector<uint8_t> prove_transaction_payload(const std::vector<uint8_t> &prove_tx_payload);

        json query_transaction(const std::string &tx_hash);
        json query_block(uint64_t height);
        json query_block(const std::string &block_hash);
        network::WalletState query_wallet_state(const std::string &address);
        network::WalletState query_wallet_state(
            const std::string &address,
            uint32_t from_block,
            uint32_t to_block);
        network::WalletState query_wallet_state_from_transaction(
            const std::string &address,
            const std::string &tx_hash);
        json query_cached_wallet_summary(const ledger::WalletSummaryParams &params);
        json query_contract_state(const std::string &contract_address_hex);

    private:
        ClientConfig config_;
        std::unique_ptr<network::MidnightNodeRPC> node_;
        std::unique_ptr<network::IndexerClient> indexer_;
        std::unique_ptr<zk::ProofServerClient> proof_server_;
        std::shared_ptr<ledger::ILedgerBackend> ledger_backend_;
        std::shared_ptr<IStateBackend> state_backend_;
    };

} // namespace midnight::production
