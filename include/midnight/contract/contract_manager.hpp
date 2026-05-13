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
     * Compact deploy/call transactions must be built by midnight-ledger and
     * passed here as serialized bytes. This class handles proof-server posts,
     * node submission, and state reads without inventing JSON/CBOR payloads.
     *
     * Integrates:
     *   - SubstrateRPC for extrinsic submission
     *   - ProofServerClient for ZK proof generation
     *   - IndexerClient for state queries
     *   - unsigned Midnight::send_mn_transaction wrapping for ledger bytes
     *
     * Example:
     * ```cpp
     * ContractManager mgr(
     *     "https://rpc.preprod.midnight.network",
     *     "http://localhost:6300",
     *     "https://indexer.preprod.midnight.network/api/v4/graphql"
     * );
     *
     * auto deploy_result = mgr.submit_deploy_transaction(deploy_tx_bytes);
     * auto call_result = mgr.submit_call_transaction(call_tx_bytes);
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

        // ─── Transaction Submission ───────────────────────────

        /**
         * @brief Submit a ledger-built Compact deployment transaction.
         */
        DeployResult submit_deploy_transaction(const std::vector<uint8_t> &transaction_bytes);

        /**
         * @brief Submit a ledger-built Compact state-changing call transaction.
         */
        CallResult submit_call_transaction(const std::vector<uint8_t> &transaction_bytes);

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
         * @brief Query contract state directly from Midnight RPC
         * @param contract_address Contract address
         * @return Raw state bytes/hex returned by midnight_contractState
         */
        std::string query_raw_state(const std::string &contract_address);

        // ─── Proof Generation ─────────────────────────────────

        std::vector<uint8_t> check_payload(const std::vector<uint8_t> &payload);
        std::vector<uint8_t> prove_payload(const std::vector<uint8_t> &payload);
        std::vector<uint8_t> prove_transaction_payload(const std::vector<uint8_t> &payload);

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

    };

} // namespace midnight::contract
