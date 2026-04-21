#include "midnight/blockchain/wallet_manager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(MIDNIGHT_ENABLE_OPENSSL_CRYPTO) && MIDNIGHT_ENABLE_OPENSSL_CRYPTO
#include <openssl/evp.h>
#include <openssl/rand.h>
#define MIDNIGHT_HAS_OPENSSL_CRYPTO 1
#else
#define MIDNIGHT_HAS_OPENSSL_CRYPTO 0
#endif

namespace midnight::blockchain
{
    namespace
    {
        constexpr std::size_t kMinAliasLength = 3;
        constexpr std::size_t kMaxAliasLength = 64;
        constexpr std::size_t kSeedByteLength = 32;
        constexpr std::size_t kCipherSaltLength = 16;
        constexpr std::size_t kCipherNonceLength = 12;
        constexpr std::size_t kCipherTagLength = 16;
        constexpr int kCipherPbkdf2Iterations = 200000;
        constexpr const char *kWalletStorePassphraseEnv = "MIDNIGHT_WALLET_STORE_PASSPHRASE";

        std::string hex_encode(const uint8_t *data, std::size_t size)
        {
            static constexpr char kHex[] = "0123456789abcdef";
            std::string out;
            out.resize(size * 2);

            for (std::size_t i = 0; i < size; ++i)
            {
                const uint8_t value = data[i];
                out[2 * i] = kHex[(value >> 4) & 0x0F];
                out[2 * i + 1] = kHex[value & 0x0F];
            }

            return out;
        }

        bool hex_decode(const std::string &hex, std::vector<uint8_t> &bytes)
        {
            if (hex.size() % 2 != 0)
            {
                return false;
            }

            bytes.clear();
            bytes.reserve(hex.size() / 2);

            const auto nibble = [](char ch) -> int
            {
                if (ch >= '0' && ch <= '9')
                {
                    return ch - '0';
                }
                if (ch >= 'a' && ch <= 'f')
                {
                    return ch - 'a' + 10;
                }
                if (ch >= 'A' && ch <= 'F')
                {
                    return ch - 'A' + 10;
                }
                return -1;
            };

            for (std::size_t i = 0; i < hex.size(); i += 2)
            {
                const int hi = nibble(hex[i]);
                const int lo = nibble(hex[i + 1]);
                if (hi < 0 || lo < 0)
                {
                    bytes.clear();
                    return false;
                }

                bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
            }

            return true;
        }

        std::string read_store_passphrase()
        {
            const char *value = std::getenv(kWalletStorePassphraseEnv);
            return (value != nullptr && value[0] != '\0') ? std::string(value) : std::string();
        }

#if MIDNIGHT_HAS_OPENSSL_CRYPTO
        bool derive_encryption_key(
            const std::string &passphrase,
            const std::vector<uint8_t> &salt,
            std::vector<uint8_t> &key,
            std::string *error)
        {
            key.assign(32, 0U);

            if (PKCS5_PBKDF2_HMAC(
                    passphrase.c_str(),
                    static_cast<int>(passphrase.size()),
                    salt.data(),
                    static_cast<int>(salt.size()),
                    kCipherPbkdf2Iterations,
                    EVP_sha256(),
                    static_cast<int>(key.size()),
                    key.data()) != 1)
            {
                if (error != nullptr)
                {
                    *error = "Failed to derive wallet encryption key";
                }
                key.clear();
                return false;
            }

            return true;
        }

        bool encrypt_seed_hex(
            const std::string &normalized_seed_hex,
            const std::string &passphrase,
            nlohmann::json &cipher_payload,
            std::string *error)
        {
            std::vector<uint8_t> seed_bytes;
            if (!hex_decode(normalized_seed_hex, seed_bytes) || seed_bytes.size() != kSeedByteLength)
            {
                if (error != nullptr)
                {
                    *error = "Failed to normalize seed before encryption";
                }
                return false;
            }

            std::vector<uint8_t> salt(kCipherSaltLength, 0U);
            std::vector<uint8_t> nonce(kCipherNonceLength, 0U);
            if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
                RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1)
            {
                if (error != nullptr)
                {
                    *error = "Failed to generate random values for wallet encryption";
                }
                return false;
            }

            std::vector<uint8_t> key;
            if (!derive_encryption_key(passphrase, salt, key, error))
            {
                return false;
            }

            std::vector<uint8_t> ciphertext(seed_bytes.size() + kCipherTagLength, 0U);
            std::vector<uint8_t> tag(kCipherTagLength, 0U);

            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            if (ctx == nullptr)
            {
                if (error != nullptr)
                {
                    *error = "Failed to allocate cipher context";
                }
                return false;
            }

            int out_len = 0;
            int total_len = 0;
            bool ok = true;

            ok = ok && (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1);
            ok = ok && (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1);
            ok = ok && (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1);
            ok = ok && (EVP_EncryptUpdate(
                            ctx,
                            ciphertext.data(),
                            &out_len,
                            seed_bytes.data(),
                            static_cast<int>(seed_bytes.size())) == 1);

            if (ok)
            {
                total_len = out_len;
                ok = ok && (EVP_EncryptFinal_ex(ctx, ciphertext.data() + total_len, &out_len) == 1);
                total_len += out_len;
                ok = ok && (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) == 1);
            }

            EVP_CIPHER_CTX_free(ctx);

            if (!ok)
            {
                if (error != nullptr)
                {
                    *error = "Failed to encrypt wallet seed";
                }
                return false;
            }

            ciphertext.resize(static_cast<std::size_t>(total_len));

            cipher_payload = {
                {"algorithm", "aes-256-gcm"},
                {"kdf", "pbkdf2-hmac-sha256"},
                {"iterations", kCipherPbkdf2Iterations},
                {"saltHex", hex_encode(salt.data(), salt.size())},
                {"nonceHex", hex_encode(nonce.data(), nonce.size())},
                {"tagHex", hex_encode(tag.data(), tag.size())},
                {"ciphertextHex", hex_encode(ciphertext.data(), ciphertext.size())},
            };

            return true;
        }

        bool decrypt_seed_hex(
            const nlohmann::json &cipher_payload,
            const std::string &passphrase,
            std::string &seed_hex,
            std::string *error)
        {
            if (!cipher_payload.is_object())
            {
                if (error != nullptr)
                {
                    *error = "Encrypted wallet payload is malformed";
                }
                return false;
            }

            const std::string salt_hex = cipher_payload.value("saltHex", "");
            const std::string nonce_hex = cipher_payload.value("nonceHex", "");
            const std::string tag_hex = cipher_payload.value("tagHex", "");
            const std::string ciphertext_hex = cipher_payload.value("ciphertextHex", "");
            const int iterations = cipher_payload.value("iterations", kCipherPbkdf2Iterations);

            if (iterations != kCipherPbkdf2Iterations)
            {
                if (error != nullptr)
                {
                    *error = "Unsupported wallet encryption iteration count";
                }
                return false;
            }

            std::vector<uint8_t> salt;
            std::vector<uint8_t> nonce;
            std::vector<uint8_t> tag;
            std::vector<uint8_t> ciphertext;

            if (!hex_decode(salt_hex, salt) || salt.size() != kCipherSaltLength ||
                !hex_decode(nonce_hex, nonce) || nonce.size() != kCipherNonceLength ||
                !hex_decode(tag_hex, tag) || tag.size() != kCipherTagLength ||
                !hex_decode(ciphertext_hex, ciphertext) || ciphertext.empty())
            {
                if (error != nullptr)
                {
                    *error = "Encrypted wallet payload has invalid encoding";
                }
                return false;
            }

            std::vector<uint8_t> key;
            if (!derive_encryption_key(passphrase, salt, key, error))
            {
                return false;
            }

            std::vector<uint8_t> plaintext(ciphertext.size(), 0U);

            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            if (ctx == nullptr)
            {
                if (error != nullptr)
                {
                    *error = "Failed to allocate cipher context";
                }
                return false;
            }

            int out_len = 0;
            int total_len = 0;
            bool ok = true;

            ok = ok && (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1);
            ok = ok && (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1);
            ok = ok && (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1);
            ok = ok && (EVP_DecryptUpdate(
                            ctx,
                            plaintext.data(),
                            &out_len,
                            ciphertext.data(),
                            static_cast<int>(ciphertext.size())) == 1);

            if (ok)
            {
                total_len = out_len;
                ok = ok && (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), tag.data()) == 1);
                ok = ok && (EVP_DecryptFinal_ex(ctx, plaintext.data() + total_len, &out_len) == 1);
                total_len += out_len;
            }

            EVP_CIPHER_CTX_free(ctx);

            if (!ok)
            {
                if (error != nullptr)
                {
                    *error = "Failed to decrypt wallet seed. Check " + std::string(kWalletStorePassphraseEnv) + ".";
                }
                return false;
            }

            plaintext.resize(static_cast<std::size_t>(total_len));
            if (plaintext.size() != kSeedByteLength)
            {
                if (error != nullptr)
                {
                    *error = "Decrypted wallet seed has unexpected size";
                }
                return false;
            }

            seed_hex = hex_encode(plaintext.data(), plaintext.size());
            return true;
        }
#endif

        void set_error(std::string *error, const std::string &message)
        {
            if (error != nullptr)
            {
                *error = message;
            }
        }

        bool is_valid_alias(const std::string &alias)
        {
            if (alias.size() < kMinAliasLength || alias.size() > kMaxAliasLength)
            {
                return false;
            }

            const auto is_allowed = [](unsigned char ch)
            {
                return std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-';
            };

            if (!std::isalnum(static_cast<unsigned char>(alias.front())) ||
                !std::isalnum(static_cast<unsigned char>(alias.back())))
            {
                return false;
            }

            return std::all_of(alias.begin(), alias.end(),
                               [&](char ch)
                               { return is_allowed(static_cast<unsigned char>(ch)); });
        }

        bool normalize_seed_hex(const std::string &seed_hex, std::string &normalized)
        {
            std::string raw = seed_hex;
            if (raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'))
            {
                raw = raw.substr(2);
            }

            if (raw.size() != 64)
            {
                return false;
            }

            for (char &ch : raw)
            {
                if (!std::isxdigit(static_cast<unsigned char>(ch)))
                {
                    return false;
                }
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }

            normalized = std::move(raw);
            return true;
        }

        std::string now_utc_iso8601()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

            std::tm tm_utc{};
#ifdef _WIN32
            gmtime_s(&tm_utc, &now_time);
#else
            gmtime_r(&now_time, &tm_utc);
#endif

            std::ostringstream out;
            out << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
            return out.str();
        }

        std::filesystem::path resolve_store_dir_path(const std::string &store_dir)
        {
            if (!store_dir.empty())
            {
                return std::filesystem::path(store_dir);
            }

            const char *env_store_dir = std::getenv("MIDNIGHT_WALLET_STORE_DIR");
            if (env_store_dir != nullptr && env_store_dir[0] != '\0')
            {
                return std::filesystem::path(env_store_dir);
            }

            const char *home = std::getenv("HOME");
            if (home != nullptr && home[0] != '\0')
            {
                return std::filesystem::path(home) / ".local" / "share" / "midnight" / "wallets";
            }

            return std::filesystem::path(".midnight-wallets");
        }

        std::filesystem::path resolve_store_dir_canonical(const std::string &store_dir)
        {
            const std::filesystem::path base = resolve_store_dir_path(store_dir);
            if (base.is_absolute())
            {
                return base;
            }

            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            if (ec)
            {
                return base;
            }

            return cwd / base;
        }

        bool ensure_store_dir(const std::filesystem::path &store_dir, std::string *error)
        {
            std::error_code ec;
            std::filesystem::create_directories(store_dir, ec);
            if (ec)
            {
                set_error(error, "Failed to create wallet store directory: " + ec.message());
                return false;
            }

#ifndef _WIN32
            std::filesystem::permissions(
                store_dir,
                std::filesystem::perms::owner_all,
                std::filesystem::perm_options::replace,
                ec);
            (void)ec;
#endif

            return true;
        }

        std::filesystem::path alias_file_path(const std::filesystem::path &store_dir, const std::string &alias)
        {
            return store_dir / (alias + ".json");
        }

        bool read_wallet_json(const std::filesystem::path &wallet_path, nlohmann::json &json_out, std::string *error)
        {
            std::ifstream input(wallet_path, std::ios::in | std::ios::binary);
            if (!input)
            {
                set_error(error, "Wallet alias not found: " + wallet_path.string());
                return false;
            }

            try
            {
                input >> json_out;
            }
            catch (const nlohmann::json::exception &ex)
            {
                set_error(error, "Wallet file is not valid JSON: " + std::string(ex.what()));
                return false;
            }

            return true;
        }

        bool write_wallet_json(const std::filesystem::path &wallet_path, const nlohmann::json &payload, std::string *error)
        {
            const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            const auto temp_path = wallet_path.parent_path() / (wallet_path.filename().string() + ".tmp." + std::to_string(now));

            {
                std::ofstream out(temp_path, std::ios::out | std::ios::trunc | std::ios::binary);
                if (!out)
                {
                    set_error(error, "Failed to write temporary wallet file: " + temp_path.string());
                    return false;
                }

                out << payload.dump(2);
                if (!out.good())
                {
                    set_error(error, "Failed to persist wallet payload to temporary file");
                    return false;
                }
            }

            std::error_code ec;
            if (std::filesystem::exists(wallet_path, ec))
            {
                ec.clear();
                std::filesystem::remove(wallet_path, ec);
                if (ec)
                {
                    std::filesystem::remove(temp_path, ec);
                    set_error(error, "Failed to replace existing wallet file: " + ec.message());
                    return false;
                }
            }

            std::filesystem::rename(temp_path, wallet_path, ec);
            if (ec)
            {
                std::filesystem::remove(temp_path, ec);
                set_error(error, "Failed to move wallet file into place: " + ec.message());
                return false;
            }

#ifndef _WIN32
            std::filesystem::permissions(
                wallet_path,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace,
                ec);
            (void)ec;
#endif

            return true;
        }
    } // namespace

    std::string WalletManager::default_store_dir()
    {
        return resolve_store_dir_canonical("").string();
    }

    bool WalletManager::save_seed_hex(
        const std::string &alias,
        const std::string &seed_hex,
        const std::string &network,
        const std::string &store_dir,
        std::string *error)
    {
        if (!is_valid_alias(alias))
        {
            set_error(error, "Invalid wallet alias. Use 3-64 chars [A-Za-z0-9._-], starting and ending with alphanumeric.");
            return false;
        }

        std::string normalized_seed;
        if (!normalize_seed_hex(seed_hex, normalized_seed))
        {
            set_error(error, "Seed must be exactly 32 bytes (64 hex chars, optional 0x prefix)");
            return false;
        }

#if !MIDNIGHT_HAS_OPENSSL_CRYPTO
        set_error(error, "OpenSSL crypto support is required for encrypted wallet storage");
        return false;
#else
        const std::string passphrase = read_store_passphrase();
        if (passphrase.size() < 16)
        {
            set_error(error, "Set MIDNIGHT_WALLET_STORE_PASSPHRASE with at least 16 characters before saving wallet seeds");
            return false;
        }
#endif

        const std::filesystem::path store_path = resolve_store_dir_canonical(store_dir);
        if (!ensure_store_dir(store_path, error))
        {
            return false;
        }

        const std::filesystem::path wallet_path = alias_file_path(store_path, alias);

        std::string created_at = now_utc_iso8601();
        nlohmann::json existing;
        if (read_wallet_json(wallet_path, existing, nullptr))
        {
            const std::string existing_created_at = existing.value("createdAt", "");
            if (!existing_created_at.empty())
            {
                created_at = existing_created_at;
            }
        }

#if MIDNIGHT_HAS_OPENSSL_CRYPTO
        nlohmann::json seed_cipher_payload;
        if (!encrypt_seed_hex(normalized_seed, passphrase, seed_cipher_payload, error))
        {
            return false;
        }
#endif

        const nlohmann::json payload = {
#if MIDNIGHT_HAS_OPENSSL_CRYPTO
            {"version", 2},
            {"alias", alias},
            {"network", network},
            {"seedCipher", seed_cipher_payload},
            {"createdAt", created_at},
            {"updatedAt", now_utc_iso8601()}
#else
            {"version", 1},
            {"alias", alias},
            {"network", network},
            {"seedHex", normalized_seed},
            {"createdAt", created_at},
            {"updatedAt", now_utc_iso8601()}
#endif
        };

        if (!write_wallet_json(wallet_path, payload, error))
        {
            return false;
        }

        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    std::optional<ManagedWalletEntry> WalletManager::load_wallet(
        const std::string &alias,
        const std::string &store_dir,
        std::string *error)
    {
        if (!is_valid_alias(alias))
        {
            set_error(error, "Invalid wallet alias. Use 3-64 chars [A-Za-z0-9._-], starting and ending with alphanumeric.");
            return std::nullopt;
        }

        const std::filesystem::path store_path = resolve_store_dir_canonical(store_dir);
        const std::filesystem::path wallet_path = alias_file_path(store_path, alias);

        nlohmann::json payload;
        if (!read_wallet_json(wallet_path, payload, error))
        {
            return std::nullopt;
        }

        ManagedWalletEntry entry;
        entry.alias = payload.value("alias", alias);
        entry.network = payload.value("network", "");
        entry.created_at = payload.value("createdAt", "");
        entry.updated_at = payload.value("updatedAt", "");

        if (payload.contains("seedCipher"))
        {
#if !MIDNIGHT_HAS_OPENSSL_CRYPTO
            set_error(error, "OpenSSL crypto support is required to read encrypted wallet seeds");
            return std::nullopt;
#else
            const std::string passphrase = read_store_passphrase();
            if (passphrase.size() < 16)
            {
                set_error(error, "Set MIDNIGHT_WALLET_STORE_PASSPHRASE with at least 16 characters to unlock wallet seeds");
                return std::nullopt;
            }

            if (!decrypt_seed_hex(payload.at("seedCipher"), passphrase, entry.seed_hex, error))
            {
                return std::nullopt;
            }
#endif
        }
        else
        {
            entry.seed_hex = payload.value("seedHex", "");
        }

        std::string normalized_seed;
        if (!normalize_seed_hex(entry.seed_hex, normalized_seed))
        {
            set_error(error, "Stored wallet seed is invalid for alias: " + alias);
            return std::nullopt;
        }

        entry.seed_hex = normalized_seed;

        if (entry.alias.empty())
        {
            entry.alias = alias;
        }

        if (error != nullptr)
        {
            error->clear();
        }

        return entry;
    }

    std::optional<std::string> WalletManager::load_seed_hex(
        const std::string &alias,
        const std::string &store_dir,
        std::string *error)
    {
        const auto entry = load_wallet(alias, store_dir, error);
        if (!entry.has_value())
        {
            return std::nullopt;
        }

        if (error != nullptr)
        {
            error->clear();
        }
        return entry->seed_hex;
    }

    std::vector<std::string> WalletManager::list_aliases(
        const std::string &store_dir,
        std::string *error)
    {
        const std::filesystem::path store_path = resolve_store_dir_canonical(store_dir);
        std::vector<std::string> aliases;

        std::error_code ec;
        if (!std::filesystem::exists(store_path, ec))
        {
            if (error != nullptr)
            {
                error->clear();
            }
            return aliases;
        }

        for (const auto &entry : std::filesystem::directory_iterator(store_path, ec))
        {
            if (ec)
            {
                set_error(error, "Failed to scan wallet store directory: " + ec.message());
                return {};
            }

            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto path = entry.path();
            if (path.extension() != ".json")
            {
                continue;
            }

            const std::string alias = path.stem().string();
            if (is_valid_alias(alias))
            {
                aliases.push_back(alias);
            }
        }

        std::sort(aliases.begin(), aliases.end());
        if (error != nullptr)
        {
            error->clear();
        }

        return aliases;
    }

    bool WalletManager::remove_wallet(
        const std::string &alias,
        const std::string &store_dir,
        std::string *error)
    {
        if (!is_valid_alias(alias))
        {
            set_error(error, "Invalid wallet alias. Use 3-64 chars [A-Za-z0-9._-], starting and ending with alphanumeric.");
            return false;
        }

        const std::filesystem::path store_path = resolve_store_dir_canonical(store_dir);
        const std::filesystem::path wallet_path = alias_file_path(store_path, alias);

        std::error_code ec;
        const bool removed = std::filesystem::remove(wallet_path, ec);
        if (ec)
        {
            set_error(error, "Failed to remove wallet alias: " + ec.message());
            return false;
        }

        if (!removed)
        {
            set_error(error, "Wallet alias not found: " + alias);
            return false;
        }

        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }
} // namespace midnight::blockchain
