#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace midnight::crypto {

/**
 * @brief AES-256-GCM state encryption for wallet serialization
 * 
 * Uses libsodium's crypto_secretbox_easy() which is XSalsa20-Poly1305
 * under the hood (actually XSalsa20-Poly1305, but equivalent security).
 */
class StateEncryption {
public:
    // Key is 32 bytes (256 bits)
    static constexpr size_t KEY_SIZE = 32;
    // Nonce is 24 bytes (192 bits)  
    static constexpr size_t NONCE_SIZE = 24;
    // Authentication tag is 16 bytes
    static constexpr size_t TAG_SIZE = 16;
    
    /**
     * @brief Derive encryption key from password using Argon2id
     * @param password User password
     * @param salt 16-byte random salt (output if nullptr)
     * @return 32-byte encryption key
     */
    static std::vector<uint8_t> derive_key_from_password(
        const std::string& password,
        std::vector<uint8_t>* salt = nullptr);
    
    /**
     * @brief Derive key with provided salt
     */
    static std::vector<uint8_t> derive_key_from_password(
        const std::string& password,
        const std::vector<uint8_t>& salt);
    
    /**
     * @brief Encrypt data with AES-256-GCM
     * @param plaintext Data to encrypt
     * @param key 32-byte encryption key
     * @return Encrypted data: nonce (24) + ciphertext + tag (16)
     */
    static std::vector<uint8_t> encrypt(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& key);
    
    /**
     * @brief Decrypt data
     * @param ciphertext Encrypted data (nonce + ciphertext + tag)
     * @param key 32-byte encryption key
     * @return Decrypted plaintext, or nullopt if decryption fails
     */
    static std::optional<std::vector<uint8_t>> decrypt(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key);
    
    /**
     * @brief Encrypt string data
     */
    static std::vector<uint8_t> encrypt_string(
        const std::string& plaintext,
        const std::vector<uint8_t>& key);
    
    /**
     * @brief Decrypt to string
     */
    static std::optional<std::string> decrypt_to_string(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key);
};

/**
 * @brief Encrypted wallet state format
 * 
 * Layout:
 * [1 byte version][16 bytes salt][24 bytes nonce][N bytes ciphertext+tag]
 */
class EncryptedState {
public:
    static constexpr uint8_t CURRENT_VERSION = 1;
    
    /**
     * @brief Create encrypted state from plaintext
     * @param plaintext Serialized wallet state (JSON string)
     * @param password User password
     * @return Encrypted state bytes
     */
    static std::vector<uint8_t> create(
        const std::string& plaintext,
        const std::string& password);
    
    /**
     * @brief Decrypt state
     * @param encrypted Encrypted state bytes
     * @param password User password
     * @return Decrypted wallet state, or nullopt
     */
    static std::optional<std::string> decrypt(
        const std::vector<uint8_t>& encrypted,
        const std::string& password);
    
    /**
     * @brief Verify password without decrypting
     * @param encrypted Encrypted state
     * @param password Password to verify
     * @return true if password is correct
     */
    static bool verify_password(
        const std::vector<uint8_t>& encrypted,
        const std::string& password);
    
    /**
     * @brief Check if data is encrypted state format
     */
    static bool is_encrypted(const std::vector<uint8_t>& data);
};

} // namespace midnight::crypto
