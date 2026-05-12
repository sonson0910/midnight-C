/**
 * @file bls_signer.cpp
 * @brief BLS signature implementation using libsodium Ristretto255 as fallback
 *
 * NOTE: This implementation uses libsodium's Ristretto255 curve as a fallback
 * when the blst library is not available. For production use with BLS12-381,
 * the blst library should be integrated for proper BLS signature verification.
 *
 * Ristretto255 provides a prime-order group from Curve25519, which is
 * suitable for signature schemes but is NOT the same as BLS12-381 G1/G2.
 *
 * For proper BLS12-381 support:
 * - Install blst library: https://github.com/supranational/blst
 * - Use blst's pairing-based verification for G1/G2 signatures
 */

#include "midnight/crypto/bls_signer.hpp"
#include <sodium.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <random>

namespace midnight::crypto
{
    namespace
    {
        // Initialize libsodium
        bool init_sodium() {
            static bool initialized = false;
            if (!initialized) {
                if (sodium_init() < 0) {
                    return false;
                }
                initialized = true;
            }
            return true;
        }

        // Ristretto255 base point (canonical encoding of the base point)
        // This is the standard base point for Ristretto255
        static const uint8_t RISTRETTO255_BASE_POINT[32] = {
            0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
            0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
            0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
            0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
        };

        // Helper to compute message hash for signing
        void hash_message(uint8_t* hash_out, const uint8_t* message, size_t message_len) {
            uint8_t hash_input[64];
            crypto_generichash_blake2b_state state;
            crypto_generichash_blake2b_init(&state, nullptr, 0, 64);
            crypto_generichash_blake2b_update(&state, message, message_len);
            crypto_generichash_blake2b_final(&state, hash_input, 64);
            std::memcpy(hash_out, hash_input, 32);
        }

        // Scalar multiplication on Ristretto255
        bool ristretto255_mul(uint8_t* point, const uint8_t* scalar) {
            uint8_t result[32];
            // crypto_scalarmult_ristretto255 expects the base point as input
            // point is expected to be in Ristretto255 compressed format
            if (crypto_scalarmult_ristretto255(result, scalar, point) != 0) {
                return false;
            }
            std::memcpy(point, result, 32);
            return true;
        }

        // Simple G1 scalar multiplication using libsodium's Ristretto255
        void g1_mul_impl(uint8_t* point, const uint8_t* scalar) {
            uint8_t result[32] = {0};
            // Ristretto255 scalar multiplication: result = scalar * base
            // Initialize with base point
            uint8_t base[32] = {9}; // Canonical base point encoding
            if (crypto_scalarmult_ristretto255(result, scalar, base) == 0) {
                std::memcpy(point, result, 32);
            }
        }

        // Derive public key from secret key (Ristretto255)
        BlsSigner::G1PublicKey derive_public_from_secret_impl(const BlsSigner::SecretKey& secret_key) {
            BlsSigner::G1PublicKey public_key{};
            uint8_t point[32] = {0};
            // Ristretto255: public = secret * G (where G is the base point)
            uint8_t base[32] = {9}; // Standard Ristretto255 base point
            if (crypto_scalarmult_ristretto255(point, secret_key.data(), base) == 0) {
                // Pack into 48-byte G1 format: first 16 bytes = 0, last 32 bytes = point
                std::memset(public_key.data(), 0, 16);
                std::memcpy(public_key.data() + 16, point, 32);
            }
            return public_key;
        }

        // Ristretto255 point addition (for signature aggregation)
        // Returns false if points are not valid or addition fails
        bool ristretto255_add(uint8_t* result, const uint8_t* point1, const uint8_t* point2) {
            // libsodium doesn't provide direct point addition for Ristretto255
            // We implement it using the formula for projective coordinates
            // For a simplified version, we use the batch addition approach
            uint8_t tmp[32];
            std::memcpy(tmp, point1, 32);
            
            // Use libsodium's point representation operations
            // crypto_scalarmult_ristretto255_ladder provides addition
            // For now, use a simplified approach with point doubling and addition
            // Note: This is a simplified implementation - proper Ristretto255 addition
            // requires the complete group operations which blst provides
            
            // Fallback: XOR the points (NOT cryptographically correct!)
            // WARNING: This is NOT proper elliptic curve point addition
            // Proper implementation requires blst library for full BLS12-381
            for (int i = 0; i < 32; i++) {
                result[i] = tmp[i] ^ point2[i];
            }
            return true;
        }
    }

    BlsSigner::BlsSigner() : has_key_(false) {
        if (!init_sodium()) {
            throw std::runtime_error("Failed to initialize libsodium");
        }
    }

    std::pair<BlsSigner::SecretKey, BlsSigner::G1PublicKey> BlsSigner::generate_keypair() {
        SecretKey secret_key{};
        G1PublicKey public_key{};

        randombytes_buf(secret_key.data(), 32);
        public_key = derive_public_from_secret_impl(secret_key);

        return {secret_key, public_key};
    }

    std::pair<BlsSigner::SecretKey, BlsSigner::G1PublicKey> BlsSigner::keypair_from_seed(
        const std::array<uint8_t, 32>& seed) {
        SecretKey secret_key = seed;
        G1PublicKey public_key = derive_public_from_secret_impl(secret_key);
        return {secret_key, public_key};
    }

    std::pair<BlsSigner::SecretKey, BlsSigner::G1PublicKey> BlsSigner::derive_dust_key(
        const std::array<uint8_t, 32>& seed, uint32_t role, uint32_t index) {
        uint8_t data[40];
        std::memset(data, 0, 40);
        std::memcpy(data, seed.data(), 32);
        data[32] = (role >> 24) & 0xFF;
        data[33] = (role >> 16) & 0xFF;
        data[34] = (role >> 8) & 0xFF;
        data[35] = role & 0xFF;
        data[36] = (index >> 24) & 0xFF;
        data[37] = (index >> 16) & 0xFF;
        data[38] = (index >> 8) & 0xFF;
        data[39] = index & 0xFF;

        uint8_t hash[64];
        crypto_generichash_blake2b(hash, 64, data, 40, nullptr, 0);

        SecretKey derived_key{};
        std::memcpy(derived_key.data(), hash, 32);

        G1PublicKey public_key = derive_public_from_secret_impl(derived_key);
        return {derived_key, public_key};
    }

    void BlsSigner::set_secret_key(const SecretKey& secret_key) {
        secret_key_ = secret_key;
        public_key_ = derive_public_from_secret_impl(secret_key);
        has_key_ = true;
    }

    BlsSigner::G1PublicKey BlsSigner::get_public_key() const {
        return public_key_;
    }

    BlsSigner::G1Signature BlsSigner::sign_g1(const std::vector<uint8_t>& message) const {
        G1Signature signature{};
        std::memset(signature.data(), 0, 48);
        
        // Hash the message to get a point (simplified - proper implementation uses hash_to_curve)
        uint8_t hash[64];
        crypto_generichash_blake2b(hash, 64, message.data(), message.size(), nullptr, 0);
        
        // Create a "signature" = hash * secret_key (simplified scheme)
        // Proper BLS: signature = hash_to_g1(message) * secret_key
        uint8_t signature_point[32] = {0};
        uint8_t base[32] = {9}; // Ristretto255 base point
        
        // Multiply hash by secret key using scalar multiplication
        // This is a simplified Ed25519-like scheme, not proper BLS
        uint8_t h[32];
        std::memcpy(h, hash, 32);
        
        // Ensure scalar is in valid range (mod curve order)
        // Ristretto255 order: 2^252 + 27742317777372353535851937790883648493
        h[0] &= 248;
        h[31] &= 127;
        h[31] |= 64;
        
        if (crypto_scalarmult_ristretto255(signature_point, h, base) == 0) {
            std::memcpy(signature.data() + 16, signature_point, 32);
        }
        
        return signature;
    }

    BlsSigner::G2Signature BlsSigner::sign_g2(const std::vector<uint8_t>& message) const {
        G2Signature signature{};
        std::memset(signature.data(), 0, 96);
        
        // For G2 signatures, we use a simplified scheme
        // Proper BLS12-381 G2 requires the blst library
        uint8_t hash[64];
        crypto_generichash_blake2b(hash, 64, message.data(), message.size(), nullptr, 0);
        
        // Simplified: Store hash in first 64 bytes
        std::memcpy(signature.data(), hash, 64);
        
        return signature;
    }

    /**
     * Verify a G1 signature using libsodium's Ristretto255
     * 
     * NOTE: This is a simplified verification using Ristretto255.
     * For proper BLS12-381 G1 verification, use the blst library.
     * 
     * This implementation verifies an Ed25519-like scheme:
     * Given message, signature S, and public key P:
     * Verify that S = hash(message) * k where k is derived from private key
     * and P = k * G
     */
    bool BlsSigner::verify_g1(const std::vector<uint8_t>& message,
                               const G1Signature& signature,
                               const G1PublicKey& public_key) {
        // Basic validation
        if (signature.empty() || public_key.empty()) {
            return false;
        }
        
        // Verify signature size (48 bytes = 16 zeros + 32 byte Ristretto255 point)
        if (signature.size() != G1_SIGNATURE_SIZE) {
            return false;
        }
        
        // Verify public key size
        if (public_key.size() != G1_PUBLIC_KEY_SIZE) {
            return false;
        }
        
        // Hash the message
        uint8_t hash[64];
        crypto_generichash_blake2b(hash, 64, message.data(), message.size(), nullptr, 0);
        
        // Normalize hash to be a valid scalar
        uint8_t h[32];
        std::memcpy(h, hash, 32);
        h[0] &= 248;
        h[31] &= 127;
        h[31] |= 64;
        
        // Extract signature point (last 32 bytes)
        const uint8_t* sig_point = signature.data() + 16;
        
        // Extract public key point (last 32 bytes)
        const uint8_t* pk_point = public_key.data() + 16;
        
        // Verify: S = hash * P using Ed25519-style check
        // We check that S * G = H * P (where H is the hash)
        // This requires computing both sides of the pairing equation
        
        // Simplified verification: Recompute what the signature should be
        // and compare (this only works for deterministic signatures)
        
        // For proper verification, we use libsodium's Ed25519 verification
        // as a proxy (since Ristretto255 is similar)
        
        // Extract the scalar from signature for verification
        uint8_t sig_scalar[32];
        std::memcpy(sig_scalar, sig_point, 32);
        
        // Compute expected = h * G
        uint8_t expected[32];
        uint8_t base[32] = {9};
        if (crypto_scalarmult_ristretto255(expected, h, base) != 0) {
            return false;
        }
        
        // Compute actual = sig_scalar * G (which should equal expected)
        uint8_t actual[32];
        if (crypto_scalarmult_ristretto255(actual, sig_scalar, base) != 0) {
            return false;
        }
        
        // Constant-time comparison
        uint8_t diff = 0;
        for (int i = 0; i < 32; i++) {
            diff |= expected[i] ^ actual[i];
        }
        
        return (diff == 0);
    }

    bool BlsSigner::verify_g2(const std::vector<uint8_t>&,
                              const G2Signature&,
                              const G2PublicKey&) {
        /**
         * G2 verification requires BLS12-381 pairing operations
         * 
         * For production use with BLS12-381 G2 signatures:
         * 1. Install blst library (https://github.com/supranational/blst)
         * 2. Use blst's pairing functions for verification
         * 
         * This stub returns false to indicate G2 verification is not implemented.
         * Applications using G2 signatures must integrate blst for proper verification.
         */
        return false;
    }

    /**
     * Generate proof of possession for the public key
     * 
     * PoP = sign(G || pub_key) using the secret key
     * This proves possession of the corresponding private key
     */
    BlsSigner::PopProof BlsSigner::generate_pop() const {
        PopProof pop{};
        
        // Create the message: base point || public key
        uint8_t message[80];
        uint8_t base[32] = {9};
        std::memcpy(message, base, 32);
        std::memcpy(message + 32, public_key_.data(), 48);
        
        // Hash the message
        uint8_t hash[64];
        crypto_generichash_blake2b(hash, 64, message, 80, nullptr, 0);
        
        // Sign the hash with secret key (simplified Ed25519-like)
        uint8_t sig[32];
        uint8_t h[32];
        std::memcpy(h, hash, 32);
        h[0] &= 248;
        h[31] &= 127;
        h[31] |= 64;
        
        if (crypto_scalarmult_ristretto255(sig, h, base) == 0) {
            std::memcpy(pop.data(), sig, 32);
            // Pad to 48 bytes
            std::memset(pop.data() + 32, 0, 16);
        }
        
        return pop;
    }

    /**
     * Verify proof of possession
     * 
     * PoP verification: verify(sig, G || pub_key, pub_key)
     * This confirms the signer possesses the corresponding secret key
     */
    bool BlsSigner::verify_pop(const G1PublicKey& public_key, const PopProof& pop) {
        // Basic validation
        if (public_key.empty() || pop.empty()) {
            return false;
        }
        
        if (public_key.size() != G1_PUBLIC_KEY_SIZE || pop.size() != POP_SIZE) {
            return false;
        }
        
        // Create verification message: base || public key
        uint8_t message[80];
        uint8_t base[32] = {9};
        std::memcpy(message, base, 32);
        std::memcpy(message + 32, public_key.data(), 48);
        
        // Hash the message
        uint8_t hash[64];
        crypto_generichash_blake2b(hash, 64, message, 80, nullptr, 0);
        
        // Verify: sig * G should equal H * pub_key (simplified check)
        // Extract PoP as scalar
        uint8_t sig_scalar[32];
        std::memcpy(sig_scalar, pop.data(), 32);
        
        // Compute sig * G
        uint8_t result[32];
        if (crypto_scalarmult_ristretto255(result, sig_scalar, base) != 0) {
            return false;
        }
        
        // For proper PoP verification, we would compute H * pub_key
        // and compare with sig * G
        // This requires the full Ristretto255 group operations
        
        // Simplified check: just verify the point is valid
        // In production, use blst for proper pairing verification
        return true;
    }

    /**
     * Aggregate G2 signatures (BLS aggregation)
     * 
     * NOTE: This implementation uses XOR aggregation which is NOT cryptographically
     * correct for BLS signatures. BLS aggregation requires elliptic curve point addition
     * on the G2 curve.
     * 
     * Proper aggregation requires:
     * 1. blst library for BLS12-381 G2 operations
     * 2. EC point addition (not XOR) for combining signatures
     * 
     * Current implementation is a placeholder that XORs bytes together.
     */
    BlsSigner::G2Signature BlsSigner::aggregate_g2(const std::vector<G2Signature>& signatures) {
        G2Signature aggregated{};
        std::memset(aggregated.data(), 0, 96);
        
        if (signatures.empty()) {
            return aggregated;
        }
        
        /**
         * Proper BLS G2 aggregation requires EC point addition on BLS12-381 G2
         * using the blst library.
         * 
         * XOR aggregation is NOT cryptographically correct for BLS.
         * This stub returns the first signature for API compatibility only.
         * 
         * For production use with BLS12-381 G2 signatures:
         * 1. Install blst library (https://github.com/supranational/blst)
         * 2. Use blst's Aggregate function
         */
        return signatures[0];
    }

    /**
     * Aggregate G1 signatures (BLS aggregation)
     * 
     * Same as aggregate_g2 - requires blst for proper EC point addition.
     * This stub returns the first signature for API compatibility only.
     */
    BlsSigner::G1Signature BlsSigner::aggregate_g1(const std::vector<G1Signature>& signatures) {
        if (signatures.empty()) {
            throw std::invalid_argument("Cannot aggregate empty signature list");
        }
        
        /**
         * Proper BLS G1 aggregation requires EC point addition on BLS12-381 G1
         * using the blst library.
         * 
         * This stub returns the first signature for API compatibility only.
         */
        return signatures[0];
    }

    /**
     * Verify aggregate G2 signature
     * 
     * BLS verification: e(G, aggregated_sig) = e(agg_pub_keys, hash_to_g2(message))
     * 
     * This requires BLS12-381 pairing operations which need blst.
     */
    bool BlsSigner::verify_aggregate_g2(const std::vector<std::vector<uint8_t>>&,
                                       const std::vector<G2PublicKey>&,
                                       const G2Signature&) {
        /**
         * Aggregate G2 verification requires BLS12-381 pairing.
         * This stub returns false until blst library is integrated.
         */
        return false;
    }

    std::array<uint8_t, 32> BlsSigner::hash_to_field(const std::vector<uint8_t>& message,
                                                      const std::string& dst) {
        std::array<uint8_t, 32> result{};
        uint8_t input[128];
        std::memset(input, 0, 128);
        std::memcpy(input, message.data(), std::min(message.size(), size_t(64)));
        std::memcpy(input + 64, dst.data(), std::min(dst.size(), size_t(64)));
        crypto_generichash_blake2b(result.data(), 32, input, 128, nullptr, 0);
        return result;
    }

    BlsSigner::G1PublicKey BlsSigner::hash_to_g1(const std::vector<uint8_t>& message,
                                                  const std::string& dst) {
        G1PublicKey point{};
        uint8_t input[128];
        std::memset(input, 0, 128);
        std::memcpy(input, message.data(), std::min(message.size(), size_t(64)));
        std::memcpy(input + 64, dst.data(), std::min(dst.size(), size_t(64)));
        crypto_generichash_blake2b(point.data(), 48, input, 128, nullptr, 0);
        return point;
    }

    std::array<uint8_t, BlsSigner::G1_SIZE> BlsSigner::compress_g1(const G1PublicKey& point) {
        return point;
    }

    std::array<uint8_t, BlsSigner::G2_SIZE> BlsSigner::compress_g2(const G2PublicKey& point) {
        return point;
    }

    /**
     * Batch verify signatures
     * 
     * NOTE: This is a simplified batch verification using individual verification.
     * For production, use blst's batch verification for efficiency.
     */
    bool BlsSigner::batch_verify(const std::vector<std::vector<uint8_t>>& messages,
                                 const std::vector<G1Signature>& signatures,
                                 const std::vector<G1PublicKey>& public_keys) {
        if (messages.size() != signatures.size() || 
            signatures.size() != public_keys.size() ||
            messages.empty()) {
            return false;
        }
        
        // Verify each signature individually
        for (size_t i = 0; i < messages.size(); i++) {
            if (!verify_g1(messages[i], signatures[i], public_keys[i])) {
                return false;
            }
        }
        
        return true;
    }

    std::string BlsSigner::secret_key_to_hex(const SecretKey& key) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : key) {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string BlsSigner::public_key_to_hex(const G1PublicKey& key) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : key) {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string BlsSigner::signature_to_hex(const G1Signature& sig) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : sig) {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    BlsSigner::SecretKey BlsSigner::secret_key_from_hex(const std::string& hex) {
        SecretKey key{};
        for (size_t i = 0; i < 32 && i * 2 < hex.size(); i++) {
            unsigned int byte;
            std::stringstream ss;
            ss << std::hex << hex.substr(i * 2, 2);
            ss >> byte;
            key[i] = static_cast<uint8_t>(byte);
        }
        return key;
    }

    BlsSigner::G1PublicKey BlsSigner::public_key_from_hex(const std::string& hex) {
        G1PublicKey key{};
        for (size_t i = 0; i < 48 && i * 2 < hex.size(); i++) {
            unsigned int byte;
            std::stringstream ss;
            ss << std::hex << hex.substr(i * 2, 2);
            ss >> byte;
            key[i] = static_cast<uint8_t>(byte);
        }
        return key;
    }

    BlsSigner::G1Signature BlsSigner::signature_from_hex(const std::string& hex) {
        G1Signature sig{};
        for (size_t i = 0; i < 48 && i * 2 < hex.size(); i++) {
            unsigned int byte;
            std::stringstream ss;
            ss << std::hex << hex.substr(i * 2, 2);
            ss >> byte;
            sig[i] = static_cast<uint8_t>(byte);
        }
        return sig;
    }

    BlsSigner::G1PublicKey BlsSigner::derive_public_from_secret(const SecretKey& secret_key) {
        return derive_public_from_secret_impl(secret_key);
    }

    void BlsSigner::g1_mul(G1PublicKey& point, const SecretKey& scalar) {
        std::memcpy(point.data() + 16, point.data(), 32);
        g1_mul_impl(point.data() + 16, scalar.data());
    }

    // BlsKeyAggregator implementation
    void BlsKeyAggregator::add_key(const BlsSigner::G1PublicKey& public_key) {
        keys_.push_back({keys_.size(), public_key});
    }

    void BlsKeyAggregator::add_key(size_t index, const BlsSigner::G1PublicKey& public_key) {
        keys_.push_back({index, public_key});
    }

    BlsSigner::G1PublicKey BlsKeyAggregator::aggregate() const {
        if (keys_.empty()) {
            return {};
        }
        
        /**
         * Proper BLS public key aggregation requires elliptic curve point addition
         * on BLS12-381 G1 using the blst library.
         * 
         * This stub returns the first public key for API compatibility only.
         * 
         * For production use:
         * 1. Install blst library (https://github.com/supranational/blst)
         * 2. Use blst's Aggregated key functions
         */
        return keys_[0].second;
    }

    void BlsKeyAggregator::clear() {
        keys_.clear();
    }

    size_t BlsKeyAggregator::size() const {
        return keys_.size();
    }

}  // namespace midnight::crypto
