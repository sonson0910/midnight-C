/**
 * Phase 4: ZK Proofs (Zero-Knowledge SNARKs)
 * Generates and verifies 128-byte zk-SNARKs for Midnight transactions
 *
 * Midnight uses zk-SNARKs for:
 * - Proving transaction correctness WITHOUT revealing amounts
 * - Privacy: Commitments provably correct without exposing values
 * - Compact proofs: 128 bytes (very small for on-chain)
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <json/json.h>
#include <cstdint>

namespace midnight::zk_proofs
{

    /**
     * ZK Circuit
     * Represents a compiled zero-knowledge circuit (from Compact contract)
     */
    struct ZkCircuit
    {
        std::string circuit_id;
        std::string verification_key; // VK for proof verification

        // Circuit complexity
        uint32_t gate_count = 0;
        uint32_t constraint_count = 0;
        uint32_t variable_count = 0;

        // Proof size
        uint32_t proof_size_bytes = 128; // Always 128 bytes for Midnight

        // Performance
        uint64_t prover_time_ms = 0; // Estimated proof generation time
    };

    /**
     * Witness (Private Input)
     * Data provided by prover but NOT revealed in proof
     */
    struct Witness
    {
        std::string circuit_id;

        // Private inputs (known only to prover)
        std::map<std::string, std::string> private_inputs;

        // Public inputs (visible in proof)
        std::map<std::string, std::string> public_inputs;

        // Commitments (prove knowledge of private data)
        std::vector<std::string> commitments;
    };

    /**
     * ZK Proof
     * 128-byte Midnight zk-SNARK proof
     */
    struct ZkProof
    {
        std::string proof_data; // 128 bytes = 256 hex chars
        std::string circuit_id;

        // Metadata
        std::vector<std::string> public_inputs; // What the proof proves about

        // Verification
        bool verified = false;
        uint64_t verification_time_ms = 0;
    };

    /**
     * Proof Request
     * Request for proof generation
     */
    struct ProofRequest
    {
        std::string circuit_id;

        // Witness data
        std::map<std::string, std::string> private_inputs;
        std::map<std::string, std::string> public_inputs;

        // Proof generation parameters
        uint32_t security_level = 128; // Bits of security
        bool fast_mode = false;
    };

    /**
     * Proof Generation Result
     */
    struct ProofGenResult
    {
        bool success = false;
        ZkProof proof;

        // Diagnostics
        std::string error_message;
        uint64_t generation_time_ms = 0;
    };

    /**
     * Proof Server Client
     * Communicates with Proof Server for proof generation
     *
     * Midnight provides Proof Servers for generating zk-SNARKs
     * Typical flow:
     * 1. App sends witness to Proof Server
     * 2. Server generates zk-SNARK (cryptographically secure)
     * 3. App includes proof in transaction
     * 4. Validators verify proof on-chain
     */
    class ProofServerClient
    {
    public:
        /**
         * Constructor
         * @param proof_server_url: URL of Proof Server (e.g., https://proof.preprod.midnight.network)
         */
        explicit ProofServerClient(const std::string &proof_server_url);

        /**
         * Connect to Proof Server
         */
        bool connect();

        /**
         * Disconnect from Proof Server
         */
        void disconnect();

        /**
         * Check Proof Server health
         */
        bool is_healthy();

        /**
         * Request proof generation
         * @param request: Witness data and parameters
         * @return Generated proof or error
         */
        ProofGenResult request_proof(const ProofRequest &request);

        /**
         * Get previous proof status
         */
        std::optional<ProofGenResult> get_proof_status(const std::string &request_id);

        /**
         * Cancel proof generation request
         */
        bool cancel_proof_request(const std::string &request_id);

    private:
        std::string proof_server_url_;
        bool connected_ = false;

        /**
         * HTTP request to Proof Server
         */
        Json::Value http_post(const std::string &endpoint, const Json::Value &payload);
    };

    /**
     * Proof Verifier
     * Verifies zk-SNARK proofs
     */
    class ProofVerifier
    {
    public:
        /**
         * Constructor
         * @param circuit: Circuit to verify against
         */
        explicit ProofVerifier(const ZkCircuit &circuit);

        /**
         * Verify proof
         * @param proof: Proof to verify
         * @return true if proof is valid
         */
        bool verify(const ZkProof &proof);

        /**
         * Verify batch of proofs
         */
        bool verify_batch(const std::vector<ZkProof> &proofs);

        /**
         * Get verification time for last proof
         */
        uint64_t get_verification_time_ms() const;

    private:
        ZkCircuit circuit_;
        uint64_t last_verification_time_ms_ = 0;

        /**
         * Verify proof using verification key
         */
        bool verify_proof_with_vk(const ZkProof &proof, const std::string &vk);
    };

    /**
     * Commitment Generator
     * Generates commitments for private data (Pedersen, Poseidon, etc.)
     */
    class CommitmentGenerator
    {
    public:
        /**
         * Generate Pedersen commitment
         * C = r*G + v*H (blinded commitment to value v)
         */
        static std::string pedersen_commit(const std::string &value, const std::string &randomness);

        /**
         * Generate Poseidon hash commitment
         * More efficient for circuits
         */
        static std::string poseidon_commit(const std::string &value);

        /**
         * Generate batch commitments
         */
        static std::vector<std::string> batch_commit(
            const std::map<std::string, std::string> &values);

        /**
         * Verify commitment opening
         * Prove: C opens to value v with randomness r
         */
        static bool verify_opening(const std::string &commitment,
                                   const std::string &value,
                                   const std::string &randomness);
    };

    /**
     * Witness Builder
     * Constructs witness from private and public data
     */
    class WitnessBuilder
    {
    public:
        /**
         * Constructor
         * @param circuit: Circuit to prove
         */
        explicit WitnessBuilder(const ZkCircuit &circuit);

        /**
         * Add private input
         * This data is NOT revealed in proof
         */
        void add_private_input(const std::string &name, const std::string &value);

        /**
         * Add public input
         * This data IS part of the statement being proved
         */
        void add_public_input(const std::string &name, const std::string &value);

        /**
         * Build witness
         */
        std::optional<Witness> build();

        /**
         * Validate witness format
         */
        bool validate();

    private:
        ZkCircuit circuit_;
        Witness witness_;
    };

    /**
     * Proof Cache
     * Caches computed proofs for reuse
     */
    class ProofCache
    {
    public:
        /**
         * Store proof in cache
         */
        static void cache_proof(const std::string &circuit_id,
                                const std::vector<std::string> &public_inputs,
                                const ZkProof &proof);

        /**
         * Retrieve cached proof
         * Returns empty if not found or cache expired
         */
        static std::optional<ZkProof> get_cached_proof(
            const std::string &circuit_id,
            const std::vector<std::string> &public_inputs);

        /**
         * Clear cache
         */
        static void clear();

        /**
         * Get cache statistics
         */
        static size_t cache_size();

    private:
        static std::map<std::string, std::pair<ZkProof, uint64_t>> cache_;
        static constexpr uint64_t CACHE_TTL_SECONDS = 3600; // 1 hour
    };

    /**
     * Proof Performance Monitor
     * Tracks proof generation and verification performance
     */
    class ProofPerformanceMonitor
    {
    public:
        /**
         * Record proof generation time
         */
        static void record_generation_time(uint64_t time_ms);

        /**
         * Record proof verification time
         */
        static void record_verification_time(uint64_t time_ms);

        /**
         * Get average generation time
         */
        static uint64_t get_avg_generation_time();

        /**
         * Get average verification time
         */
        static uint64_t get_avg_verification_time();

        /**
         * Get performance statistics
         */
        struct PerfStats
        {
            uint64_t avg_generation_ms;
            uint64_t avg_verification_ms;
            uint32_t generation_count;
            uint32_t verification_count;
        };

        static PerfStats get_stats();

    private:
        static std::vector<uint64_t> generation_times_;
        static std::vector<uint64_t> verification_times_;
    };

} // namespace midnight::zk_proofs
