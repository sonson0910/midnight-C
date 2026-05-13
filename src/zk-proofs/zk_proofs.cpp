/**
 * ZK proof metadata and legacy guard implementation.
 */

#include "midnight/zk-proofs/zk_proofs.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/json_bridge_utils.hpp"
#include "midnight/core/logger.hpp"
#include <chrono>
#include <ctime>
#include <stdexcept>
#include <mutex>

namespace
{
    using NJson = nlohmann::json;
    constexpr uint32_t kProofServerTimeoutMs = 30000;

    using midnight::util::strip_hex_prefix;
    using midnight::util::is_hex_string;

    bool is_even_hex_payload(const std::string &value)
    {
        const std::string stripped = strip_hex_prefix(value);
        return !stripped.empty() && is_hex_string(stripped);
    }

    // Import shared JSON bridge from json_bridge_utils.hpp
    using midnight::util::nlohmann_to_jsoncpp;
    using midnight::util::jsoncpp_to_nlohmann;
}

namespace midnight::zk_proofs
{

    // ============================================================================
    // ProofServerClient Implementation
    // ============================================================================

    ProofServerClient::ProofServerClient(const std::string &proof_server_url)
        : proof_server_url_(proof_server_url)
    {
    }

    bool ProofServerClient::connect()
    {
        connected_ = false;
        if (proof_server_url_.empty())
        {
            return false;
        }

        midnight::network::NetworkClient client(proof_server_url_, kProofServerTimeoutMs);
        const std::vector<std::string> health_endpoints = {
            "/status",
            "/health",
        };

        for (const auto &endpoint : health_endpoints)
        {
            try
            {
                NJson response = client.get_json(endpoint);
                if (response.is_object() || response.is_array())
                {
                    connected_ = true;
                    return true;
                }
            }
            catch (...)
            {
                // Try the next health endpoint.
            }
        }

        return false;
    }

    void ProofServerClient::disconnect()
    {
        connected_ = false;
    }

    bool ProofServerClient::is_healthy()
    {
        if (!connected_)
        {
            return false;
        }

        try
        {
            midnight::network::NetworkClient client(proof_server_url_, kProofServerTimeoutMs);
            for (const auto &endpoint : {std::string("/health"), std::string("/status")})
            {
                try
                {
                    NJson response = client.get_json(endpoint);
                    if (response.is_object() || response.is_array())
                    {
                        return true;
                    }
                }
                catch (...)
                {
                }
            }
            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    ProofGenResult ProofServerClient::request_proof(const ProofRequest &request)
    {
        ProofGenResult result;

        if (!is_healthy() && !connect())
        {
            result.success = false;
            result.error_message = "Proof Server not available";
            return result;
        }

        (void)request;
        result.success = false;
        result.error_message =
            "ProofServerClient::request_proof requires ledger-built binary payloads. "
            "Midnight proof-server exposes /check, /prove, and /prove-tx for serialized "
            "payload bytes; JSON /generate and /proof/generate are not valid endpoints.";
        return result;
    }

    std::optional<ProofGenResult> ProofServerClient::get_proof_status(const std::string &request_id)
    {
        (void)request_id;
        return {};
    }

    bool ProofServerClient::cancel_proof_request(const std::string &request_id)
    {
        (void)request_id;
        return false;
    }

    Json::Value ProofServerClient::http_post(const std::string &endpoint, const Json::Value &payload)
    {
        NJson request = jsoncpp_to_nlohmann(payload);
        if (request.is_discarded())
        {
            throw std::runtime_error("Invalid JSON payload");
        }

        midnight::network::NetworkClient client(proof_server_url_, kProofServerTimeoutMs);
        NJson response = client.post_json(endpoint.empty() ? "/" : endpoint, request);

        return nlohmann_to_jsoncpp(response);
    }

    // ============================================================================
    // ProofVerifier Implementation
    // ============================================================================

    ProofVerifier::ProofVerifier(const ZkCircuit &circuit) : circuit_(circuit)
    {
    }

    bool ProofVerifier::verify(const ZkProof &proof)
    {
        auto start = std::chrono::high_resolution_clock::now();

        const std::string proof_hex = strip_hex_prefix(proof.proof_data);
        const size_t proof_bytes = proof_hex.size() / 2;
        if (!is_even_hex_payload(proof.proof_data) || proof_bytes < 64 || proof_bytes > (10 * 1024 * 1024))
        {
            return false;
        }

        if (proof.circuit_id != circuit_.circuit_id)
        {
            return false;
        }

        bool verified = verify_proof_with_vk(proof, circuit_.verification_key);

        auto end = std::chrono::high_resolution_clock::now();
        last_verification_time_ms_ =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        ProofPerformanceMonitor::record_verification_time(last_verification_time_ms_);

        return verified;
    }

    bool ProofVerifier::verify_batch(const std::vector<ZkProof> &proofs)
    {
        if (proofs.empty())
        {
            return false;
        }

        for (const auto &proof : proofs)
        {
            if (!verify(proof))
            {
                return false;
            }
        }

        return true;
    }

    uint64_t ProofVerifier::get_verification_time_ms() const
    {
        return last_verification_time_ms_;
    }

    /**
     * Verify proof with verification key
     * 
     * Full ZK proof verification requires:
     * 1. A ZK library (libsnark, bellman, blst, etc.) for SNARK verification
     * 2. The verification key (VK) from the circuit setup (Powers of Tau ceremony)
     * 3. The public inputs/outputs that were used in proof generation
     * 
     * This implementation provides format validation and a clear document of
     * what full verification requires. For production use, integrate a ZK library.
     */
    bool ProofVerifier::verify_proof_with_vk(const ZkProof& proof, const std::string& vk) {
        /**
         * Step 1: Validate inputs
         */
        if (vk.empty()) {
            midnight::g_logger->error("ZK proof verification failed: empty verification key");
            return false;
        }
        
        if (proof.proof_data.empty()) {
            midnight::g_logger->error("ZK proof verification failed: empty proof data");
            return false;
        }
        
        /**
         * Step 2: Validate proof format
         * 
     * Midnight proofs are PLONK transcript bytes. The exact size depends on the
     * circuit, public inputs, and cost model, so validation here only checks
     * the transport encoding and leaves cryptographic verification to the
     * Midnight verifier/proof stack.
         * 
         * We check for minimum size and valid hex format.
         */
        std::string proof_hex = proof.proof_data;
        if (proof_hex.rfind("0x", 0) == 0 || proof_hex.rfind("0X", 0) == 0) {
            proof_hex = proof_hex.substr(2);
        }
        
        // Check valid hex
        if (proof_hex.size() % 2 != 0) {
            midnight::g_logger->error("ZK proof verification failed: odd-length hex");
            return false;
        }
        
        for (char c : proof_hex) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                midnight::g_logger->error("ZK proof verification failed: invalid hex character");
                return false;
            }
        }
        
        // Check minimum size (at least 32 bytes for any ZK proof)
        if (proof_hex.size() < 64) {  // 32 bytes minimum
            midnight::g_logger->error("ZK proof verification failed: proof too small");
            return false;
        }
        
        /**
         * Step 3: Validate verification key format
         * 
         * A Groth16 verification key contains:
         * - Alpha (G1 element)
         * - Beta (G2 element)  
         * - Gamma (G2 element)
         * - Delta (G2 element)
         * - IC (array of G1 elements - one per public input)
         * 
         * Minimum VK size is typically several hundred bytes.
         */
        std::string vk_hex = vk;
        if (vk_hex.rfind("0x", 0) == 0 || vk_hex.rfind("0X", 0) == 0) {
            vk_hex = vk_hex.substr(2);
        }
        
        if (!is_hex_string(vk_hex)) {
            midnight::g_logger->error("ZK proof verification failed: verification key is not hex");
            return false;
        }
        
        if (vk_hex.size() < 128) {
            midnight::g_logger->warn("ZK verification key appears too small - may be malformed");
        }
        
        /**
         * Step 4: Check public inputs match
         * 
         * The proof was generated with specific public inputs.
         * Verification MUST use the same public inputs.
         * If public_inputs is empty, this may be a pre-computed proof.
         */
        if (!proof.public_inputs.empty() && proof.circuit_id.empty()) {
            midnight::g_logger->warn("ZK proof verification: proof has public inputs but no circuit ID");
        }
        
        midnight::g_logger->error(
            "ZK proof verification requires the Midnight verifier/proof stack; "
            "format-only validation is not a cryptographic verification result");
        return false;
    }

    // ============================================================================
    // CommitmentGenerator Implementation
    // ============================================================================

    std::string CommitmentGenerator::pedersen_commit(const std::string& value,
                                                 const std::string& randomness) {
        if (value.empty() || randomness.empty()) {
            return "";
        }

        try {
            midnight::g_logger->error(
                "Pedersen commitment generation is not implemented in C++ for Midnight ledger types; "
                "use the ledger/proof stack instead");
            return "";
        }
        catch (const std::exception& e) {
            midnight::g_logger->error("Pedersen commitment failed: " + std::string(e.what()));
            return "";
        }
    }

    std::string CommitmentGenerator::poseidon_commit(const std::string& value) {
        if (value.empty()) {
            return "";
        }

        try {
            midnight::g_logger->error(
                "Poseidon commitment generation is not implemented; "
                "Blake2b is not a compatible substitute for Midnight circuit commitments");
            return "";
        }
        catch (const std::exception& e) {
            midnight::g_logger->error("Poseidon commitment failed: " + std::string(e.what()));
            return "";
        }
    }

    std::vector<std::string> CommitmentGenerator::batch_commit(
        const std::map<std::string, std::string> &values)
    {

        std::vector<std::string> commitments;
        for (const auto &[name, value] : values)
        {
            commitments.push_back(poseidon_commit(value));
        }
        return commitments;
    }

    bool CommitmentGenerator::verify_opening(const std::string &commitment,
                                             const std::string &value,
                                             const std::string &randomness)
    {
        (void)commitment;
        (void)value;
        (void)randomness;
        midnight::g_logger->error(
            "Commitment opening verification requires the Midnight ledger commitment implementation");
        return false;
    }

    // ============================================================================
    // WitnessBuilder Implementation
    // ============================================================================

    WitnessBuilder::WitnessBuilder(const ZkCircuit &circuit) : circuit_(circuit)
    {
        witness_.circuit_id = circuit.circuit_id;
    }

    void WitnessBuilder::add_private_input(const std::string &name, const std::string &value)
    {
        witness_.private_inputs[name] = value;
    }

    void WitnessBuilder::add_public_input(const std::string &name, const std::string &value)
    {
        witness_.public_inputs[name] = value;
    }

    std::optional<Witness> WitnessBuilder::build()
    {
        if (!validate())
        {
            return {};
        }

        // Commitment generation must come from the Midnight ledger/proof stack.
        witness_.commitments = CommitmentGenerator::batch_commit(witness_.private_inputs);

        return witness_;
    }

    bool WitnessBuilder::validate()
    {
        if (witness_.circuit_id.empty())
        {
            return false;
        }

        // At least one input required
        if (witness_.private_inputs.empty() && witness_.public_inputs.empty())
        {
            return false;
        }

        return true;
    }

    // ============================================================================
    // ProofCache Implementation
    // ============================================================================

    std::map<std::string, std::pair<ZkProof, uint64_t>> ProofCache::cache_;
    std::mutex ProofCache::cache_mutex_;

    void ProofCache::cache_proof(const std::string &circuit_id,
                                 const std::vector<std::string> &public_inputs,
                                 const ZkProof &proof)
    {
        std::string cache_key = circuit_id;
        for (const auto &input : public_inputs)
        {
            cache_key += ":" + input;
        }

        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_[cache_key] = {proof, std::time(nullptr)};
    }

    std::optional<ZkProof> ProofCache::get_cached_proof(
        const std::string &circuit_id,
        const std::vector<std::string> &public_inputs)
    {

        std::string cache_key = circuit_id;
        for (const auto &input : public_inputs)
        {
            cache_key += ":" + input;
        }

        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(cache_key);
        if (it == cache_.end())
        {
            return {};
        }

        // Check cache TTL
        uint64_t age = std::time(nullptr) - it->second.second;
        if (age > CACHE_TTL_SECONDS)
        {
            cache_.erase(it);
            return {};
        }

        return it->second.first;
    }

    void ProofCache::clear()
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.clear();
    }

    size_t ProofCache::cache_size()
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return cache_.size();
    }

    // ============================================================================
    // ProofPerformanceMonitor Implementation
    // ============================================================================

    std::vector<uint64_t> ProofPerformanceMonitor::generation_times_;
    std::vector<uint64_t> ProofPerformanceMonitor::verification_times_;
    std::mutex ProofPerformanceMonitor::perf_mutex_;

    void ProofPerformanceMonitor::record_generation_time(uint64_t time_ms)
    {
        std::lock_guard<std::mutex> lock(perf_mutex_);
        generation_times_.push_back(time_ms);
    }

    void ProofPerformanceMonitor::record_verification_time(uint64_t time_ms)
    {
        std::lock_guard<std::mutex> lock(perf_mutex_);
        verification_times_.push_back(time_ms);
    }

    uint64_t ProofPerformanceMonitor::get_avg_generation_time()
    {
        std::lock_guard<std::mutex> lock(perf_mutex_);
        if (generation_times_.empty())
        {
            return 0;
        }

        uint64_t sum = 0;
        for (auto time : generation_times_)
        {
            sum += time;
        }

        return sum / generation_times_.size();
    }

    uint64_t ProofPerformanceMonitor::get_avg_verification_time()
    {
        std::lock_guard<std::mutex> lock(perf_mutex_);
        if (verification_times_.empty())
        {
            return 0;
        }

        uint64_t sum = 0;
        for (auto time : verification_times_)
        {
            sum += time;
        }

        return sum / verification_times_.size();
    }

    ProofPerformanceMonitor::PerfStats ProofPerformanceMonitor::get_stats()
    {
        std::lock_guard<std::mutex> lock(perf_mutex_);
        PerfStats stats;

        // Inline avg computation to avoid deadlock (don't call
        // get_avg_generation_time / get_avg_verification_time which also lock)
        if (!generation_times_.empty())
        {
            uint64_t sum = 0;
            for (auto t : generation_times_) sum += t;
            stats.avg_generation_ms = sum / generation_times_.size();
        }
        else
        {
            stats.avg_generation_ms = 0;
        }

        if (!verification_times_.empty())
        {
            uint64_t sum = 0;
            for (auto t : verification_times_) sum += t;
            stats.avg_verification_ms = sum / verification_times_.size();
        }
        else
        {
            stats.avg_verification_ms = 0;
        }

        stats.generation_count = generation_times_.size();
        stats.verification_count = verification_times_.size();
        return stats;
    }

} // namespace midnight::zk_proofs
