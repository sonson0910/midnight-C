/**
 * Key Encryption Implementation
 * Argon2id + AES-256-GCM for secure key storage
 */

#include "midnight/crypto/key_encryption.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <stdexcept>

#if defined(MIDNIGHT_ENABLE_SODIUM) && MIDNIGHT_ENABLE_SODIUM && __has_include(<sodium.h>)
#include <sodium.h>
#if __has_include(<argon2.h>)
#include <argon2.h>
#define MIDNIGHT_HAS_ARGON2 1
#else
#define MIDNIGHT_HAS_ARGON2 0
#endif
#else
#define MIDNIGHT_HAS_ARGON2 0
#endif

namespace midnight::crypto {

// ─── Secure Memory ────────────────────────────────────────────────────────────

void SecureMemory::secure_zero(void* data, size_t size) {
    if (!data || size == 0) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
    while (size--) {
        *p++ = 0;
    }
}

bool SecureMemory::constant_time_compare(const void* a, const void* b, size_t size) {
    if (!a || !b || size == 0) return false;
    const uint8_t* pa = static_cast<const uint8_t*>(a);
    const uint8_t* pb = static_cast<const uint8_t*>(b);
    uint8_t diff = 0;
    for (size_t i = 0; i < size; ++i) {
        diff |= pa[i] ^ pb[i];
    }
    return diff == 0;
}

// ─── EncryptedKey JSON Serialization ─────────────────────────────────────────

std::string EncryptedKey::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"version\":" << version << ",";
    oss << "\"salt\":\"" << midnight::util::bytes_to_hex(salt) << "\",";
    oss << "\"nonce\":\"" << midnight::util::bytes_to_hex(nonce) << "\",";
    oss << "\"ciphertext\":\"" << midnight::util::bytes_to_hex(ciphertext) << "\",";
    oss << "\"tag\":\"" << midnight::util::bytes_to_hex(tag) << "\"";
    oss << "}";
    return oss.str();
}

std::optional<EncryptedKey> EncryptedKey::from_json(const std::string& json_str) {
    try {
        EncryptedKey result;
        std::string s = json_str;
        
        // Find and extract fields
        auto find_field = [](const std::string& str, const char* field) -> std::pair<size_t, size_t> {
            std::string pattern = std::string("\"") + field + "\":";
            auto pos = str.find(pattern);
            if (pos == std::string::npos) return {std::string::npos, std::string::npos};
            pos += strlen(field) + 3; // Skip field name and ":"
            size_t start = pos;
            
            // Find end (comma or closing brace)
            while (pos < str.size() && str[pos] != ',' && str[pos] != '}') pos++;
            size_t end = pos;
            
            return {start, end};
        };
        
        auto [v_start, v_end] = find_field(s, "version");
        if (v_start != std::string::npos) {
            result.version = static_cast<uint32_t>(std::stoul(s.substr(v_start, v_end - v_start)));
        }
        
        auto hex_from_field = [&s, &find_field](const char* field) -> std::optional<std::vector<uint8_t>> {
            auto [start, end] = find_field(s, field);
            if (start == std::string::npos) return std::nullopt;
            std::string hex_val = s.substr(start, end - start);
            // Remove quotes if present
            if (!hex_val.empty() && hex_val[0] == '"') hex_val = hex_val.substr(1);
            if (!hex_val.empty() && hex_val.back() == '"') hex_val.pop_back();
            return midnight::util::hex_to_bytes(hex_val);
        };
        
        auto salt = hex_from_field("salt");
        auto nonce = hex_from_field("nonce");
        auto ciphertext = hex_from_field("ciphertext");
        auto tag = hex_from_field("tag");
        
        if (!salt || !nonce || !ciphertext || !tag) {
            midnight::g_logger->error("Invalid encrypted key JSON: missing fields");
            return std::nullopt;
        }
        
        result.salt = *salt;
        result.nonce = *nonce;
        result.ciphertext = *ciphertext;
        result.tag = *tag;
        
        return result;
    }
    catch (const std::exception& e) {
        midnight::g_logger->error(std::string("Failed to parse encrypted key JSON: ") + e.what());
        return std::nullopt;
    }
}

namespace {
    std::string bytes_to_hex(const std::vector<uint8_t>& data) {
        static const char* hex_chars = "0123456789abcdef";
        std::string result;
        result.reserve(data.size() * 2);
        for (uint8_t byte : data) {
            result.push_back(hex_chars[(byte >> 4) & 0x0F]);
            result.push_back(hex_chars[byte & 0x0F]);
        }
        return result;
    }
    
    std::optional<std::vector<uint8_t>> hex_to_bytes(const std::string& hex) {
        std::string clean_hex = hex;
        if (clean_hex.size() >= 2 && clean_hex.substr(0, 2) == "0x") {
            clean_hex = clean_hex.substr(2);
        }
        
        if (clean_hex.size() % 2 != 0) return std::nullopt;
        
        std::vector<uint8_t> result;
        result.reserve(clean_hex.size() / 2);
        
        for (size_t i = 0; i < clean_hex.size(); i += 2) {
            auto hex_char_to_nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            
            int high = hex_char_to_nibble(clean_hex[i]);
            int low = hex_char_to_nibble(clean_hex[i + 1]);
            if (high < 0 || low < 0) return std::nullopt;
            
            result.push_back(static_cast<uint8_t>((high << 4) | low));
        }
        
        return result;
    }
}

// ─── Random Bytes ─────────────────────────────────────────────────────────────

std::vector<uint8_t> KeyEncryption::generate_random_bytes(size_t count) {
    std::vector<uint8_t> result(count);
    
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10101000L
    if (RAND_bytes(result.data(), static_cast<int>(count)) != 1) {
        throw std::runtime_error("RAND_bytes failed while generating cryptographic random bytes");
    }
#elif defined(MIDNIGHT_HAS_SODIUM) && MIDNIGHT_HAS_SODIUM
    randombytes_buf(result.data(), count);
#else
    // Fallback (less secure, but prevents compilation failure)
    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned>(time(nullptr)));
        seeded = true;
    }
    for (size_t i = 0; i < count; ++i) {
        result[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
#endif
    
    return result;
}

// ─── Argon2id Key Derivation ─────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> KeyEncryption::derive_key_argon2id(
    const std::string& password,
    const std::vector<uint8_t>& salt,
    const KeyEncryptionConfig& config) {
    
    std::vector<uint8_t> derived_key(KeyEncryptionConfig::KEY_SIZE);
    
#if defined(MIDNIGHT_HAS_ARGON2) && MIDNIGHT_HAS_ARGON2
    int result = argon2d_hash(
        config.argon2_opslimit,
        config.argon2_memlimit,
        ARGON2ID_TYPE,
        password.c_str(),
        password.size(),
        salt.data(),
        salt.size(),
        derived_key.data(),
        derived_key.size(),
        nullptr,  // encoded output (not needed)
        0,
        ARGON2_VERSION_NUMBER
    );
    
    if (result != ARGON2_OK) {
        midnight::g_logger->error(std::string("Argon2id key derivation failed: error code ") + std::to_string(result));
        return std::nullopt;
    }
#else
    (void)config;
    // Fallback: Use PBKDF2-HMAC-SHA256 if Argon2 is not available
    // This is less secure but ensures the code compiles
    midnight::g_logger->warn("Argon2 not available, using PBKDF2 fallback (reduce security)");
    
    const EVP_MD* md = EVP_sha256();
    if (md == nullptr) return std::nullopt;
    
    std::vector<uint8_t> password_bytes(password.begin(), password.end());
    
    int result = PKCS5_PBKDF2_HMAC(
        password.c_str(),
        static_cast<int>(password.size()),
        salt.data(),
        static_cast<int>(salt.size()),
        100000,  // iterations (less than Argon2 but acceptable fallback)
        md,
        static_cast<int>(derived_key.size()),
        derived_key.data()
    );
    
    if (result != 1) {
        midnight::g_logger->error("PBKDF2 fallback failed");
        return std::nullopt;
    }
#endif
    
    return derived_key;
}

// ─── AES-256-GCM Encryption ───────────────────────────────────────────────────

std::optional<EncryptedKey> KeyEncryption::encrypt_key(
    const std::vector<uint8_t>& key_bytes,
    const std::string& password,
    const KeyEncryptionConfig& config) {
    
    if (password.empty()) {
        midnight::g_logger->error("Password cannot be empty");
        return std::nullopt;
    }
    
    try {
        EncryptedKey result;
        result.version = config.CURRENT_VERSION;
        
        // Generate random salt and nonce
        result.salt = generate_random_bytes(config.SALT_SIZE);
        result.nonce = generate_random_bytes(config.NONCE_SIZE);
        
        // Derive encryption key from password
        auto derived_key = derive_key_argon2id(password, result.salt, config);
        if (!derived_key) return std::nullopt;
        
        // Prepare output buffers
        std::vector<uint8_t> ciphertext(key_bytes.size());
        result.tag.resize(config.TAG_SIZE);
        
        // Encrypt using AES-256-GCM
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            midnight::g_logger->error("Failed to create cipher context");
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        int len = 0;
        int ciphertext_len = 0;
        
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            midnight::g_logger->error("Failed to initialize AES-GCM encryption");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        // Set IV length (non-default for GCM)
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 
                                  static_cast<int>(config.NONCE_SIZE), nullptr) != 1) {
            midnight::g_logger->error("Failed to set GCM IV length");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        // Initialize with key and IV
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, 
                                derived_key->data(), result.nonce.data()) != 1) {
            midnight::g_logger->error("Failed to set key and IV");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        // Encrypt the data
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                              key_bytes.data(), static_cast<int>(key_bytes.size())) != 1) {
            midnight::g_logger->error("AES-GCM encryption failed");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        ciphertext_len = len;
        
        // Finalize encryption (generates auth tag)
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
            midnight::g_logger->error("AES-GCM finalization failed");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        ciphertext_len += len;
        
        // Get authentication tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                                  static_cast<int>(config.TAG_SIZE), result.tag.data()) != 1) {
            midnight::g_logger->error("Failed to get GCM auth tag");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Resize ciphertext to actual size
        result.ciphertext.resize(ciphertext_len);
        
        // Securely clear the derived key from memory
        SecureMemory::secure_zero(derived_key->data(), derived_key->size());
        
        return result;
    }
    catch (const std::exception& e) {
        midnight::g_logger->error(std::string("Encryption failed: ") + e.what());
        return std::nullopt;
    }
}

std::optional<std::vector<uint8_t>> KeyEncryption::decrypt_key(
    const EncryptedKey& encrypted,
    const std::string& password) {
    
    if (encrypted.version != KeyEncryptionConfig::CURRENT_VERSION) {
        midnight::g_logger->error("Unsupported encryption version");
        return std::nullopt;
    }
    
    if (encrypted.ciphertext.empty()) {
        midnight::g_logger->error("Empty ciphertext");
        return std::nullopt;
    }
    
    try {
        // Derive key from password
        KeyEncryptionConfig config;
        auto derived_key = derive_key_argon2id(password, encrypted.salt, config);
        if (!derived_key) return std::nullopt;
        
        // Prepare output buffer
        std::vector<uint8_t> plaintext(encrypted.ciphertext.size());
        
        // Decrypt using AES-256-GCM
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            midnight::g_logger->error("Failed to create cipher context");
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        int len = 0;
        int plaintext_len = 0;
        
        // Initialize decryption
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            midnight::g_logger->error("Failed to initialize AES-GCM decryption");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                                  static_cast<int>(encrypted.nonce.size()), nullptr) != 1) {
            midnight::g_logger->error("Failed to set GCM IV length");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        // Initialize with key and IV
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                                derived_key->data(), encrypted.nonce.data()) != 1) {
            midnight::g_logger->error("Failed to set key and IV");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        // Set expected authentication tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                                  static_cast<int>(encrypted.tag.size()),
                                  const_cast<uint8_t*>(encrypted.tag.data())) != 1) {
            midnight::g_logger->error("Failed to set GCM auth tag");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        
        // Decrypt the data
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                              encrypted.ciphertext.data(),
                              static_cast<int>(encrypted.ciphertext.size())) != 1) {
            midnight::g_logger->error("AES-GCM decryption failed (wrong password or corrupted data)");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        plaintext_len = len;
        
        // Finalize decryption (verifies auth tag)
        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) < 0) {
            midnight::g_logger->error("AES-GCM authentication failed (wrong password)");
            EVP_CIPHER_CTX_free(ctx);
            SecureMemory::secure_zero(derived_key->data(), derived_key->size());
            return std::nullopt;
        }
        plaintext_len += len;
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Securely clear the derived key
        SecureMemory::secure_zero(derived_key->data(), derived_key->size());
        
        // Resize to actual plaintext size
        plaintext.resize(plaintext_len);
        
        return plaintext;
    }
    catch (const std::exception& e) {
        midnight::g_logger->error(std::string("Decryption failed: ") + e.what());
        return std::nullopt;
    }
}

bool KeyEncryption::verify_password(
    const EncryptedKey& encrypted,
    const std::string& password) {
    auto decrypted = decrypt_key(encrypted, password);
    return decrypted.has_value();
}

// ─── Hex Key Convenience Methods ─────────────────────────────────────────────

std::optional<EncryptedKey> KeyEncryption::encrypt_hex_key(
    const std::string& hex_key,
    const std::string& password,
    const KeyEncryptionConfig& config) {
    
    auto key_bytes = hex_to_bytes(hex_key);
    if (!key_bytes) {
        midnight::g_logger->error("Invalid hex key format");
        return std::nullopt;
    }
    
    return encrypt_key(*key_bytes, password, config);
}

std::optional<std::string> KeyEncryption::decrypt_hex_key(
    const EncryptedKey& encrypted,
    const std::string& password) {
    
    auto decrypted = decrypt_key(encrypted, password);
    if (!decrypted) return std::nullopt;
    
    // Convert back to hex
    std::string result = "0x" + bytes_to_hex(*decrypted);
    return result;
}

} // namespace midnight::crypto
