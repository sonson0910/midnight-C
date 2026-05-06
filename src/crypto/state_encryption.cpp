#include "midnight/crypto/state_encryption.hpp"

#include <sodium.h>
#include <sstream>
#include <stdexcept>

namespace midnight::crypto {

// Argon2id parameters - balanced for security vs speed
static constexpr unsigned long long ARGON2_OPSLIMIT = 3;        // 3 ops limit
static constexpr size_t ARGON2_MEMLIMIT = 67108864;            // 64 MiB memory
static constexpr size_t SALT_SIZE = 16;                       // 16 bytes salt

// ─── StateEncryption ───────────────────────────────────────────

std::vector<uint8_t> StateEncryption::derive_key_from_password(
    const std::string& password,
    std::vector<uint8_t>* salt)
{
    std::vector<uint8_t> salt_out(SALT_SIZE);
    
    // Generate random salt
    randombytes_buf(salt_out.data(), salt_out.size());
    
    if (salt != nullptr) {
        *salt = salt_out;
    }
    
    return derive_key_from_password(password, salt_out);
}

std::vector<uint8_t> StateEncryption::derive_key_from_password(
    const std::string& password,
    const std::vector<uint8_t>& salt)
{
    std::vector<uint8_t> key(KEY_SIZE);
    
    int result = crypto_pwhash(
        key.data(),
        KEY_SIZE,
        password.c_str(),
        password.size(),
        salt.data(),
        ARGON2_OPSLIMIT,
        ARGON2_MEMLIMIT,
        crypto_pwhash_ALG_ARGON2ID13);
    
    if (result != 0) {
        throw std::runtime_error("Failed to derive key from password");
    }
    
    return key;
}

std::vector<uint8_t> StateEncryption::encrypt(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key)
{
    if (key.size() != KEY_SIZE) {
        throw std::invalid_argument("Invalid key size for encryption");
    }
    
    // Generate random nonce
    std::vector<uint8_t> nonce(NONCE_SIZE);
    randombytes_buf(nonce.data(), nonce.size());
    
    // Ciphertext includes Poly1305 tag (16 bytes)
    std::vector<uint8_t> ciphertext(plaintext.size() + TAG_SIZE);
    
    int result = crypto_secretbox_easy(
        ciphertext.data(),
        plaintext.data(),
        plaintext.size(),
        nonce.data(),
        key.data());
    
    if (result != 0) {
        throw std::runtime_error("Encryption failed");
    }
    
    // Prepend nonce to ciphertext
    std::vector<uint8_t> output;
    output.reserve(NONCE_SIZE + ciphertext.size());
    output.insert(output.end(), nonce.begin(), nonce.end());
    output.insert(output.end(), ciphertext.begin(), ciphertext.end());
    
    return output;
}

std::optional<std::vector<uint8_t>> StateEncryption::decrypt(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key)
{
    if (key.size() != KEY_SIZE) {
        return std::nullopt;
    }
    
    if (ciphertext.size() < NONCE_SIZE + TAG_SIZE) {
        return std::nullopt;
    }
    
    // Extract nonce
    std::vector<uint8_t> nonce(ciphertext.begin(), ciphertext.begin() + NONCE_SIZE);
    
    // Extract actual ciphertext with tag
    std::vector<uint8_t> encrypted(
        ciphertext.begin() + NONCE_SIZE,
        ciphertext.end());
    
    std::vector<uint8_t> plaintext(encrypted.size() - TAG_SIZE);
    
    int result = crypto_secretbox_open_easy(
        plaintext.data(),
        encrypted.data(),
        encrypted.size(),
        nonce.data(),
        key.data());
    
    if (result != 0) {
        // Decryption failed - wrong key or tampered data
        return std::nullopt;
    }
    
    return plaintext;
}

std::vector<uint8_t> StateEncryption::encrypt_string(
    const std::string& plaintext,
    const std::vector<uint8_t>& key)
{
    std::vector<uint8_t> bytes(plaintext.begin(), plaintext.end());
    return encrypt(bytes, key);
}

std::optional<std::string> StateEncryption::decrypt_to_string(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key)
{
    auto plaintext = decrypt(ciphertext, key);
    if (!plaintext) {
        return std::nullopt;
    }
    
    return std::string(plaintext->begin(), plaintext->end());
}

// ─── EncryptedState ─────────────────────────────────────────────

std::vector<uint8_t> EncryptedState::create(
    const std::string& plaintext,
    const std::string& password)
{
    // Derive key with random salt
    std::vector<uint8_t> salt;
    std::vector<uint8_t> key = StateEncryption::derive_key_from_password(
        password, &salt);
    
    // Encrypt the plaintext
    std::vector<uint8_t> encrypted = StateEncryption::encrypt_string(plaintext, key);
    
    // Securely wipe the key from memory
    sodium_memzero(key.data(), key.size());
    
    // Build output: [version][salt][nonce+ciphertext+tag]
    std::vector<uint8_t> output;
    output.reserve(1 + SALT_SIZE + encrypted.size());
    
    output.push_back(CURRENT_VERSION);
    output.insert(output.end(), salt.begin(), salt.end());
    output.insert(output.end(), encrypted.begin(), encrypted.end());
    
    return output;
}

std::optional<std::string> EncryptedState::decrypt(
    const std::vector<uint8_t>& encrypted,
    const std::string& password)
{
    if (!is_encrypted(encrypted)) {
        return std::nullopt;
    }
    
    // Extract version (already verified by is_encrypted)
    size_t offset = 1;
    
    // Extract salt
    std::vector<uint8_t> salt(encrypted.begin() + offset, 
                               encrypted.begin() + offset + SALT_SIZE);
    offset += SALT_SIZE;
    
    // Extract nonce+ciphertext+tag
    std::vector<uint8_t> ciphertext(encrypted.begin() + offset, encrypted.end());
    
    // Derive key with extracted salt
    std::vector<uint8_t> key = StateEncryption::derive_key_from_password(
        password, salt);
    
    // Decrypt
    auto plaintext = StateEncryption::decrypt_to_string(ciphertext, key);
    
    // Securely wipe the key from memory
    sodium_memzero(key.data(), key.size());
    
    return plaintext;
}

bool EncryptedState::verify_password(
    const std::vector<uint8_t>& encrypted,
    const std::string& password)
{
    // Quick check - just try decryption, if it fails, password is wrong
    // The Poly1305 authentication tag will fail if password is wrong
    return decrypt(encrypted, password).has_value();
}

bool EncryptedState::is_encrypted(const std::vector<uint8_t>& data)
{
    // Minimum size: version (1) + salt (16) + nonce (24) + tag (16) = 57 bytes
    if (data.size() < 1 + SALT_SIZE + StateEncryption::NONCE_SIZE + StateEncryption::TAG_SIZE) {
        return false;
    }
    
    // Check version
    return data[0] == CURRENT_VERSION;
}

} // namespace midnight::crypto
