#pragma once

#include "midnight/network/indexer_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/ledger/ledger_backend.hpp"
#include "midnight/zk/proof_server_client.hpp"
#include "midnight/zk/proof_types.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <vector>

namespace midnight::contracts
{

    using json = nlohmann::json;

    /**
     * @brief Network configuration for native contract interaction
     */
    struct NetworkConfig
    {
        std::string node_url;           ///< Midnight node JSON-RPC URL
        std::string indexer_url;        ///< Indexer GraphQL URL
        std::string proof_server_url;   ///< Proof Server URL (localhost:6300)
        std::string network_name;       ///< "preprod", "preview", "mainnet"
        std::string ledger_ffi_library; ///< Native libmidnight_ledger_ffi for canonical wallet derivation

        /// Default configurations for known networks
        static NetworkConfig preprod();
        static NetworkConfig preview();
        static NetworkConfig from_urls(const std::string &node,
                                       const std::string &indexer,
                                       const std::string &proof_server,
                                       const std::string &network = "preprod");
    };

    /**
     * @brief Result of a contract deployment
     */
    struct DeployResult
    {
        bool success = false;
        std::string contract_address;   ///< Deployed contract address
        std::string txid;               ///< Deployment transaction hash
        std::string error;
    };

    /**
     * @brief Result of a contract circuit call
     */
    struct CallResult
    {
        bool success = false;
        std::string txid;               ///< Transaction hash
        json result;                    ///< Return value (if any)
        std::string error;
    };

    /**
     * @brief Result of a read-only contract query
     */
    struct QueryResult
    {
        bool success = false;
        json result;                    ///< Circuit return value
        std::string error;
    };

    /**
     * @brief Contract interaction helper for Indexer, node RPC, and Proof Server
     *
     * Production note: read-only contract state queries are native Indexer v4
     * calls. State-changing Compact deploy/call/transfer flows require serialized
     * ledger transaction or proof payload bytes produced by the Midnight
     * ledger/Compact toolchain, then submitted through the raw payload helpers.
     *
     * Uses pure C++ calls to the Midnight network:
     *
     *   1. Read contract state → Indexer (GraphQL, native HTTP)
     *   2. Send ledger-built proof payloads → Proof Server (native HTTP)
     *   3. Submit ledger-built transactions → Node (author_submitExtrinsic)
     *
     * Architecture (following cardano-c pattern):
     * @code
     *   ContractInteractor interactor(NetworkConfig::preprod());
     *   interactor.set_seed_hex("deadbeef...");
     *
     *   // Deploy/call transaction bytes are produced by midnight-ledger/Compact.
     *   auto deploy = interactor.submit_deploy_transaction(deploy_tx_bytes);
     *
     *   // Call
     *   auto result = interactor.submit_call_transaction(call_tx_bytes);
     *
     *   // Query (read-only, no proof needed)
     *   auto state = interactor.read_state(deploy.contract_address);
     * @endcode
     */
    class ContractInteractor
    {
    public:
        /**
         * @brief Construct interactor with network configuration
         * @param config Network URLs and settings
         */
        explicit ContractInteractor(const NetworkConfig &config);

        /**
         * @brief Set wallet seed for signing transactions
         * @param seed_hex 32-byte seed in hex
         */
        void set_seed_hex(const std::string &seed_hex);

        /**
         * @brief Submit a ledger-built Compact deployment transaction.
         */
        DeployResult submit_deploy_transaction(const std::vector<uint8_t> &transaction_bytes);

        /**
         * @brief Submit a ledger-built Compact state-changing call transaction.
         */
        CallResult submit_call_transaction(const std::vector<uint8_t> &transaction_bytes);

        /**
         * @brief Read contract state (no proof, no transaction)
         *
         * Pure read-only query via indexer GraphQL.
         *
         * @param contract_address Contract address
         * @return JSON with all exported ledger fields
         */
        json read_state(const std::string &contract_address);

        /**
         * @brief Read specific contract fields
         * @param contract_address Contract address
         * @param fields List of field names
         * @return JSON with requested fields
         */
        json read_fields(const std::string &contract_address,
                         const std::vector<std::string> &fields);

        /**
         * @brief Submit a ledger-built NIGHT transfer transaction.
         */
        CallResult submit_transfer_transaction(const std::vector<uint8_t> &transaction_bytes);

        /**
         * @brief Submit a ledger-serialized transaction to the node.
         *
         * The input must be real Midnight transaction bytes produced by the
         * ledger/wallet builder. This method hex-encodes the bytes and submits
         * them via the node RPC transaction endpoint.
         */
        CallResult submit_serialized_transaction(const std::vector<uint8_t> &transaction_bytes);

        /**
         * @brief POST a ledger createProvingPayload(...) body to /prove.
         */
        std::vector<uint8_t> prove_contract_payload(const std::vector<uint8_t> &proving_payload);

        /**
         * @brief POST a ledger createCheckPayload(...) body to /check.
         */
        std::vector<uint8_t> check_contract_payload(const std::vector<uint8_t> &check_payload);

        /**
         * @brief POST a ledger createProvingTransactionPayload(...) body to /prove-tx.
         */
        std::vector<uint8_t> prove_transaction_payload(const std::vector<uint8_t> &prove_tx_payload);

        /**
         * @brief Submit a ledger-built DUST registration transaction.
         */
        CallResult submit_dust_registration_transaction(const std::vector<uint8_t> &transaction_bytes);

        /**
         * @brief Query DUST status
         * @return DustRegistrationStatus
         */
        network::DustRegistrationStatus query_dust_status();

        /**
         * @brief Get wallet balance
         * @return WalletState with balances
         */
        network::WalletState get_wallet_state();

        /**
         * @brief Get wallet state from a known funding/transaction hash.
         *
         * This is the fast production path for faucet or recently observed
         * wallet activity. It avoids historical full-chain UTXO scans.
         */
        network::WalletState get_wallet_state_from_transaction(const std::string &tx_hash);

        /**
         * @brief Get wallet address derived from current seed
         */
        std::string get_wallet_address() const;

        /**
         * @brief Check if all network services are reachable
         * @return JSON with status of each service
         */
        json health_check();

    private:
        NetworkConfig config_;
        std::string seed_hex_;
        std::string derived_address_;

        // Native clients — no Node.js dependency
        std::unique_ptr<network::IndexerClient> indexer_;
        std::unique_ptr<network::MidnightNodeRPC> rpc_;
        std::unique_ptr<zk::ProofServerClient> proof_server_;
        std::shared_ptr<ledger::ILedgerBackend> ledger_backend_;

        /**
         * @brief Derive keypair from seed
         */
        void derive_wallet();

        /**
         * @brief Load circuit data from ZK config directory
         */
        std::vector<uint8_t> load_circuit_data(
            const std::string &zk_config_path,
            const std::string &circuit_name);

        /**
         * @brief Build circuit public inputs from args and state
         */
        zk::PublicInputs build_public_inputs(
            const std::string &circuit_name,
            const json &args,
            const json &current_state);

    };

} // namespace midnight::contracts
