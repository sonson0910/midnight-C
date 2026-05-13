/**
 * ZK Proofs
 * Handles local proof metadata and guards legacy paths.
 *
 * Midnight uses zk-SNARKs for:
 * - Proving transaction correctness WITHOUT revealing amounts
 * - Privacy: Commitments provably correct without exposing values
 * - Compact binary proofs whose size depends on the circuit
 *
 * Production proof creation/verification must use Midnight ledger/proof
 * payloads. This module must not substitute JSON proof requests, Blake2b
 * hashes, or format-only checks for ledger-compatible proofs.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <json/json.h>
#include <cstdint>
#include <mutex>

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

        // Expected proof size hint, when known.
        uint32_t proof_size_bytes = 0;

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
     * Midnight proof bytes encoded as hex.
     */
    struct ZkProof
    {
        std::string proof_data; // Hex payload, with or without 0x prefix.
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
         * @param proof_server_url: URL of local Proof Server (e.g., http://localhost:6300)
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
         * Legacy high-level proof request.
         *
         * Midnight proof-server accepts ledger-built binary payloads on /check,
         * /prove, and /prove-tx. This method returns a clear error until the
         * caller supplies serialized payload bytes from the ledger/Compact toolchain.
         *
         * @param request: Witness data and parameters
         * @return Error explaining the required binary payload flow
         */
        ProofGenResult request_proof(const ProofRequest &request);

        /**
         * Legacy async-status helper. The Midnight proof server endpoints used
         * here are synchronous binary endpoints, so this returns empty.
         */
        std::optional<ProofGenResult> get_proof_status(const std::string &request_id);

        /**
         * Legacy async-cancel helper. The Midnight proof server endpoints used
         * here are synchronous binary endpoints, so this returns false.
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
         * Validate proof container format only, then fail closed because C++
         * does not currently include the Midnight verifier/proof stack.
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
     * Guard methods for commitment generation. Production commitments must come
     * from the Midnight ledger/proof stack; these methods fail closed.
     */
    class CommitmentGenerator
    {
    public:
        /**
         * Fail closed; C++ does not currently implement Midnight-compatible
         * ledger commitments.
         */
        static std::string pedersen_commit(const std::string &value, const std::string &randomness);

        /**
         * Fail closed; Blake2b or any other hash is not a compatible substitute
         * for Midnight circuit commitments.
         */
        static std::string poseidon_commit(const std::string &value);

        /**
         * Generate batch commitments; entries are empty when ledger support is
         * unavailable.
         */
        static std::vector<std::string> batch_commit(
            const std::map<std::string, std::string> &values);

        /**
         * Fail closed; opening verification requires the ledger implementation.
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
        static std::mutex cache_mutex_;
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
        static std::mutex perf_mutex_;
    };

} // namespace midnight::zk_proofs
