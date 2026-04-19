#pragma once

#include "midnight/zk/proof_types.hpp"
#include "midnight/network/network_client.hpp"
#include <memory>
#include <chrono>

namespace midnight::zk
{

    /**
     * @brief Client for communicating with the Midnight Proof Server
     *
     * The Proof Server (Docker: midnightntwrk/proof-server:8.0.3)
     * runs at http://localhost:6300 and generates zero-knowledge proofs
     * for circuit execution.
     *
     * This client handles:
     * - Proof generation requests
     * - Proof verification
     * - Circuit metadata retrieval
     * - Witness context management
     */
    class ProofServerClient
    {
    public:
        /**
         * @brief Configuration for Proof Server connection
         */
        struct Config
        {
            std::string host = "localhost";
            uint16_t port = 6300;
            std::chrono::milliseconds timeout_ms{30000};
            bool use_https = false;

            std::string base_url() const;
        };

        /**
         * @brief Constructor
         * @param config Proof Server configuration
         */
        ProofServerClient();
        explicit ProofServerClient(const Config &config);

        /**
         * @brief Initialize connection to Proof Server
         * @return true if connection successful
         */
        bool connect();

        /**
         * @brief Check if connected to Proof Server
         * @return true if reachable
         */
        bool is_connected() const;

        /**
         * @brief Generate a ZK proof for circuit execution
         *
         * Sends circuit data, inputs, and witness outputs to Proof Server.
         * Proof Server performs the cryptographic computation to generate a ZK proof.
         *
         * @param circuit_name Name of circuit (e.g., "post", "takeDown")
         * @param circuit_data Compiled circuit data (.zkir bytes)
         * @param inputs Circuit public inputs
         * @param witnesses Witness outputs from witness functions
         * @return ProofResult with generated proof or error
         */
        ProofResult generate_proof(
            const std::string &circuit_name,
            const std::vector<uint8_t> &circuit_data,
            const PublicInputs &inputs,
            const std::map<std::string, WitnessOutput> &witnesses);

        /**
         * @brief Verify a generated proof
         *
         * Local verification using public inputs and proof data.
         * Can be used to validate proof before submission to network.
         *
         * @param proof CircuitProof to verify
         * @return true if proof is valid
         */
        bool verify_proof(const CircuitProof &proof);

        /**
         * @brief Get circuit metadata from Proof Server
         *
         * Retrieves information about available circuits and their constraints.
         *
         * @param circuit_name Circuit identifier
         * @return Circuit metadata (constraints, version, etc.)
         */
        CircuitProofMetadata get_circuit_metadata(const std::string &circuit_name);

        /**
         * @brief Get Proof Server status
         *
         * Check if server is running and responsive.
         *
         * @return Server status information (version, circuits available, etc.)
         */
        json get_server_status();

        /**
         * @brief Submit proof to blockchain via RPC
         *
         * After proof is generated and verified, submit to network.
         *
         * @param transaction Proof-enabled transaction to submit
         * @param rpc_endpoint Midnight RPC node endpoint
         * @return Transaction hash if successful
         */
        std::string submit_proof_transaction(
            const ProofEnabledTransaction &transaction,
            const std::string &rpc_endpoint);

        /**
         * @brief Create empty proof for testing
         *
         * Useful for development and tests before Proof Server is available.
         *
         * @param circuit_name Circuit name for test proof
         * @return Test CircuitProof
         */
        CircuitProof create_test_proof(const std::string &circuit_name);

        /**
         * @brief Set Proof Server configuration
         * @param config New configuration
         */
        void set_config(const Config &config);

        /**
         * @brief Get current configuration
         * @return Current Config
         */
        Config get_config() const { return config_; }

        /**
         * @brief Get last error message
         * @return Error message from last operation
         */
        std::string get_last_error() const { return last_error_; }

    private:
        Config config_;
        std::shared_ptr<midnight::network::NetworkClient> network_client_;
        std::string last_error_;

        // Internal helper methods
        json build_proof_request(
            const std::string &circuit_name,
            const std::vector<uint8_t> &circuit_data,
            const PublicInputs &inputs,
            const std::map<std::string, WitnessOutput> &witnesses);

        ProofResult parse_proof_response(const json &response);

        void set_error(const std::string &error_msg);
    };

} // namespace midnight::zk
