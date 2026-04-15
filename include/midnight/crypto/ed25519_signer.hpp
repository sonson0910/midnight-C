#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace midnight::crypto
{

    /**
     * Ed25519 Cryptographic Signing Module
     *
     * Provides real Ed25519 key generation, signing, and verification
     * using libsodium (sodium.h) - industry-standard cryptography library
     *
     * Features:
     * - Real Ed25519 key pairs (32-byte private key, 32-byte public key)
     * - Deterministic signing of messages
     * - Signature verification (64 bytes)
     * - Seed-based key derivation for deterministic wallets
     */

    class Ed25519Signer
    {
    public:
        // Key sizes (standard Ed25519)
        static constexpr size_t PUBLIC_KEY_SIZE = 32;
        static constexpr size_t PRIVATE_KEY_SIZE = 64; // private_key + public_key
        static constexpr size_t SECRET_SEED_SIZE = 32;
        static constexpr size_t SIGNATURE_SIZE = 64;

        typedef std::array<uint8_t, PUBLIC_KEY_SIZE> PublicKey;
        typedef std::array<uint8_t, PRIVATE_KEY_SIZE> PrivateKey;
        typedef std::array<uint8_t, SIGNATURE_SIZE> Signature;

        /**
         * Initialize the cryptography library (libsodium)
         * Must be called once at application startup
         */
        static void initialize();

        /**
         * Generate a new Ed25519 key pair
         * @return Pair of (public_key, private_key)
         */
        static std::pair<PublicKey, PrivateKey> generate_keypair();

        /**
         * Generate a key pair from a 32-byte seed
         * Deterministic - same seed produces same key pair
         * Used for HD wallets (BIP39/CIP1852 derivation)
         *
         * @param seed 32-byte seed value
         * @return Pair of (public_key, private_key)
         */
        static std::pair<PublicKey, PrivateKey> keypair_from_seed(
            const std::array<uint8_t, SECRET_SEED_SIZE> &seed);

        /**
         * Sign a message with the private key
         *
         * @param message The message bytes to sign
         * @param private_key The Ed25519 private key (64 bytes)
         * @return 64-byte Ed25519 signature
         *
         * @throws std::runtime_error if signing fails
         */
        static Signature sign_message(
            const std::string &message,
            const PrivateKey &private_key);

        /**
         * Sign a message with the private key (binary data)
         *
         * @param message The message bytes to sign
         * @param message_len Length of message
         * @param private_key The Ed25519 private key
         * @return 64-byte Ed25519 signature
         */
        static Signature sign_message(
            const uint8_t *message,
            size_t message_len,
            const PrivateKey &private_key);

        /**
         * Verify a signature
         *
         * @param message The original message that was signed
         * @param signature The 64-byte signature to verify
         * @param public_key The Ed25519 public key of signer
         * @return true if signature is valid, false otherwise
         */
        static bool verify_signature(
            const std::string &message,
            const Signature &signature,
            const PublicKey &public_key);

        /**
         * Verify a signature (binary data)
         *
         * @param message The original message bytes
         * @param message_len Length of message
         * @param signature The 64-byte signature
         * @param public_key The Ed25519 public key
         * @return true if valid
         */
        static bool verify_signature(
            const uint8_t *message,
            size_t message_len,
            const Signature &signature,
            const PublicKey &public_key);

        /**
         * Extract public key from private key
         * Private key format: [32 bytes secret | 32 bytes public]
         * This extracts the public key part
         *
         * @param private_key The 64-byte private key
         * @return The 32-byte public key
         */
        static PublicKey extract_public_key(const PrivateKey &private_key);

        /**
         * Convert public key to human-readable hex string
         * @param key The public key bytes
         * @return Hex string representation (64 characters)
         */
        static std::string public_key_to_hex(const PublicKey &key);

        /**
         * Convert hex string to public key
         * @param hex_string Hex string (exactly 64 characters)
         * @return Public key bytes
         * @throws std::invalid_argument if hex is malformed
         */
        static PublicKey public_key_from_hex(const std::string &hex_string);

        /**
         * Convert signature to human-readable hex string
         * @param sig The 64-byte signature
         * @return Hex string representation (128 characters)
         */
        static std::string signature_to_hex(const Signature &sig);

        /**
         * Convert hex string to signature
         * @param hex_string Hex string (exactly 128 characters)
         * @return Signature bytes
         * @throws std::invalid_argument if hex is malformed
         */
        static Signature signature_from_hex(const std::string &hex_string);
    };

} // namespace midnight::crypto
