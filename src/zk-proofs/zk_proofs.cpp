/**
 * Phase 4: ZK Proofs Implementation
 */

#include "midnight/zk-proofs/zk_proofs.hpp"
#include "midnight/network/network_client.hpp"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <cctype>
#include <stdexcept>

namespace
{
    using NJson = nlohmann::json;
    constexpr uint32_t kProofServerTimeoutMs = 30000;

    std::string strip_hex_prefix(const std::string &value)
    {
        if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
        {
            return value.substr(2);
        }
        return value;
    }

    bool is_hex_string(const std::string &value)
    {
        if (value.empty())
        {
            return false;
        }

        for (char c : value)
        {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
            {
                return false;
            }
        }

        return true;
    }

    std::vector<uint8_t> decode_hex_or_ascii(const std::string &value)
    {
        const std::string normalized = strip_hex_prefix(value);
        if (is_hex_string(normalized) && (normalized.size() % 2 == 0))
        {
            std::vector<uint8_t> bytes;
            bytes.reserve(normalized.size() / 2);
            for (size_t i = 0; i < normalized.size(); i += 2)
            {
                const std::string byte_str = normalized.substr(i, 2);
                bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
            }
            return bytes;
        }

        return std::vector<uint8_t>(value.begin(), value.end());
    }

    std::string bytes_to_hex(const std::vector<uint8_t> &bytes)
    {
        static const char *kHex = "0123456789abcdef";
        std::string encoded;
        encoded.reserve(bytes.size() * 2);
        for (uint8_t byte : bytes)
        {
            encoded.push_back(kHex[(byte >> 4) & 0x0F]);
            encoded.push_back(kHex[byte & 0x0F]);
        }
        return encoded;
    }

    Json::Value nlohmann_to_jsoncpp(const NJson &input)
    {
        Json::CharReaderBuilder builder;
        Json::Value parsed;
        std::string errors;
        std::istringstream stream(input.dump());
        if (!Json::parseFromStream(builder, stream, &parsed, &errors))
        {
            throw std::runtime_error("Failed to convert JSON response: " + errors);
        }
        return parsed;
    }

    NJson jsoncpp_to_nlohmann(const Json::Value &input)
    {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        const std::string serialized = Json::writeString(writer, input);
        return NJson::parse(serialized.empty() ? "{}" : serialized, nullptr, false);
    }

    std::optional<std::string> extract_proof_hex(const NJson &node)
    {
        if (node.is_string())
        {
            return node.get<std::string>();
        }

        if (node.is_object())
        {
            if (node.contains("proof_hex") && node["proof_hex"].is_string())
            {
                return node["proof_hex"].get<std::string>();
            }
            if (node.contains("proof") && node["proof"].is_string())
            {
                return node["proof"].get<std::string>();
            }
            if (node.contains("proof") && node["proof"].is_object())
            {
                const auto &proof_obj = node["proof"];
                if (proof_obj.contains("proof_hex") && proof_obj["proof_hex"].is_string())
                {
                    return proof_obj["proof_hex"].get<std::string>();
                }
            }
            if (node.contains("proof_data") && node["proof_data"].is_string())
            {
                return node["proof_data"].get<std::string>();
            }
            if (node.contains("hex") && node["hex"].is_string())
            {
                return node["hex"].get<std::string>();
            }
        }

        return {};
    }
}

namespace midnight::phase4
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
            NJson response = client.get_json("/status");
            return response.is_object() || response.is_array();
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

        try
        {
            const auto started = std::chrono::high_resolution_clock::now();

            const std::string circuit_name =
                request.circuit_id.empty() ? "default_circuit" : request.circuit_id;

            std::vector<uint8_t> circuit_data;
            auto circuit_data_it = request.private_inputs.find("__circuit_data_hex");
            if (circuit_data_it != request.private_inputs.end())
            {
                circuit_data = decode_hex_or_ascii(circuit_data_it->second);
            }
            if (circuit_data.empty())
            {
                circuit_data.assign(circuit_name.begin(), circuit_name.end());
            }

            NJson proof_request;
            proof_request["circuit_name"] = circuit_name;
            proof_request["circuit_data_hex"] = bytes_to_hex(circuit_data);

            NJson inputs_json = NJson::object();
            for (const auto &[key, value] : request.public_inputs)
            {
                std::string normalized = strip_hex_prefix(value);
                if (normalized.empty())
                {
                    normalized = "00";
                }
                else if (!is_hex_string(normalized))
                {
                    normalized = bytes_to_hex(std::vector<uint8_t>(value.begin(), value.end()));
                }
                inputs_json[key] = normalized;
            }
            proof_request["circuit_inputs"] = inputs_json;

            NJson witnesses_json = NJson::object();
            for (const auto &[key, value] : request.private_inputs)
            {
                if (key == "__circuit_data_hex")
                {
                    continue;
                }

                const std::vector<uint8_t> witness_bytes = decode_hex_or_ascii(value);
                NJson witness_json;
                witness_json["witness_name"] = key;
                witness_json["output_type"] = "Bytes";
                witness_json["output_hex"] = bytes_to_hex(witness_bytes);
                witness_json["size"] = witness_bytes.size();
                witnesses_json[key] = witness_json;
            }
            proof_request["witnesses"] = witnesses_json;

            midnight::network::NetworkClient client(proof_server_url_, kProofServerTimeoutMs);
            const std::vector<std::string> generate_endpoints = {
                "/generate",
                "/proof/generate",
            };

            NJson response;
            bool endpoint_hit = false;
            std::string last_error;
            for (const auto &endpoint : generate_endpoints)
            {
                try
                {
                    response = client.post_json(endpoint, proof_request);
                    endpoint_hit = true;
                    break;
                }
                catch (const std::exception &e)
                {
                    last_error = e.what();
                }
            }

            if (!endpoint_hit)
            {
                throw std::runtime_error(last_error.empty() ? "No generate endpoint available" : last_error);
            }

            const auto finished = std::chrono::high_resolution_clock::now();
            result.generation_time_ms =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count());

            result.proof.circuit_id = circuit_name;
            const NJson result_node = response.contains("result") ? response["result"] : response;

            if (response.contains("success") && response["success"].is_boolean())
            {
                result.success = response["success"].get<bool>();
            }
            else
            {
                result.success = true;
            }

            if (response.contains("error"))
            {
                result.success = false;
                if (response["error"].is_string())
                {
                    result.error_message = response["error"].get<std::string>();
                }
                else
                {
                    result.error_message = response["error"].dump();
                }
            }

            const auto proof_hex = extract_proof_hex(result_node);
            if (proof_hex.has_value())
            {
                std::string normalized_proof = *proof_hex;
                if (normalized_proof.rfind("0x", 0) != 0 && normalized_proof.rfind("0X", 0) != 0)
                {
                    normalized_proof = "0x" + normalized_proof;
                }
                result.proof.proof_data = normalized_proof;
            }

            if (result_node.is_object() && result_node.contains("public_inputs") && result_node["public_inputs"].is_object())
            {
                for (const auto &[_, value] : result_node["public_inputs"].items())
                {
                    if (value.is_string())
                    {
                        result.proof.public_inputs.push_back(value.get<std::string>());
                    }
                }
            }

            if (result.proof.public_inputs.empty())
            {
                for (const auto &[_, value] : request.public_inputs)
                {
                    result.proof.public_inputs.push_back(value);
                }
            }

            result.proof.verified = false;

            if (!result.success && result.error_message.empty())
            {
                result.error_message = "Proof server returned an unsuccessful response";
            }

            if (result.success)
            {
                ProofPerformanceMonitor::record_generation_time(result.generation_time_ms);
            }

            return result;
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error_message = std::string("Proof generation failed: ") + e.what();
            return result;
        }
    }

    std::optional<ProofGenResult> ProofServerClient::get_proof_status(const std::string &request_id)
    {
        if (request_id.empty())
        {
            return {};
        }

        const NJson payload = {
            {"request_id", request_id},
        };

        const std::vector<std::string> endpoints = {
            "/proofs/status",
            "/proof/status",
            "/status",
        };

        for (const auto &endpoint : endpoints)
        {
            try
            {
                midnight::network::NetworkClient client(proof_server_url_, kProofServerTimeoutMs);
                NJson response = client.post_json(endpoint, payload);

                ProofGenResult result;
                if (response.contains("success") && response["success"].is_boolean())
                {
                    result.success = response["success"].get<bool>();
                }
                else if (response.contains("status") && response["status"].is_string())
                {
                    const std::string status = response["status"].get<std::string>();
                    result.success = (status == "success" || status == "completed" || status == "ready");
                    if (!result.success)
                    {
                        result.error_message = status;
                    }
                }

                if (response.contains("error") && response["error"].is_string())
                {
                    result.success = false;
                    result.error_message = response["error"].get<std::string>();
                }

                const auto proof_hex = extract_proof_hex(response.contains("result") ? response["result"] : response);
                if (proof_hex.has_value())
                {
                    result.proof.proof_data = *proof_hex;
                    result.success = true;
                }

                return result;
            }
            catch (...)
            {
                // Try next known status endpoint.
            }
        }

        return {};
    }

    bool ProofServerClient::cancel_proof_request(const std::string &request_id)
    {
        if (request_id.empty())
        {
            return false;
        }

        const NJson payload = {
            {"request_id", request_id},
        };

        const std::vector<std::string> endpoints = {
            "/proofs/cancel",
            "/proof/cancel",
            "/cancel",
        };

        for (const auto &endpoint : endpoints)
        {
            try
            {
                midnight::network::NetworkClient client(proof_server_url_, kProofServerTimeoutMs);
                NJson response = client.post_json(endpoint, payload);

                if (response.contains("cancelled") && response["cancelled"].is_boolean())
                {
                    return response["cancelled"].get<bool>();
                }
                if (response.contains("success") && response["success"].is_boolean())
                {
                    return response["success"].get<bool>();
                }
                if (response.contains("status") && response["status"].is_string())
                {
                    const std::string status = response["status"].get<std::string>();
                    if (status == "success" || status == "cancelled" || status == "ok")
                    {
                        return true;
                    }
                }
            }
            catch (...)
            {
                // Try next known cancel endpoint.
            }
        }

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

        // In production: Verify zk-SNARK using verification key
        // For now: Basic validation

        if (proof.proof_data.size() != 258)
        { // "0x" + 256 hex chars = 128 bytes
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

    bool ProofVerifier::verify_proof_with_vk(const ZkProof &proof, const std::string &vk)
    {
        // In production: Use snark verification library
        // Verify: proof is valid under vk with public inputs

        if (vk.empty() || proof.proof_data.empty())
        {
            return false;
        }

        // Mock: Accept if proof and VK are both non-empty
        return true;
    }

    // ============================================================================
    // CommitmentGenerator Implementation
    // ============================================================================

    std::string CommitmentGenerator::pedersen_commit(const std::string &value,
                                                     const std::string &randomness)
    {
        // In production: Compute C = r*G + v*H
        // Using elliptic curve cryptography

        if (value.empty() || randomness.empty())
        {
            return "";
        }

        // Mock: Return hash of value and randomness
        return "0x" + std::string(64, 'c');
    }

    std::string CommitmentGenerator::poseidon_commit(const std::string &value)
    {
        // Poseidon is efficient hash for circuits
        // More efficient than Pedersen for zk-SNARKs

        if (value.empty())
        {
            return "";
        }

        // Mock: Return commitment
        return "0x" + std::string(64, 'c');
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
        // Verify: commitment = pedersen_commit(value, randomness)

        auto recomputed = pedersen_commit(value, randomness);
        return recomputed == commitment;
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

        // Generate commitments for private inputs
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

    void ProofCache::cache_proof(const std::string &circuit_id,
                                 const std::vector<std::string> &public_inputs,
                                 const ZkProof &proof)
    {
        std::string cache_key = circuit_id;
        for (const auto &input : public_inputs)
        {
            cache_key += ":" + input;
        }

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
        cache_.clear();
    }

    size_t ProofCache::cache_size()
    {
        return cache_.size();
    }

    // ============================================================================
    // ProofPerformanceMonitor Implementation
    // ============================================================================

    std::vector<uint64_t> ProofPerformanceMonitor::generation_times_;
    std::vector<uint64_t> ProofPerformanceMonitor::verification_times_;

    void ProofPerformanceMonitor::record_generation_time(uint64_t time_ms)
    {
        generation_times_.push_back(time_ms);
    }

    void ProofPerformanceMonitor::record_verification_time(uint64_t time_ms)
    {
        verification_times_.push_back(time_ms);
    }

    uint64_t ProofPerformanceMonitor::get_avg_generation_time()
    {
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
        PerfStats stats;
        stats.avg_generation_ms = get_avg_generation_time();
        stats.avg_verification_ms = get_avg_verification_time();
        stats.generation_count = generation_times_.size();
        stats.verification_count = verification_times_.size();
        return stats;
    }

} // namespace midnight::phase4
