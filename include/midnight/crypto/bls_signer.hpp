#pragma once

/**
 * @file bls_signer.hpp
 * @brief BLS key helpers and guarded DUST signature API
 *
 * Midnight uses BLS signatures on the BLS12-381 curve for DUST transactions.
 * This C++ module does not currently include a BLS12-381 backend; signing,
 * verification, proof-of-possession, and aggregation fail closed rather than
 * using incompatible fallback math.
 */

#ifndef MIDNIGHT_CRYPTO_BLS_SIGNER_HPP
#define MIDNIGHT_CRYPTO_BLS_SIGNER_HPP

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <memory>

namespace midnight::crypto
{

    /**
     * BLS12-381 curve types
     */
    enum class BlsCurveType
    {
        G1,    // 381-bit twist curve, 48-byte representation
        G2     // 12-degree twist curve, 96-byte representation
    };

    /**
     * BLS signature scheme types
     */
    enum class BlsSchemeType
    {
        Basic,      // Basic scheme (membership proof)
        Pop,        // Proof of possession (Proof of possession)
        Aug,        // Augmentation scheme
        PopAgg       // Proof of possession with aggregation
    };

    /**
     * BLS Signer for DUST transactions
     *
     * Midnight uses BLS12-381 G1 for public keys and signatures,
     * G2 for aggregate signatures.
     *
     * Key sizes:
     * - Secret key: 32 bytes
     * - Public key (G1): 48 bytes
     * - Signature (G1 or G2 depending on scheme): 48 or 96 bytes
     * - Pop proof: 48 bytes
     */
    class BlsSigner
    {
    public:
        // Key sizes
        static constexpr size_t SECRET_KEY_SIZE = 32;
        static constexpr size_t G1_PUBLIC_KEY_SIZE = 48;
        static constexpr size_t G2_PUBLIC_KEY_SIZE = 96;
        static constexpr size_t G1_SIGNATURE_SIZE = 48;
        static constexpr size_t G2_SIGNATURE_SIZE = 96;
        static constexpr size_t POP_SIZE = 48;
        static constexpr size_t G1_SIZE = 48;
        static constexpr size_t G2_SIZE = 96;
        static constexpr size_t G1_XCOORD_SIZE = 48;
        static constexpr size_t G2_XCOORD_SIZE = 96;

        // Type aliases
        using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;
        using G1PublicKey = std::array<uint8_t, G1_PUBLIC_KEY_SIZE>;
        using G2PublicKey = std::array<uint8_t, G2_PUBLIC_KEY_SIZE>;
        using G1Signature = std::array<uint8_t, G1_SIGNATURE_SIZE>;
        using G2Signature = std::array<uint8_t, G2_SIGNATURE_SIZE>;
        using PopProof = std::array<uint8_t, POP_SIZE>;

        /**
         * Initialize the BLS signer
         */
        BlsSigner();

        /**
         * Generate a random BLS keypair
         */
        static std::pair<SecretKey, G1PublicKey> generate_keypair();

        /**
         * Generate keypair from seed (deterministic)
         * Used for HD wallet derivation of DUST keys
         */
        static std::pair<SecretKey, G1PublicKey> keypair_from_seed(
            const std::array<uint8_t, 32>& seed);

        /**
         * Derive BLS key for DUST address from seed
         * Implements Midnight's BLS scalar reduction for DUST keys
         *
         * @param seed 32-byte seed
         * @param role DUST role identifier
         * @param index Key index
         * @return Tuple of (secret_key, public_key)
         */
        static std::pair<SecretKey, G1PublicKey> derive_dust_key(
            const std::array<uint8_t, 32>& seed,
            uint32_t role,
            uint32_t index);

        /**
         * Set secret key from bytes
         */
        void set_secret_key(const SecretKey& secret_key);

        /**
         * Get public key from current secret key
         */
        G1PublicKey get_public_key() const;

        /**
         * Sign a message (G1 signature)
         */
        G1Signature sign_g1(const std::vector<uint8_t>& message) const;

        /**
         * Sign a message (G2 signature for aggregation)
         */
        G2Signature sign_g2(const std::vector<uint8_t>& message) const;

        /**
         * Verify a G1 signature
         */
        static bool verify_g1(
            const std::vector<uint8_t>& message,
            const G1Signature& signature,
            const G1PublicKey& public_key);

        /**
         * Verify a G2 signature
         */
        static bool verify_g2(
            const std::vector<uint8_t>& message,
            const G2Signature& signature,
            const G2PublicKey& public_key);

        /**
         * Generate proof of possession
         */
        PopProof generate_pop() const;

        /**
         * Verify proof of possession
         */
        static bool verify_pop(
            const G1PublicKey& public_key,
            const PopProof& pop);

        /**
         * Aggregate multiple signatures
         */
        static G2Signature aggregate_g2(
            const std::vector<G2Signature>& signatures);

        /**
         * Aggregate G1 signatures
         */
        static G1Signature aggregate_g1(
            const std::vector<G1Signature>& signatures);

        /**
         * Verify aggregate signature
         */
        static bool verify_aggregate_g2(
            const std::vector<std::vector<uint8_t>>& messages,
            const std::vector<G2PublicKey>& public_keys,
            const G2Signature& aggregate_signature);

        /**
         * Hash to field element (BLS12-381)
         * Used for message hashing before signing
         */
        static std::array<uint8_t, 32> hash_to_field(
            const std::vector<uint8_t>& message,
            const std::string& dst);

        /**
         * Hash to G1 point
         * Used for deriving public keys from seeds
         */
        static G1PublicKey hash_to_g1(
            const std::vector<uint8_t>& message,
            const std::string& dst);

        /**
         * Compress G1 point to 48 bytes
         */
        static std::array<uint8_t, G1_SIZE> compress_g1(const G1PublicKey& point);

        /**
         * Compress G2 point to 96 bytes
         */
        static std::array<uint8_t, G2_SIZE> compress_g2(const G2PublicKey& point);

        /**
         * Batch verify signatures
         * More efficient than individual verification
         */
        static bool batch_verify(
            const std::vector<std::vector<uint8_t>>& messages,
            const std::vector<G1Signature>& signatures,
            const std::vector<G1PublicKey>& public_keys);

        /**
         * Convert secret key to hex
         */
        static std::string secret_key_to_hex(const SecretKey& key);

        /**
         * Convert public key to hex
         */
        static std::string public_key_to_hex(const G1PublicKey& key);

        /**
         * Convert signature to hex
         */
        static std::string signature_to_hex(const G1Signature& sig);

        /**
         * Parse hex to secret key
         */
        static SecretKey secret_key_from_hex(const std::string& hex);

        /**
         * Parse hex to public key
         */
        static G1PublicKey public_key_from_hex(const std::string& hex);

        /**
         * Parse hex to signature
         */
        static G1Signature signature_from_hex(const std::string& hex);

    private:
        SecretKey secret_key_;
        G1PublicKey public_key_;
        bool has_key_;

        /**
         * Derive public key from secret key (G1)
         */
        static G1PublicKey derive_public_from_secret(const SecretKey& secret_key);

        /**
         * Scalar multiplication on G1
         */
        static void g1_mul(G1PublicKey& point, const SecretKey& scalar);
    };

    /**
     * BLS Key Aggregator
     * Used for aggregating public keys for threshold signatures
     */
    class BlsKeyAggregator
    {
    public:
        /**
         * Add a public key to the aggregation set
         */
        void add_key(const BlsSigner::G1PublicKey& public_key);

        /**
         * Add a public key with its index
         */
        void add_key(size_t index, const BlsSigner::G1PublicKey& public_key);

        /**
         * Aggregate all added public keys
         */
        BlsSigner::G1PublicKey aggregate() const;

        /**
         * Clear all keys
         */
        void clear();

        /**
         * Get number of keys
         */
        size_t size() const;

    private:
        std::vector<std::pair<size_t, BlsSigner::G1PublicKey>> keys_;
    };

} // namespace midnight::crypto

#endif // MIDNIGHT_CRYPTO_BLS_SIGNER_HPP
