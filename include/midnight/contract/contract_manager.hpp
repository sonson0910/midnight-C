#pragma once

#include "midnight/network/substrate_rpc.hpp"
#include "midnight/network/indexer_client.hpp"
#include "midnight/zk/proof_server_client.hpp"
#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/tx/extrinsic_builder.hpp"
#include "midnight/codec/scale_codec.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>

namespace midnight::contract
{

    using json = nlohmann::json;

    // ─── Contract Artifact Types ──────────────────────────────

    /**
     * @brief Compiled Compact contract artifacts
     *
     * Contains all outputs from `compact compile`:
     *   - Contract bytecode (WASM/zkIR)
     *   - Circuit definitions (verifier keys, prover keys)
     *   - Initial state specification
     */
    struct ContractArtifact
    {
        std::string name;                      ///< Contract name (e.g., "FaucetAMM")
        std::vector<uint8_t> bytecode;         ///< Contract WASM/zkIR bytecode
        json metadata;                         ///< Compilation metadata

        /// Circuit definitions (circuit_name -> prover key bytes)
        std::map<std::string, std::vector<uint8_t>> circuits;

        /// Verifier keys per circuit
        std::map<std::string, std::vector<uint8_t>> verifier_keys;

        /// ZK config data for proof provider initialization
        std::vector<uint8_t> zk_config;

        /// Load artifact from compiled output directory
        static ContractArtifact load_from_dir(const std::string &dir_path);

        /// Serialize to JSON
        json to_json() const;

        /// Deserialize from JSON
        static ContractArtifact from_json(const json &j);
    };

    // ─── Deployment Types ─────────────────────────────────────

    /**
     * @brief Contract deployment parameters
     */
    struct DeployParams
    {
        ContractArtifact artifact;                 ///< Compiled contract
        std::vector<uint8_t> constructor_args;     ///< SCALE-encoded constructor args
        uint64_t gas_limit = 5000000000;           ///< Gas limit for deployment
        uint64_t storage_deposit_limit = 0;        ///< Storage deposit (0 = unlimited)
        uint64_t value = 0;                        ///< Value to transfer with deployment
        uint64_t tip = 0;                          ///< Priority tip
    };

    /**
     * @brief Deployment result
     */
    struct DeployResult
    {
        bool success = false;
        std::string tx_hash;                   ///< Deployment transaction hash
        std::string contract_address;          ///< Deployed contract address
        uint64_t block_number = 0;             ///< Block where deployment was included
        std::string error;
    };

    // ─── Call Types ───────────────────────────────────────────

    /**
     * @brief Contract call parameters
     */
    struct CallParams
    {
        std::string contract_address;          ///< Target contract address
        std::string circuit_name;              ///< Circuit/function name to call
        std::vector<uint8_t> call_data;        ///< SCALE-encoded call arguments
        uint64_t gas_limit = 5000000000;       ///< Gas limit
        uint64_t value = 0;                    ///< Value to send with call
        uint64_t tip = 0;                      ///< Priority tip

        /// Optional: proof data if pre-generated
        std::optional<std::vector<uint8_t>> proof;
    };

    /**
     * @brief Contract call result
     */
    struct CallResult
    {
        bool success = false;
        std::string tx_hash;                   ///< Transaction hash
        json return_data;                      ///< Decoded return data
        json events;                           ///< Emitted events
        uint64_t gas_used = 0;                 ///< Gas consumed
        std::string error;
    };

    // ─── Contract State ───────────────────────────────────────

    /**
     * @brief On-chain contract state
     */
    struct ContractInfo
    {
        std::string address;                   ///< Contract address
        bool exists = false;                   ///< Whether contract exists on-chain
        json ledger_state;                     ///< Public ledger state (from indexer)
        uint64_t block_height = 0;             ///< Block height of state query
        std::string error;
    };

    // ─── Contract Manager ─────────────────────────────────────

    /**
     * @brief Midnight contract lifecycle manager
     *
     * Provides a unified C++ API for the full contract lifecycle:
     *   1. Deploy    — compile artifacts + construct deployment extrinsic
     *   2. Call      — invoke circuit functions with ZK proof generation
     *   3. Query     — read contract state from indexer or direct RPC
     *   4. Monitor   — wait for finalization, poll events
     *
     * Integrates:
     *   - SubstrateRPC for extrinsic submission
     *   - ProofServerClient for ZK proof generation
     *   - IndexerClient for state queries
     *   - ExtrinsicBuilder + HDWallet for native signing
     *
     * Example:
     * ```cpp
     * ContractManager mgr(
     *     "https://rpc.preview.midnight.network",
     *     "http://localhost:6300",
     *     "https://indexer.preview.midnight.network/graphql"
     * );
     *
     * auto wallet = HDWallet::from_mnemonic("...");
     * auto key = wallet.derive_night(0, 0);
     *
     * // Deploy
     * auto artifact = ContractArtifact::load_from_dir("./dist/my-contract");
     * DeployParams dp{.artifact = artifact, .constructor_args = {...}};
     * auto deploy_result = mgr.deploy(dp, key);
     *
     * // Call
     * CallParams cp{.contract_address = deploy_result.contract_address,
     *               .circuit_name = "mint", .call_data = {...}};
     * auto call_result = mgr.call(cp, key);
     *
     * // Query
     * auto state = mgr.query_state(deploy_result.contract_address);
     * ```
     */
    class ContractManager
    {
    public:
        /**
         * @brief Construct contract manager with all service endpoints
         * @param node_rpc_url   Midnight node JSON-RPC endpoint
         * @param proof_url      Proof Server HTTP endpoint
         * @param indexer_url    Indexer GraphQL endpoint
         */
        ContractManager(const std::string &node_rpc_url,
                        const std::string &proof_url,
                        const std::string &indexer_url);

        // ─── Deployment ───────────────────────────────────────

        /**
         * @brief Deploy a compiled contract to the network
         *
         * Workflow:
         *   1. Build contracts.instantiateWithCode extrinsic
         *   2. Auto-fetch runtime version + nonce
         *   3. Sign with provided key pair
         *   4. Submit via author_submitExtrinsic
         *   5. Optionally wait for finalization
         *
         * @param params Deployment parameters (artifact + gas + args)
         * @param signer Key pair for signing the deployment transaction
         * @param wait_finalized If true, block until finalized
         * @return DeployResult with tx_hash and contract_address
         */
        DeployResult deploy(const DeployParams &params,
                            const wallet::KeyPair &signer,
                            bool wait_finalized = true);

        // ─── Contract Calls ───────────────────────────────────

        /**
         * @brief Call a contract circuit function
         *
         * Workflow:
         *   1. Generate ZK proof via Proof Server (if not pre-provided)
         *   2. Build contracts.call extrinsic
         *   3. Sign and submit
         *
         * @param params Call parameters (address + circuit + data)
         * @param signer Key pair for signing
         * @param wait_finalized If true, block until finalized
         * @return CallResult with tx_hash and decoded return data
         */
        CallResult call(const CallParams &params,
                        const wallet::KeyPair &signer,
                        bool wait_finalized = true);

        /**
         * @brief Dry-run a contract call (estimate gas, no state change)
         *
         * Uses contracts_call RPC to simulate execution without submission.
         */
        CallResult dry_run(const CallParams &params,
                           const wallet::KeyPair &signer);

        // ─── State Queries ────────────────────────────────────

        /**
         * @brief Query contract state from indexer
         * @param contract_address Contract address (hex)
         * @return ContractInfo with ledger state
         */
        ContractInfo query_state(const std::string &contract_address);

        /**
         * @brief Query specific fields from contract state
         * @param contract_address Contract address
         * @param fields List of field names to retrieve
         * @return JSON object with requested field values
         */
        json query_fields(const std::string &contract_address,
                          const std::vector<std::string> &fields);

        /**
         * @brief Query contract state directly from RPC storage
         * @param contract_address Contract address
         * @return Raw SCALE-encoded state bytes
         */
        std::string query_raw_state(const std::string &contract_address);

        // ─── Proof Generation ─────────────────────────────────

        /**
         * @brief Generate a ZK proof for a contract call
         *
         * Uses the Proof Server's /prove-tx endpoint.
         *
         * @param params Call parameters (contract + circuit + inputs)
         * @param artifact Contract artifact with circuit definitions
         * @return Proof bytes, or empty on failure
         */
        std::vector<uint8_t> generate_proof(
            const CallParams &params,
            const ContractArtifact &artifact);

        // ─── Utilities ────────────────────────────────────────

        /**
         * @brief Get SubstrateRPC client for direct access
         */
        network::SubstrateRPC &rpc() { return *rpc_; }

        /**
         * @brief Get IndexerClient for direct access
         */
        network::IndexerClient &indexer() { return *indexer_; }

        /**
         * @brief Get ProofServerClient for direct access
         */
        zk::ProofServerClient &proof_server() { return *proof_server_; }

        /**
         * @brief Verify all services are reachable
         * @return JSON with status of each endpoint
         */
        json health_check();

        /**
         * @brief Encode constructor arguments for deployment
         *
         * Convenience method to SCALE-encode constructor arguments
         * matching the Compact contract's constructor signature.
         *
         * @param args JSON array of arguments
         * @return SCALE-encoded bytes
         */
        static std::vector<uint8_t> encode_constructor_args(const json &args);

        /**
         * @brief Encode circuit call arguments
         * @param circuit_name Circuit function name
         * @param args JSON array of arguments
         * @return SCALE-encoded call data
         */
        static std::vector<uint8_t> encode_call_args(
            const std::string &circuit_name,
            const json &args);

    private:
        std::unique_ptr<network::SubstrateRPC> rpc_;
        std::unique_ptr<zk::ProofServerClient> proof_server_;
        std::unique_ptr<network::IndexerClient> indexer_;

        /// Build contracts pallet call for deployment
        tx::PalletCall build_deploy_call(const DeployParams &params);

        /// Build contracts pallet call for contract invocation
        tx::PalletCall build_call_call(const CallParams &params);

        /// Extract contract address from deployment events
        std::string extract_contract_address(const std::string &tx_hash);
    };

} // namespace midnight::contract
