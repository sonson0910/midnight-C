/**
 * Key Encryption Module
 * 
 * Secure key storage using Argon2id key derivation and AES-256-GCM encryption.
 * This implements secure key encryption for production use.
 * 
 * File format (JSON):
 * {
 *   "version": 1,
 *   "salt": "<hex:32 bytes>",
 *   "nonce": "<hex:12 bytes>",
 *   "ciphertext": "<hex:encrypted key>",
 *   "tag": "<hex:16 bytes auth tag>"
 * }
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <chrono>

namespace midnight::crypto {

/**
 * Key encryption configuration
 */
struct KeyEncryptionConfig {
    // Argon2id parameters
    uint32_t argon2_opslimit = 3;        // Memory operations (3 = interactive, 4 = moderate)
    size_t argon2_memlimit = 64 * 1024 * 1024;  // 64 MB
    
    // AES-256-GCM parameters
    static constexpr size_t KEY_SIZE = 32;         // 256 bits
    static constexpr size_t NONCE_SIZE = 12;      // 96 bits (recommended for GCM)
    static constexpr size_t TAG_SIZE = 16;       // 128 bits (standard GCM tag)
    static constexpr size_t SALT_SIZE = 32;       // 256 bits
    
    // Version for future upgrades
    static constexpr uint32_t CURRENT_VERSION = 1;
};

/**
 * Encrypted key container
 */
struct EncryptedKey {
    uint32_t version = KeyEncryptionConfig::CURRENT_VERSION;
    std::vector<uint8_t> salt;    // Argon2id salt
    std::vector<uint8_t> nonce;  // GCM nonce
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;    // Authentication tag
    
    std::string to_json() const;
    static std::optional<EncryptedKey> from_json(const std::string& json_str);
};

/**
 * KeyEncryption - Secure key encryption/decryption
 * 
 * Provides secure encryption of private keys using:
 * - Argon2id for password-based key derivation (memory-hard, side-channel resistant)
 * - AES-256-GCM for authenticated encryption (confidentiality + integrity)
 * 
 * Usage:
 *   // Encrypt a private key
 *   auto encrypted = KeyEncryption::encrypt_key(private_key, password);
 *   auto json = encrypted->to_json();
 *   save_to_file(json);
 * 
 *   // Decrypt a private key
 *   auto loaded = EncryptedKey::from_json(read_from_file());
 *   auto private_key = KeyEncryption::decrypt_key(*loaded, password);
 */
class KeyEncryption {
public:
    /**
     * Encrypt a key with password
     * @param key_bytes: Raw key bytes to encrypt
     * @param password: Password for key derivation
     * @param config: Encryption configuration (optional)
     * @return Encrypted key container, or nullopt on failure
     */
    static std::optional<EncryptedKey> encrypt_key(
        const std::vector<uint8_t>& key_bytes,
        const std::string& password,
        const KeyEncryptionConfig& config = KeyEncryptionConfig{});
    
    /**
     * Decrypt a key with password
     * @param encrypted: Encrypted key container
     * @param password: Password used for encryption
     * @return Decrypted key bytes, or nullopt on failure (wrong password or corrupted data)
     */
    static std::optional<std::vector<uint8_t>> decrypt_key(
        const EncryptedKey& encrypted,
        const std::string& password);
    
    /**
     * Verify password without decrypting (for key validation)
     * @param encrypted: Encrypted key container
     * @param password: Password to verify
     * @return true if password is correct
     */
    static bool verify_password(
        const EncryptedKey& encrypted,
        const std::string& password);
    
    /**
     * Encrypt a hex string key (common case for Midnight SDK)
     * @param hex_key: Hex-encoded key (with or without 0x prefix)
     * @param password: Password for key derivation
     * @param config: Encryption configuration (optional)
     * @return Encrypted key container, or nullopt on failure
     */
    static std::optional<EncryptedKey> encrypt_hex_key(
        const std::string& hex_key,
        const std::string& password,
        const KeyEncryptionConfig& config = KeyEncryptionConfig{});
    
    /**
     * Decrypt to hex string
     * @param encrypted: Encrypted key container
     * @param password: Password used for encryption
     * @return Decrypted key as hex string (with 0x prefix), or nullopt on failure
     */
    static std::optional<std::string> decrypt_hex_key(
        const EncryptedKey& encrypted,
        const std::string& password);
    
    /**
     * Generate cryptographically secure random bytes
     */
    static std::vector<uint8_t> generate_random_bytes(size_t count);

private:
    static constexpr uint8_t ARGON2ID_TYPE = 2;  // Argon2id variant
    
    /**
     * Derive key from password using Argon2id
     */
    static std::optional<std::vector<uint8_t>> derive_key_argon2id(
        const std::string& password,
        const std::vector<uint8_t>& salt,
        const KeyEncryptionConfig& config);
};

/**
 * Secure memory utilities
 */
class SecureMemory {
public:
    /**
     * Securely clear memory (optimized to prevent compiler optimization)
     * @param data: Pointer to memory to clear
     * @param size: Size of memory to clear
     */
    static void secure_zero(void* data, size_t size);
    
    /**
     * Securely compare two memory regions (constant-time)
     * @return true if equal
     */
    static bool constant_time_compare(
        const void* a, 
        const void* b, 
        size_t size);
};

} // namespace midnight::crypto
