/**
 * @file bls_signer.cpp
 * @brief BLS guard implementation.
 *
 * Midnight DUST keys and signatures require BLS12-381-compatible primitives.
 * This file keeps deterministic key-material helpers, but signing,
 * verification, proof-of-possession, and aggregation fail closed unless a real
 * BLS12-381 backend is integrated.
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

        // Temporary local derivation used only to keep existing address code stable.
        // It is not a BLS signature backend.
        BlsSigner::G1PublicKey derive_public_from_secret_impl(const BlsSigner::SecretKey& secret_key) {
            BlsSigner::G1PublicKey public_key{};
            uint8_t point[32] = {0};
            uint8_t base[32] = {9};
            if (crypto_scalarmult_ristretto255(point, secret_key.data(), base) == 0) {
                std::memset(public_key.data(), 0, 16);
                std::memcpy(public_key.data() + 16, point, 32);
            }
            return public_key;
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
        (void)message;
        throw std::runtime_error("BLS G1 signing requires a BLS12-381 backend");
    }

    BlsSigner::G2Signature BlsSigner::sign_g2(const std::vector<uint8_t>& message) const {
        (void)message;
        throw std::runtime_error("BLS G2 signing requires a BLS12-381 backend");
    }

    bool BlsSigner::verify_g1(const std::vector<uint8_t>& message,
                               const G1Signature& signature,
                               const G1PublicKey& public_key) {
        (void)message;
        (void)signature;
        (void)public_key;
        return false;
    }

    bool BlsSigner::verify_g2(const std::vector<uint8_t>&,
                              const G2Signature&,
                              const G2PublicKey&) {
        return false;
    }

    BlsSigner::PopProof BlsSigner::generate_pop() const {
        throw std::runtime_error("BLS proof-of-possession generation requires a BLS12-381 backend");
    }

    bool BlsSigner::verify_pop(const G1PublicKey& public_key, const PopProof& pop) {
        (void)public_key;
        (void)pop;
        return false;
    }

    BlsSigner::G2Signature BlsSigner::aggregate_g2(const std::vector<G2Signature>& signatures) {
        if (signatures.empty()) {
            throw std::invalid_argument("Cannot aggregate empty signature list");
        }

        throw std::runtime_error("BLS G2 aggregation requires a BLS12-381 backend");
    }

    BlsSigner::G1Signature BlsSigner::aggregate_g1(const std::vector<G1Signature>& signatures) {
        if (signatures.empty()) {
            throw std::invalid_argument("Cannot aggregate empty signature list");
        }
        
        throw std::runtime_error("BLS G1 aggregation requires a BLS12-381 backend");
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
        (void)message;
        (void)dst;
        throw std::runtime_error("BLS hash_to_g1 requires a BLS12-381 hash-to-curve implementation");
    }

    std::array<uint8_t, BlsSigner::G1_SIZE> BlsSigner::compress_g1(const G1PublicKey& point) {
        return point;
    }

    std::array<uint8_t, BlsSigner::G2_SIZE> BlsSigner::compress_g2(const G2PublicKey& point) {
        return point;
    }

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
        (void)point;
        (void)scalar;
        throw std::runtime_error("BLS G1 multiplication requires a BLS12-381 backend");
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

        throw std::runtime_error("BLS public key aggregation requires a BLS12-381 backend");
    }

    void BlsKeyAggregator::clear() {
        keys_.clear();
    }

    size_t BlsKeyAggregator::size() const {
        return keys_.size();
    }

}  // namespace midnight::crypto
