/**
 * Phase 2: Compact Contracts
 * Handles Compact language contracts (TypeScript-based, not Solidity)
 *
 * Midnight uses Compact for smart contracts, which:
 * - Compiles to ZK circuits automatically
 * - Has public + private state sections
 * - Uses TypeScript-like syntax (not Solidity)
 * - Generates zk-SNARKs for private computations
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <json/json.h>
#include <cstdint>

namespace midnight::phase2
{

    /**
     * Compact Contract ABI Function Parameter
     */
    struct AbiParam
    {
        std::string name;
        std::string type; // "u32", "u64", "bytes32", "bytes", etc.
        bool is_array = false;
        uint32_t array_size = 0;
    };

    /**
     * Compact Contract ABI Function
     */
    struct AbiFunction
    {
        std::string name;
        std::vector<AbiParam> parameters;
        std::string return_type;

        // Transaction cost estimation (in DUST)
        uint64_t estimated_weight = 0;

        // Access control
        bool requires_private_witness = false; // Needs witness function?
        bool modifies_state = false;           // Is this function mutable?
    };

    /**
     * Compact Contract ABI (Application Binary Interface)
     * Defines contract structure and functions
     */
    struct CompactAbi
    {
        std::string contract_name;
        std::string version;

        // Public state
        std::map<std::string, std::string> public_state; // name -> type

        // Private state (encrypted on-chain)
        std::map<std::string, std::string> private_state; // name -> type

        // Callable functions
        std::vector<AbiFunction> functions;

        // Constructor parameters
        std::vector<AbiParam> constructor_params;
    };

    /**
     * Compiled Compact Contract
     */
    struct CompiledContract
    {
        std::string bytecode;         // Compiled machine code
        std::string verification_key; // For ZK proofs
        CompactAbi abi;

        // Circuit information
        uint32_t circuit_gates = 0;            // Number of gates in circuit
        uint32_t circuit_constraints = 0;      // Constraint count
        uint64_t proof_generation_time_ms = 0; // Estimated proof time
    };

    /**
     * Contract State (Read)
     */
    struct ContractState
    {
        std::string contract_address;
        uint32_t block_height;

        // Public state accessible without proof
        Json::Value public_data;

        // Private state commitments (encrypted, requires witness to access)
        std::map<std::string, std::string> private_commitments;
    };

    /**
     * Compact Contract Loader
     * Parses and loads Compact contract files
     */
    class CompactLoader
    {
    public:
        /**
         * Load ABI from JSON
         */
        static std::optional<CompactAbi> load_abi(const std::string &abi_json);

        /**
         * Load compiled contract from file
         */
        static std::optional<CompiledContract> load_compiled(const std::string &wasm_file,
                                                             const std::string &abi_file);

        /**
         * Validate ABI format
         */
        static bool validate_abi(const CompactAbi &abi);
    };

    /**
     * Compact Contract Query Engine
     * Queries Midnight Indexer for contract state
     */
    class ContractQueryEngine
    {
    public:
        /**
         * Constructor
         * @param indexer_graphql_url: GraphQL endpoint (https://indexer.preprod.midnight.network/api/v3/graphql)
         */
        explicit ContractQueryEngine(const std::string &indexer_graphql_url);

        /**
         * Query public state
         * @param contract_address: Contract address
         * @param state_key: Public state key
         * @return State value or empty
         */
        std::optional<Json::Value> query_public_state(const std::string &contract_address,
                                                      const std::string &state_key);

        /**
         * Query all public state
         */
        std::optional<ContractState> query_all_public_state(const std::string &contract_address);

        /**
         * Query contract metadata
         */
        std::optional<CompactAbi> query_contract_abi(const std::string &contract_address);

        /**
         * Query contract deployment info
         */
        struct DeploymentInfo
        {
            std::string deployer_address;
            uint32_t block_height;
            std::string transaction_hash;
            uint64_t deployment_timestamp;
        };

        std::optional<DeploymentInfo> query_deployment_info(const std::string &contract_address);

    private:
        std::string indexer_url_;

        /**
         * Execute GraphQL query
         */
        Json::Value graphql_query(const std::string &query_str);
    };

    /**
     * Compact Contract Builder
     * Constructs function calls for contract interaction
     */
    class ContractCallBuilder
    {
    public:
        /**
         * Constructor
         * @param abi: Contract ABI
         */
        explicit ContractCallBuilder(const CompactAbi &abi);

        /**
         * Build function call data
         * @param function_name: Name of function to call
         * @param args: Function arguments as JSON
         * @return Encoded call data
         */
        std::string build_call(const std::string &function_name,
                               const Json::Value &args);

        /**
         * Validate function call
         */
        bool validate_call(const std::string &function_name,
                           const Json::Value &args);

        /**
         * Get function ABI
         */
        std::optional<AbiFunction> get_function(const std::string &function_name);

        /**
         * Estimate gas/weight for function call
         */
        uint64_t estimate_weight(const std::string &function_name);

    private:
        CompactAbi abi_;

        /**
         * Encode parameter value according to type
         */
        std::string encode_parameter(const std::string &type, const Json::Value &value);
    };

    /**
     * Contract Deployer
     * Deploys Compact contracts to Midnight
     */
    class ContractDeployer
    {
    public:
        /**
         * Constructor
         * @param node_rpc_url: Node RPC connection
         */
        explicit ContractDeployer(const std::string &node_rpc_url);

        /**
         * Deploy contract
         * @param compiled_contract: Compiled contract bytecode + ABI
         * @param constructor_args: Constructor args + optional deploy bridge config.
         *        The official bridge is opt-in and requires `use_official_sdk_bridge=true`.
         *        Supported keys: `network`, `contract`, `contract_module`, `contract_export`,
         *        `seed_hex`, `fee_bps`, `initial_nonce_hex`, `proof_server`, `node_url`,
         *        `indexer_url`, `indexer_ws_url`, `sync_timeout`, `dust_wait`,
         *        `deploy_timeout`, `dust_utxo_limit`, `private_state_store`,
         *        `private_state_id`, `constructor_args_json`, `constructor_args_file`,
         *        `constructor_args`, `zk_config_path`, and `bridge_script`.
         * @return Contract address or empty
         */
        std::optional<std::string> deploy_contract(const CompiledContract &compiled_contract,
                                                   const Json::Value &constructor_args);

        /**
         * Get deployment status
         */
        struct DeploymentStatus
        {
            enum Status
            {
                PENDING,
                CONFIRMED,
                FINALIZED,
                FAILED
            };
            Status status;
            std::string transaction_hash;
            uint32_t block_height = 0;
            std::string contract_address;
        };

        std::optional<DeploymentStatus> get_deployment_status(const std::string &tx_hash);

    private:
        std::string rpc_url_;
    };

    /**
     * Witness Context
     * For Compact contracts: proves knowledge of private data without revealing it
     */
    struct WitnessContext
    {
        std::string contract_address;

        // Private inputs (not sent on-chain)
        std::map<std::string, std::string> private_inputs;

        // Public commitments (sent on-chain)
        std::vector<std::string> commitments;

        // Function being called
        std::string function_name;

        // Function arguments
        Json::Value function_args;
    };

    /**
     * Witness Function Processor
     * Builds witness contexts for private contract interactions
     */
    class WitnessFunctionProcessor
    {
    public:
        /**
         * Constructor
         * @param contract_abi: Compact contract ABI
         */
        explicit WitnessFunctionProcessor(const CompactAbi &contract_abi);

        /**
         * Build witness context
         * @param function_name: Function to call
         * @param private_data: Private inputs (witness data)
         * @param public_args: Public function arguments
         * @return Witness context
         */
        WitnessContext build_witness(const std::string &function_name,
                                     const std::map<std::string, std::string> &private_data,
                                     const Json::Value &public_args);

        /**
         * Check if function requires witness
         */
        bool requires_witness(const std::string &function_name);

        /**
         * Generate commitments for private data
         * These prove knowledge without revealing data
         */
        std::vector<std::string> generate_commitments(
            const std::map<std::string, std::string> &private_data);

    private:
        CompactAbi contract_abi_;
    };

} // namespace midnight::phase2
