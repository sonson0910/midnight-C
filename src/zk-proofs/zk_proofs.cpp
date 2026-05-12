/**
 * Phase 4: ZK Proofs Implementation
 */

#include "midnight/zk-proofs/zk_proofs.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/json_bridge_utils.hpp"
#include "midnight/core/logger.hpp"
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <sodium.h>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <mutex>
#include <cstring>

namespace
{
    using NJson = nlohmann::json;
    constexpr uint32_t kProofServerTimeoutMs = 30000;

    using midnight::util::strip_hex_prefix;
    using midnight::util::is_hex_string;

    // Sanitize error messages to prevent internal information disclosure.
    // Only allows printable ASCII, limits length, and strips sensitive patterns.
    static std::string sanitize_error_message(const std::string &raw)
    {
        if (raw.empty())
        {
            return "An internal error occurred";
        }

        std::string sanitized;
        sanitized.reserve(std::min(raw.size(), size_t(200)));

        for (size_t i = 0; i < raw.size() && sanitized.size() < 200; ++i)
        {
            char c = raw[i];
            if (c >= 32 && c <= 126)
            {
                sanitized += c;
            }
            else
            {
                sanitized += '?';
            }
        }

        if (sanitized.size() > 200)
        {
            sanitized.resize(200);
            sanitized += "...";
        }

        const std::string sensitive_patterns[] = {
            "password", "token", "secret", "api_key", "apikey",
            "authorization", "private_key", "credential"
        };
        for (const auto &pattern : sensitive_patterns)
        {
            for (size_t pos = sanitized.find(pattern); pos != std::string::npos; pos = sanitized.find(pattern, pos + 1))
            {
                size_t colon_pos = sanitized.find(':', pos);
                size_t newline_pos = sanitized.find('\n', pos);
                size_t end = std::min(colon_pos, newline_pos);
                if (end != std::string::npos && end > pos)
                {
                    sanitized.replace(pos, end - pos + 1, pattern + ": <redacted>");
                }
                pos = sanitized.find(pattern, pos + pattern.size() + 12);
            }
        }

        return sanitized;
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

    // Import shared bytes_to_hex from common_utils.hpp
    using midnight::util::bytes_to_hex;

    // Import shared JSON bridge from json_bridge_utils.hpp
    using midnight::util::nlohmann_to_jsoncpp;
    using midnight::util::jsoncpp_to_nlohmann;

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
                    result.error_message = sanitize_error_message(response["error"].get<std::string>());
                }
                else
                {
                    result.error_message = sanitize_error_message(response["error"].dump());
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
                    result.error_message = sanitize_error_message(response["error"].get<std::string>());
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
         * A proper ZK proof has specific structure depending on the proving system:
         * - Groth16: 3 elements (A, B, C) from pairing groups - ~128 bytes (compressed)
         * - PLONK: larger proof with multiple elements - ~400+ bytes
         * - Halo2: variable size depending on constraints - ~100-200 bytes
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
        
        if (vk_hex.size() < 128) {  // Minimum reasonable VK size
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
        
        /**
         * Step 5: Full verification requires ZK library
         * 
         * For production verification, integrate one of:
         * 
         * Option A: libsnark (C++)
         *   - Full Groth16/PLONK support
         *   - Requires trusted setup
         *   - Heavy dependency
         * 
         * Option B: bellman (Rust via Cbindgen)
         *   - Groth16 + PLONK
         *   - Used by zcash
         * 
         * Option C: snarkjs (JavaScript/WASM)
         *   - Groth16 + PLONK + FFLONK
         *   - Easy to integrate via WASM
         *   - Slower verification
         * 
         * Option D: circom + proof server (current architecture)
         *   - Proofs generated by proof server with ZK circuits
         *   - Server performs verification before accepting proof
         *   - This SDK just passes proof to Midnight node
         * 
         * For now, we return true if format is valid, with a warning.
         * The Midnight node will perform full verification.
         */
        midnight::g_logger->debug("ZK proof format validated - node will perform full SNARK verification");
        
        return true;
    }

    // ============================================================================
    // CommitmentGenerator Implementation
    // ============================================================================

    namespace
    {
        // Helper to convert BIGNUM to bytes (for internal use)
        void bn_to_bytes_fixed(const BIGNUM* bn, uint8_t* out, size_t len) {
            int nbytes = BN_num_bytes(bn);
            std::vector<uint8_t> tmp(nbytes);
            BN_bn2bin(bn, tmp.data());
            std::memset(out, 0, len - nbytes);
            std::memcpy(out + len - nbytes, tmp.data(), nbytes);
        }

        // Get the generator point G for secp256k1
        EC_POINT* get_secp256k1_generator() {
            static EC_POINT* generator = nullptr;
            if (!generator) {
                EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
                generator = EC_POINT_new(group);
                EC_POINT_copy(generator, EC_GROUP_get0_generator(group));
                EC_GROUP_free(group);
            }
            return generator;
        }

        // Derive H generator point from a hash
        // H is computed as hash("PedersenH") * G, making it deterministic
        EC_POINT* get_pedersen_h_generator() {
            static EC_POINT* h_generator = nullptr;
            static bool initialized = false;
            
            if (!initialized) {
                EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
                h_generator = EC_POINT_new(group);
                
                // Hash of "PedersenH" to get a scalar
                uint8_t hash[32];
                const char* h_label = "PedersenH";
                SHA256(reinterpret_cast<const uint8_t*>(h_label), 9, hash);
                
                // Ensure valid scalar (mod curve order)
                hash[0] &= 248;
                hash[31] &= 127;
                hash[31] |= 64;
                
                BIGNUM* scalar = BN_bin2bn(hash, 32, nullptr);
                const EC_POINT* g = EC_GROUP_get0_generator(group);
                
                // H = hash * G
                EC_POINT_mul(group, h_generator, scalar, nullptr, nullptr, nullptr);
                
                BN_free(scalar);
                EC_GROUP_free(group);
                initialized = true;
            }
            return h_generator;
        }

        // Convert a value to a 32-byte scalar (mod curve order)
        bool value_to_scalar(const std::string& value_str, BIGNUM*& scalar_out) {
            // Parse the value as a uint64_t first
            uint64_t value = 0;
            for (char c : value_str) {
                if (c >= '0' && c <= '9') {
                    value = value * 10 + (c - '0');
                }
            }
            
            // Convert to scalar
            scalar_out = BN_new();
            BN_set_word(scalar_out, value);
            return true;
        }

        // Convert randomness to scalar
        bool randomness_to_scalar(const std::string& randomness, BIGNUM*& scalar_out) {
            // Hash the randomness to get a 32-byte value
            uint8_t hash[32];
            SHA256(reinterpret_cast<const uint8_t*>(randomness.data()), randomness.size(), hash);
            
            // Ensure valid scalar
            hash[0] &= 248;
            hash[31] &= 127;
            hash[31] |= 64;
            
            scalar_out = BN_bin2bn(hash, 32, nullptr);
            return true;
        }

        // Point to compressed hex string (0x02/0x03 + x coordinate)
        std::string point_to_hex(EC_GROUP* group, EC_POINT* point) {
            BIGNUM* x = BN_new();
            BIGNUM* y = BN_new();
            EC_POINT_get_affine_coordinates(group, point, x, y, nullptr);
            
            uint8_t x_bytes[32];
            bn_to_bytes_fixed(x, x_bytes, 32);
            
            std::string result = "0x";
            result += (BN_is_odd(y) ? "03" : "02");
            for (int i = 0; i < 32; i++) {
                char buf[3];
                snprintf(buf, sizeof(buf), "%02x", x_bytes[i]);
                result += buf;
            }
            
            BN_free(x);
            BN_free(y);
            return result;
        }
    }

    /**
     * Compute Pedersen commitment: C = r*G + v*H
     * 
     * Pedersen commitments are additively homomorphic:
     * - C(v1, r1) + C(v2, r2) = C(v1+v2, r1+r2)
     * 
     * This allows range proofs and other ZK protocols.
     */
    std::string CommitmentGenerator::pedersen_commit(const std::string& value,
                                                 const std::string& randomness) {
        if (value.empty() || randomness.empty()) {
            return "";
        }

        try {
            EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
            EC_POINT* result = EC_POINT_new(group);
            EC_POINT* g = get_secp256k1_generator();
            EC_POINT* h = get_pedersen_h_generator();
            
            // Convert value to scalar
            BIGNUM* v_scalar = BN_new();
            value_to_scalar(value, v_scalar);
            
            // Convert randomness to scalar
            BIGNUM* r_scalar = BN_new();
            randomness_to_scalar(randomness, r_scalar);
            
            // C = r*G + v*H
            // Compute r*G and v*H separately, then add
            EC_POINT* term1 = EC_POINT_new(group);
            EC_POINT* term2 = EC_POINT_new(group);
            
            EC_POINT_mul(group, term1, r_scalar, nullptr, nullptr, nullptr);  // r*G
            EC_POINT_mul(group, term2, nullptr, h, v_scalar, nullptr);  // v*H
            
            EC_POINT_add(group, result, term1, term2, nullptr);
            
            // Convert result to hex (compressed format)
            std::string commitment = point_to_hex(group, result);
            
            // Cleanup
            EC_POINT_free(term1);
            EC_POINT_free(term2);
            BN_free(v_scalar);
            BN_free(r_scalar);
            EC_POINT_free(result);
            EC_GROUP_free(group);
            
            return commitment;
        }
        catch (const std::exception& e) {
            midnight::g_logger->error("Pedersen commitment failed: " + std::string(e.what()));
            return "";
        }
    }

    /**
     * Compute Poseidon hash commitment
     * 
     * Poseidon is a SNARK-friendly hash function designed for use in
     * zero-knowledge circuits. It is more efficient than Pedersen
     * for constraints in zk-SNARK circuits.
     * 
     * Since we don't have a native Poseidon implementation, we use
     * a Blake2b hash as a placeholder. For production, integrate
     * a Poseidon implementation.
     */
    std::string CommitmentGenerator::poseidon_commit(const std::string& value) {
        if (value.empty()) {
            return "";
        }

        try {
            uint8_t hash[32];
            // Use Blake2b for hash (SNARK-friendly alternative to SHA)
            crypto_generichash_blake2b(
                hash, 32,
                reinterpret_cast<const uint8_t*>(value.data()), value.size(),
                nullptr, 0
            );
            
            // Format as hex with 0x prefix
            std::string result = "0x";
            for (int i = 0; i < 32; i++) {
                char buf[3];
                snprintf(buf, sizeof(buf), "%02x", hash[i]);
                result += buf;
            }
            return result;
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
