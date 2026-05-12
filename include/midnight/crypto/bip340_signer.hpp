#pragma once
/**
 * @file bip340_signer.hpp
 * @brief BIP-340 Schnorr signature implementation for Midnight
 *
 * BIP-340 (Schnorr signatures for secp256k1) is used by Midnight for
 * unshielded transaction signing. This implementation follows the BIP-340
 * specification exactly to match the Midnight SDK's signTransaction behavior.
 */

#ifndef MIDNIGHT_CRYPTO_BIP340_SIGNER_HPP
#define MIDNIGHT_CRYPTO_BIP340_SIGNER_HPP

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <sstream>
#include <iomanip>

namespace midnight::crypto
{

    /**
     * BIP-340 Schnorr signature implementation for secp256k1
     *
     * Midnight uses BIP-340 (Schnorr signatures) for unshielded transaction signing,
     * NOT Ed25519. This matches the SDK's use of:
     *   - signingKeyFromBip340() for private key to signing key
     *   - signatureVerifyingKey for the public verification key
     *
     * BIP-340 specifics:
     * - 32-byte x-only public key (only x-coordinate of point)
     * - 64-byte signature (R.x || s)
     * - Message is tagged with "BIP0340/challenge" for signing
     * - Uses SHA-256 for hashing (tagged hash)
     */
    class Bip340Signer
    {
    public:
        static constexpr size_t PRIVATE_KEY_SIZE = 32;   // secp256k1 scalar
        static constexpr size_t PUBLIC_KEY_SIZE = 32;    // x-only (32 bytes)
        static constexpr size_t SIGNATURE_SIZE = 64;     // R.x (32) || s (32)
        static constexpr size_t SECRET_SEED_SIZE = 64;   // HMAC-SHA512 output
        static constexpr size_t HASH_SIZE = 32;        // SHA-256 output

        using PrivateKey = std::array<uint8_t, PRIVATE_KEY_SIZE>;
        using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
        using Signature = std::array<uint8_t, SIGNATURE_SIZE>;

        Bip340Signer();

        /**
         * Create signer from hex string (0x-prefixed or not)
         */
        explicit Bip340Signer(const std::string& hex_sk);

        /**
         * Set the secret key from raw bytes
         */
        void set_key(const std::array<uint8_t, 32>& secret_key);

        /**
         * Sign a message using BIP-340 Schnorr
         * @param message Message to sign
         * @return Hex string of 64-byte signature
         */
        std::string sign_message(const std::vector<uint8_t>& message) const;

        /**
         * Verify a BIP-340 signature
         * @param message Original message
         * @param signature_hex Hex string of 64-byte signature
         * @param public_key_hex Hex string of 32-byte x-only public key
         * @return true if valid
         */
        static bool verify_signature(
            const std::vector<uint8_t>& message,
            const std::string& signature_hex,
            const std::string& public_key_hex);

        /**
         * Derive public key from private key
         */
        static std::array<uint8_t, 32> derive_public_key(const std::array<uint8_t, 32>& secret_key);

        /**
         * Generate a random BIP-340 keypair
         * @return Pair of (private_key, public_key) as byte arrays
         */
        static std::pair<std::array<uint8_t, 32>, std::array<uint8_t, 32>> generate_keypair();

        /**
         * Convert signature to hex string
         */
        static std::string signature_to_hex(const std::array<uint8_t, 64>& signature);

        /**
         * Convert public key to hex string
         */
        static std::string public_key_to_hex(const std::array<uint8_t, 32>& public_key);

    private:
        PrivateKey secret_key_;
        PublicKey public_key_;
        bool has_key_;

        /**
         * Compute x-only public key from private key (BIP-340 point derivation)
         */
        static std::array<uint8_t, 32> derive_pubkey(const uint8_t* private_key);
    };

} // namespace midnight::crypto

#endif // MIDNIGHT_CRYPTO_BIP340_SIGNER_HPP
