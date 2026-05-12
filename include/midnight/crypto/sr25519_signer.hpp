#pragma once

/**
 * @file sr25519_signer.hpp
 * @brief SR25519 signature implementation for AURA block authorship
 *
 * Midnight uses SR25519 for AURA block authorship consensus.
 * This implementation uses libsodium's Ristretto255 which provides
 * the same high-level interface as SR25519 and is wire-compatible
 * for most Substrate-based chains.
 *
 * NOTE: For full SR25519 compatibility with Polkadot/Midnight mainnet,
 * consider using the schnorrkel library via FFI or the WebAssembly build.
 */

#ifndef MIDNIGHT_CRYPTO_SR25519_SIGNER_HPP
#define MIDNIGHT_CRYPTO_SR25519_SIGNER_HPP

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace midnight::crypto
{

    /**
     * SR25519 signature scheme for AURA block authorship
     *
     * Uses Ristretto255 from libsodium which provides:
     * - Same security properties as SR25519 (cofactor 8)
     * - Wire-compatible with many Substrate chains
     * - High performance implementation
     *
     * Key differences from Ed25519:
     * - Uses "V" (Vrf as Verifiable Random Function) prefix
     * - VRF output format compatible with Substrate's Sr25519VRF
     * - Chain code support for hierarchical derivation
     */
    class Sr25519Signer
    {
    public:
        // SR25519 key sizes
        static constexpr size_t SECRET_KEY_SIZE = 64;   // 32-byte seed + 32-byte public
        static constexpr size_t PUBLIC_KEY_SIZE = 32;
        static constexpr size_t SIGNATURE_SIZE = 64;
        static constexpr size_t SEED_SIZE = 32;
        static constexpr size_t CHAIN_CODE_SIZE = 32;

        using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;
        using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
        using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
        using Seed = std::array<uint8_t, SEED_SIZE>;
        using ChainCode = std::array<uint8_t, CHAIN_CODE_SIZE>;

        /**
         * Initialize the signer (libsodium)
         */
        static void initialize();

        /**
         * Generate a new SR25519 keypair
         */
        static std::pair<SecretKey, PublicKey> generate_keypair();

        /**
         * Generate keypair from seed (deterministic)
         * Same as BIP39 seed -> SLIP-0010 Ed25519 derivation
         */
        static std::pair<SecretKey, PublicKey> keypair_from_seed(const Seed& seed);

        /**
         * Derive child key from parent secret key and chain code
         * Implements SLIP-0010 derivation for SR25519
         *
         * @param parent_key Parent secret key (64 bytes)
         * @param chain_code Chain code for derivation (32 bytes)
         * @param index Child index (hardened: >= 0x80000000)
         * @return Tuple of (child_secret_key, child_chain_code)
         */
        static std::tuple<SecretKey, ChainCode> derive_child_key(
            const SecretKey& parent_key,
            const ChainCode& chain_code,
            uint32_t index);

        /**
         * Derive a hardended child key from seed
         * Standard HD wallet derivation: m/44'/coin_type'/account'/role/index
         *
         * @param seed BIP39 seed (64 bytes from PBKDF2)
         * @param derivation_path Path components e.g., {44', 2400', 0', 0, 0}
         * @return Tuple of (secret_key, public_key, chain_code)
         */
        static std::tuple<SecretKey, PublicKey, ChainCode> derive_path(
            const std::array<uint8_t, 64>& seed,
            const std::vector<uint32_t>& derivation_path);

        /**
         * Extract public key from secret key
         */
        static PublicKey extract_public_key(const SecretKey& secret_key);

        /**
         * Sign a message
         *
         * @param message Message bytes
         * @param secret_key Secret key (64 bytes)
         * @return 64-byte signature
         */
        static Signature sign(
            const std::vector<uint8_t>& message,
            const SecretKey& secret_key);

        /**
         * Sign with chain code (for VRF output)
         *
         * @param message Message bytes
         * @param secret_key Secret key
         * @param chain_code Chain code for VRF context
         * @return Signature
         */
        static Signature sign_vrf(
            const std::vector<uint8_t>& message,
            const SecretKey& secret_key,
            const ChainCode& chain_code);

        /**
         * Verify a signature
         */
        static bool verify(
            const std::vector<uint8_t>& message,
            const Signature& signature,
            const PublicKey& public_key);

        /**
         * VRF verify (Substrate-style)
         */
        static bool verify_vrf(
            const std::vector<uint8_t>& message,
            const Signature& signature,
            const PublicKey& public_key,
            const ChainCode& chain_code);

        /**
         * Convert public key to hex string
         */
        static std::string public_key_to_hex(const PublicKey& key);

        /**
         * Convert secret key to hex string
         */
        static std::string secret_key_to_hex(const SecretKey& key);

        /**
         * Convert signature to hex string
         */
        static std::string signature_to_hex(const Signature& sig);

        /**
         * Parse hex string to public key
         */
        static PublicKey public_key_from_hex(const std::string& hex);

        /**
         * Parse hex string to secret key
         */
        static SecretKey secret_key_from_hex(const std::string& hex);

        /**
         * Parse hex string to signature
         */
        static Signature signature_from_hex(const std::string& hex);

        /**
         * Compute VRF output (deterministic random from message)
         * Used for randomness in consensus
         */
        static std::array<uint8_t, 32> compute_vrf_output(
            const std::vector<uint8_t>& message,
            const SecretKey& secret_key,
            const ChainCode& chain_code);

        /**
         * Verify VRF output
         */
        static bool verify_vrf_output(
            const std::array<uint8_t, 32>& vrf_output,
            const std::vector<uint8_t>& message,
            const Signature& signature,
            const PublicKey& public_key,
            const ChainCode& chain_code);

    private:
        /**
         * Internal: Derive public key from seed
         */
        static PublicKey derive_public_from_seed(const Seed& seed);
    };

} // namespace midnight::crypto

#endif // MIDNIGHT_CRYPTO_SR25519_SIGNER_HPP
