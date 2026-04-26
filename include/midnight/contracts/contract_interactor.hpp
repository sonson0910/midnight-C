#pragma once

#include "midnight/network/indexer_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/zk/proof_server_client.hpp"
#include "midnight/zk/proof_types.hpp"
#include "midnight/crypto/ed25519_signer.hpp"
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
     * @brief Native contract interaction engine
     *
     * Replaces the Node.js bridge (`official_sdk_bridge.cpp` → popen("node ..."))
     * with pure C++ calls to the Midnight network:
     *
     *   1. Read contract state → Indexer (GraphQL, native HTTP)
     *   2. Build circuit inputs → local C++
     *   3. Generate ZK proof → Proof Server (native HTTP)
     *   4. Build transaction → native UTXO builder
     *   5. Sign → native Ed25519 (libsodium)
     *   6. Submit → Node (JSON-RPC, native HTTP)
     *
     * Architecture (following cardano-c pattern):
     * @code
     *   ContractInteractor interactor(NetworkConfig::preprod());
     *   interactor.set_seed_hex("deadbeef...");
     *
     *   // Deploy
     *   auto deploy = interactor.deploy("Vesting", constructor_args, zk_config_path);
     *
     *   // Call
     *   auto result = interactor.call_circuit(deploy.contract_address, "lock", {});
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
         * @brief Deploy a Compact contract
         *
         * Full native flow:
         *   1. Derive wallet address from seed
         *   2. Load ZK circuit config from zk_config_path
         *   3. Generate deployment proof via Proof Server
         *   4. Build + sign deployment transaction
         *   5. Submit to network
         *   6. Wait for confirmation
         *
         * @param contract_name Name of the contract (e.g., "Vesting")
         * @param constructor_args JSON array of constructor arguments
         * @param zk_config_path Path to managed ZK config directory
         * @return DeployResult with contract address or error
         */
        DeployResult deploy(
            const std::string &contract_name,
            const json &constructor_args,
            const std::string &zk_config_path);

        /**
         * @brief Call a circuit on a deployed contract (state-changing)
         *
         * Flow:
         *   1. Read current contract state from indexer
         *   2. Build circuit inputs
         *   3. Generate ZK proof via Proof Server
         *   4. Build + sign transaction
         *   5. Submit to network
         *
         * @param contract_address Address of deployed contract
         * @param circuit_name Name of circuit to call (e.g., "lock", "claim")
         * @param args Circuit arguments as JSON
         * @return CallResult with txid or error
         */
        CallResult call_circuit(
            const std::string &contract_address,
            const std::string &circuit_name,
            const json &args = json::array());

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
         * @brief Native NIGHT transfer (no Node.js)
         *
         * @param to_address Recipient address
         * @param amount Amount in base units
         * @return CallResult with txid or error
         */
        CallResult transfer_night(
            const std::string &to_address,
            const std::string &amount);

        /**
         * @brief Native DUST registration (no Node.js)
         *
         * @return CallResult with registration status
         */
        CallResult register_dust();

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

        /**
         * @brief Sign and submit a proof-enabled transaction
         */
        std::string sign_and_submit(const zk::ProofEnabledTransaction &tx);
    };

} // namespace midnight::contracts
