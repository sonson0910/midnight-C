#pragma once

/**
 * @file ecdsa_signer.hpp
 * @brief ECDSA secp256k1 signature implementation for Cardano bridge
 *
 * Midnight uses ECDSA on secp256k1 for the Cardano partnerchain bridge.
 * This implementation uses OpenSSL's built-in secp256k1 curve support.
 */

#ifndef MIDNIGHT_CRYPTO_ECDSA_SIGNER_HPP
#define MIDNIGHT_CRYPTO_ECDSA_SIGNER_HPP

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace midnight::crypto
{

    /**
     * ECDSA secp256k1 signature scheme for Cardano bridge
     *
     * Supports:
     * - Key generation (random)
     * - Key derivation from seed
     * - Signing with RFC6979 deterministic nonce
     * - Verification
     * - Signature recovery
     */
    class EcdsaSigner
    {
    public:
        static constexpr size_t PRIVATE_KEY_SIZE = 32;   // secp256k1 scalar
        static constexpr size_t PUBLIC_KEY_SIZE = 33;    // Compressed (33 bytes)
        static constexpr size_t SIGNATURE_SIZE = 64;    // r || s (32 + 32)

        using PrivateKey = std::array<uint8_t, PRIVATE_KEY_SIZE>;
        using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
        using Signature = std::array<uint8_t, SIGNATURE_SIZE>;

        /**
         * Initialize the signer (loads OpenSSL)
         */
        EcdsaSigner();

        /**
         * Generate a new ECDSA keypair
         */
        static std::pair<PrivateKey, PublicKey> generate_keypair();

        /**
         * Generate keypair from seed (deterministic)
         */
        static std::pair<PrivateKey, PublicKey> keypair_from_seed(
            const std::array<uint8_t, 32>& seed);

        /**
         * Set private key from bytes
         */
        void set_private_key(const PrivateKey& private_key);

        /**
         * Get public key from current private key
         */
        PublicKey get_public_key() const;

        /**
         * Sign a message (returns DER-encoded signature as vector)
         */
        std::vector<uint8_t> sign_der(const std::vector<uint8_t>& message) const;

        /**
         * Sign a message (returns raw r||s format)
         */
        Signature sign_raw(const std::vector<uint8_t>& message) const;

        /**
         * Sign with deterministic nonce (RFC6979)
         */
        Signature sign_deterministic(const std::vector<uint8_t>& message) const;

        /**
         * Verify a signature (raw r||s format)
         */
        static bool verify_raw(
            const std::vector<uint8_t>& message,
            const Signature& signature,
            const PublicKey& public_key);

        /**
         * Verify a signature (DER format)
         */
        static bool verify_der(
            const std::vector<uint8_t>& message,
            const std::vector<uint8_t>& der_signature,
            const PublicKey& public_key);

        /**
         * Convert private key to hex string
         */
        static std::string private_key_to_hex(const PrivateKey& key);

        /**
         * Convert public key to hex string
         */
        static std::string public_key_to_hex(const PublicKey& key);

        /**
         * Convert signature to hex string
         */
        static std::string signature_to_hex(const Signature& sig);

        /**
         * Convert DER signature to hex
         */
        static std::string der_to_hex(const std::vector<uint8_t>& der);

        /**
         * Get address from public key (Keccak256 truncated)
         */
        static std::string public_key_to_address(const PublicKey& public_key);

    private:
        PrivateKey private_key_;
        PublicKey public_key_;
        bool has_key_;

        /**
         * Derive public key from private key
         */
        static PublicKey derive_public_key(const PrivateKey& private_key);

        /**
         * Parse compressed public key
         */
        static bool parse_compressed_public_key(
            const PublicKey& compressed,
            void* x, void* y, int* parity);

        /**
         * RFC6979 deterministic nonce generation
         */
        static std::array<uint8_t, 32> nonce_rfc6979(
            const std::vector<uint8_t>& message,
            const PrivateKey& private_key,
            const std::array<uint8_t, 32>& message_hash);
    };

} // namespace midnight::crypto

#endif // MIDNIGHT_CRYPTO_ECDSA_SIGNER_HPP
