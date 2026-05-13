#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/crypto/ed25519_signer.hpp"
#include "midnight/crypto/state_encryption.hpp"
#include <array>
#include <vector>
#include <vector>
#include <sstream>
#include <iomanip>
#include <functional>
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <fstream>

#if defined(MIDNIGHT_ENABLE_OPENSSL_CRYPTO) && MIDNIGHT_ENABLE_OPENSSL_CRYPTO
#include <openssl/evp.h>
#include <openssl/hmac.h>
#define MIDNIGHT_HAS_OPENSSL_CRYPTO 1
#else
#define MIDNIGHT_HAS_OPENSSL_CRYPTO 0
#endif

namespace midnight::blockchain
{
    namespace
    {
        // Import shared Bech32m utilities from common_utils.hpp
        using midnight::util::bech32m::kBech32mConst;
        using midnight::util::bech32m::kBech32Charset;

        struct Bip32Node
        {
            std::array<uint8_t, 32> key{};
            std::array<uint8_t, 32> chain{};
        };

        std::string bech32m_encode(const std::string &hrp, const std::array<uint8_t, 32> &payload)
        {
            return midnight::util::bech32m::encode(hrp, payload);
        }

        std::string normalize_words(const std::string &input)
        {
            std::istringstream iss(input);
            std::string word;
            std::string normalized;

            while (iss >> word)
            {
                std::transform(word.begin(), word.end(), word.begin(),
                               [](unsigned char c) -> char
                               { return static_cast<char>(std::tolower(c)); });
                if (!normalized.empty())
                {
                    normalized.push_back(' ');
                }
                normalized += word;
            }
            return normalized;
        }

        uint32_t parse_u32(const std::string &component)
        {
            if (component.empty())
            {
                throw std::invalid_argument("Empty derivation path component");
            }

            size_t parsed = 0;
            const unsigned long value = std::stoul(component, &parsed, 10);
            if (parsed != component.size() || value > std::numeric_limits<uint32_t>::max())
            {
                throw std::invalid_argument("Invalid derivation path component: " + component);
            }
            return static_cast<uint32_t>(value);
        }

        void parse_derivation_path(const std::string &derivation_path, uint32_t &account, uint32_t &role, uint32_t &address_index)
        {
            std::vector<std::string> parts;
            std::stringstream ss(derivation_path);
            std::string segment;
            while (std::getline(ss, segment, '/'))
            {
                parts.push_back(segment);
            }

            if (parts.size() != 4 || parts[0] != "m")
            {
                throw std::invalid_argument("Derivation path must follow m/<account>/<role>/<index>");
            }

            account = parse_u32(parts[1]);
            role = parse_u32(parts[2]);
            address_index = parse_u32(parts[3]);
        }

        std::string hex_encode(const uint8_t *data, size_t len)
        {
            std::ostringstream out;
            out << std::hex << std::setfill('0');
            for (size_t i = 0; i < len; ++i)
            {
                out << std::setw(2) << static_cast<int>(data[i]);
            }
            return out.str();
        }

        std::vector<uint8_t> hex_decode(std::string hex)
        {
            if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0)
            {
                hex = hex.substr(2);
            }

            if (hex.size() % 2 != 0)
            {
                throw std::invalid_argument("Hex input must have even length");
            }

            auto nibble = [](char c) -> int
            {
                if (c >= '0' && c <= '9')
                {
                    return c - '0';
                }
                const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower >= 'a' && lower <= 'f')
                {
                    return 10 + (lower - 'a');
                }
                return -1;
            };

            std::vector<uint8_t> out;
            out.reserve(hex.size() / 2);
            for (size_t i = 0; i < hex.size(); i += 2)
            {
                const int hi = nibble(hex[i]);
                const int lo = nibble(hex[i + 1]);
                if (hi < 0 || lo < 0)
                {
                    throw std::invalid_argument("Invalid hex input");
                }
                out.push_back(static_cast<uint8_t>((hi << 4) | lo));
            }
            return out;
        }

#if MIDNIGHT_HAS_OPENSSL_CRYPTO
        std::vector<uint8_t> hmac_sha512_bytes(const std::vector<uint8_t> &key, const std::vector<uint8_t> &message)
        {
            unsigned int out_len = 0;
            std::array<uint8_t, EVP_MAX_MD_SIZE> digest{};
            unsigned char *result = HMAC(EVP_sha512(),
                                         key.data(), static_cast<int>(key.size()),
                                         message.data(), message.size(),
                                         digest.data(), &out_len);
            if (result == nullptr || out_len != 64)
            {
                throw std::runtime_error("HMAC-SHA512 failed");
            }
            return std::vector<uint8_t>(digest.begin(), digest.begin() + out_len);
        }

        std::vector<uint8_t> pbkdf2_hmac_sha512_bytes(const std::string &password, const std::string &salt, int iterations, size_t out_len)
        {
            std::vector<uint8_t> out(out_len);
            const int ok = PKCS5_PBKDF2_HMAC(
                password.c_str(), static_cast<int>(password.size()),
                reinterpret_cast<const unsigned char *>(salt.data()), static_cast<int>(salt.size()),
                iterations, EVP_sha512(), static_cast<int>(out_len), out.data());
            if (ok != 1)
            {
                throw std::runtime_error("PBKDF2-HMAC-SHA512 failed");
            }
            return out;
        }

        std::vector<uint8_t> pbkdf2_derive_bytes(const std::string &password, const std::string &salt)
        {
            const std::string pbkdf2_salt = "mnemonic" + salt;
            return pbkdf2_hmac_sha512_bytes(password, pbkdf2_salt, 2048, 64);
        }
#endif

        Bip32Node derive_master_node_from_seed(const std::vector<uint8_t> &seed)
        {
#if MIDNIGHT_HAS_OPENSSL_CRYPTO
            const std::vector<uint8_t> master_key = {'B', 'i', 't', 'c', 'o', 'i', 'n', ' ', 's', 'e', 'e', 'd'};
            const auto digest = hmac_sha512_bytes(master_key, seed);
            Bip32Node node{};
            std::copy_n(digest.begin(), 32, node.key.begin());
            std::copy_n(digest.begin() + 32, 32, node.chain.begin());
            return node;
#else
            (void)seed;
            throw std::runtime_error("OpenSSL crypto support is required for wallet key derivation");
#endif
        }

        Bip32Node derive_child_node(const Bip32Node &parent, uint32_t index)
        {
#if MIDNIGHT_HAS_OPENSSL_CRYPTO
            std::vector<uint8_t> payload(parent.key.begin(), parent.key.end());
            payload.push_back(static_cast<uint8_t>((index >> 24) & 0xFF));
            payload.push_back(static_cast<uint8_t>((index >> 16) & 0xFF));
            payload.push_back(static_cast<uint8_t>((index >> 8) & 0xFF));
            payload.push_back(static_cast<uint8_t>(index & 0xFF));

            std::vector<uint8_t> chain(parent.chain.begin(), parent.chain.end());
            const auto digest = hmac_sha512_bytes(chain, payload);
            Bip32Node child{};
            std::copy_n(digest.begin(), 32, child.key.begin());
            std::copy_n(digest.begin() + 32, 32, child.chain.begin());
            return child;
#else
            (void)parent;
            (void)index;
            throw std::runtime_error("OpenSSL crypto support is required for wallet key derivation");
#endif
        }

        std::array<uint8_t, 32> derive_role_payload(const std::vector<uint8_t> &seed, uint32_t account, uint32_t role, uint32_t address_index)
        {
            auto node = derive_master_node_from_seed(seed);
            node = derive_child_node(node, account);
            node = derive_child_node(node, role);
            node = derive_child_node(node, address_index);
            return node.key;
        }

        std::string role_to_hrp(uint32_t role, const std::string &network = "preprod")
        {
            switch (role)
            {
            case 3:
                return "mn_shield-addr_" + network;
            case 2:
                return "mn_dust_" + network;
            default:
                return "mn_addr_" + network;
            }
        }

        std::vector<uint8_t> decode_seed_hex_or_throw(const std::string &seed_hex)
        {
            auto seed = hex_decode(seed_hex);
            if (seed.size() != 64)
            {
                throw std::runtime_error("Wallet seed must be 64 bytes");
            }
            return seed;
        }
    } // namespace

    void Wallet::create_from_mnemonic(const std::string &mnemonic, const std::string &passphrase)
    {
        wallet_type_ = WalletType::MNEMONIC;

        const size_t word_count = std::count(mnemonic.begin(), mnemonic.end(), ' ') + 1;
        if (midnight::g_logger)
        {
            midnight::g_logger->info("Creating wallet from mnemonic");
        }

        const std::string normalized_mnemonic = normalize_words(mnemonic);
        if (normalized_mnemonic.empty())
        {
            throw std::invalid_argument("Mnemonic cannot be empty");
        }

        // BIP39 seed: PBKDF2-HMAC-SHA512(mnemonic, "mnemonic" + passphrase, 2048, 64 bytes)
        extended_private_key_.assign(pbkdf2_derive_bytes(normalized_mnemonic, passphrase));

        addresses_.clear();
        balances_.clear();

        // Generate primary address
        generate_address(0, 0, 0);
        if (midnight::g_logger)
        {
            midnight::g_logger->info("Wallet created successfully");
        }
    }

    void Wallet::create_from_xprv(const std::string &xprv, const std::string &passphrase)
    {
        wallet_type_ = WalletType::EXTENDED_KEY;

        if (xprv.empty())
        {
            throw std::invalid_argument("Extended private key cannot be empty");
        }

        // Accept direct seed hex (64 bytes), otherwise derive deterministic seed from provided secret.
        try
        {
            auto maybe_seed = hex_decode(xprv);
            if (maybe_seed.size() == 64)
            {
                extended_private_key_.assign(std::move(maybe_seed));
            }
            else
            {
                extended_private_key_.assign(pbkdf2_derive_bytes(xprv, passphrase));
            }
        }
        catch (const std::invalid_argument &)
        {
            extended_private_key_.assign(pbkdf2_derive_bytes(xprv, passphrase));
        }

        addresses_.clear();
        balances_.clear();

        if (midnight::g_logger)
        {
            midnight::g_logger->info("Creating wallet from extended private key");
        }

        generate_address(0, 0, 0);
    }

    void Wallet::load(const std::string &wallet_path, const std::string &passphrase)
    {
        if (midnight::g_logger) {
            midnight::g_logger->info("Loading wallet from: " + wallet_path);
        }

        std::ifstream file(wallet_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open wallet file: " + wallet_path);
        }

        std::vector<uint8_t> encrypted_data(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        if (encrypted_data.empty()) {
            throw std::runtime_error("Wallet file is empty: " + wallet_path);
        }

        if (passphrase.empty()) {
            extended_private_key_.assign(encrypted_data);
            if (extended_private_key_.size() != 64) {
                throw std::runtime_error("Invalid wallet file: expected 64-byte seed");
            }
        } else {
            // Decrypt using state encryption
            auto decrypted = midnight::crypto::EncryptedState::decrypt(encrypted_data, passphrase);
            if (!decrypted.has_value()) {
                throw std::runtime_error("Failed to decrypt wallet: invalid password or corrupted file");
            }
            std::istringstream iss(*decrypted, std::ios::binary);

            uint32_t magic;
            iss.read(reinterpret_cast<char*>(&magic), 4);
            if (magic != 0x4D574C4C) { // 'MWLL'
                throw std::runtime_error("Invalid wallet file format");
            }

            uint32_t version;
            iss.read(reinterpret_cast<char*>(&version), 4);
            if (version != 1) {
                throw std::runtime_error("Unsupported wallet format version");
            }

            uint32_t seed_len;
            iss.read(reinterpret_cast<char*>(&seed_len), 4);
            if (seed_len != 64) {
                throw std::runtime_error("Invalid seed length in wallet file");
            }

            std::vector<uint8_t> seed(64);
            iss.read(reinterpret_cast<char*>(seed.data()), 64);
            extended_private_key_.assign(seed);

            uint32_t addr_count;
            iss.read(reinterpret_cast<char*>(&addr_count), 4);
            addresses_.clear();
            for (uint32_t i = 0; i < addr_count; ++i) {
                uint32_t len;
                iss.read(reinterpret_cast<char*>(&len), 4);
                std::string addr(len, '\0');
                iss.read(addr.data(), len);
                addresses_.push_back(addr);
            }
        }

        wallet_type_ = WalletType::EXTENDED_KEY;
        if (midnight::g_logger) {
            midnight::g_logger->info("Wallet loaded: " + std::to_string(addresses_.size()) + " address(es)");
        }
    }

    void Wallet::save(const std::string &wallet_path, const std::string &passphrase)
    {
        if (midnight::g_logger) {
            midnight::g_logger->info("Saving wallet to: " + wallet_path);
        }

        if (extended_private_key_.empty()) {
            throw std::runtime_error("Cannot save: wallet seed is empty");
        }

        std::ofstream file(wallet_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot create wallet file: " + wallet_path);
        }

        if (passphrase.empty()) {
            // Save raw seed (unencrypted) - NOT recommended for production
            if (midnight::g_logger) {
                midnight::g_logger->warn("Saving wallet WITHOUT encryption - this is insecure");
            }
            auto seed = extended_private_key_.bytes();
            file.write(reinterpret_cast<const char*>(seed.data()), seed.size());
        } else {
            // Encrypt state and save with format header
            std::ostringstream oss(std::ios::binary);

            uint32_t magic = 0x4D574C4C; // 'MWLL'
            uint32_t version = 1;
            uint32_t seed_len = 64;

            oss.write(reinterpret_cast<const char*>(&magic), 4);
            oss.write(reinterpret_cast<const char*>(&version), 4);
            oss.write(reinterpret_cast<const char*>(&seed_len), 4);

            auto seed = extended_private_key_.bytes();
            oss.write(reinterpret_cast<const char*>(seed.data()), 64);

            uint32_t addr_count = static_cast<uint32_t>(addresses_.size());
            oss.write(reinterpret_cast<const char*>(&addr_count), 4);
            for (const auto &addr : addresses_) {
                uint32_t len = static_cast<uint32_t>(addr.size());
                oss.write(reinterpret_cast<const char*>(&len), 4);
                oss.write(addr.data(), len);
            }

            std::string plaintext = oss.str();
            auto encrypted = midnight::crypto::EncryptedState::create(plaintext, passphrase);
            file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
        }

        if (midnight::g_logger) {
            midnight::g_logger->info("Wallet saved successfully");
        }
    }

    std::string Wallet::get_address() const
    {
        return addresses_.empty() ? "" : addresses_[0];
    }

    std::string Wallet::generate_address(uint32_t account, uint32_t change, uint32_t address_index)
    {
        std::ostringstream derivation_path;
        derivation_path << "m/" << account << "/" << change << "/" << address_index;

        std::string address = derive_key(derivation_path.str());
        addresses_.push_back(address);

        std::ostringstream msg;
        msg << "Generated address (" << account << "/" << change << "/"
            << address_index << "): " << address.substr(0, std::min<size_t>(30, address.size())) << "...";
        midnight::g_logger->debug(msg.str());

        return address;
    }

    std::string Wallet::sign_transaction(const std::string &tx_hex)
    {
        if (midnight::g_logger)
        {
            midnight::g_logger->debug("Signing transaction");
        }

        if (extended_private_key_.empty())
        {
            throw std::runtime_error("Cannot sign: wallet seed is not initialized");
        }

        // Validate tx_hex format before attempting to sign
        std::string tx_normalized = tx_hex;
        if (tx_normalized.rfind("0x", 0) == 0 || tx_normalized.rfind("0X", 0) == 0)
        {
            tx_normalized = tx_normalized.substr(2);
        }
        if (tx_normalized.empty() || (tx_normalized.size() % 2) != 0)
        {
            throw std::invalid_argument("Invalid tx_hex: must be a non-empty hex string (optionally prefixed with 0x)");
        }
        for (char c : tx_normalized)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            {
                throw std::invalid_argument("Invalid tx_hex: contains non-hex character");
            }
        }

#if defined(MIDNIGHT_ENABLE_SODIUM) && MIDNIGHT_ENABLE_SODIUM
        // Use Ed25519 signing via the wallet's derived private key
        try
        {
            const auto &seed = extended_private_key_.bytes();
            if (seed.size() != 64)
            {
                throw std::runtime_error("Wallet seed must be 64 bytes");
            }
            // Derive signing key from m/0/0/0 (primary spending key)
            const auto key_payload = derive_role_payload(seed, 0, 0, 0);

            // Convert 32-byte derived key to Ed25519 seed and generate keypair
            std::array<uint8_t, 32> ed_seed{};
            std::copy(key_payload.begin(), key_payload.end(), ed_seed.begin());
            auto [pub_key, priv_key] = crypto::Ed25519Signer::keypair_from_seed(ed_seed);

            // Sign the transaction hex payload
            auto signature = crypto::Ed25519Signer::sign_message(tx_hex, priv_key);
            return "0x" + crypto::Ed25519Signer::signature_to_hex(signature);
        }
        catch (const std::exception &e)
        {
            if (midnight::g_logger)
            {
                midnight::g_logger->error("Ed25519 transaction signing failed: " + std::string(e.what()));
            }
            throw;
        }
#else
        if (midnight::g_logger)
        {
            midnight::g_logger->warn("Wallet::sign_transaction: libsodium unavailable");
        }
        throw std::runtime_error("Cannot sign transaction: libsodium crypto is unavailable");
#endif
    }

    uint64_t Wallet::get_balance(const std::string &address, MidnightBlockchain *adapter) const
    {
        auto it = balances_.find(address);
        if (it != balances_.end())
        {
            return it->second;
        }

        if (adapter)
        {
            auto utxos = adapter->query_utxos(address);
            uint64_t total = 0;
            for (const auto &utxo : utxos)
            {
                total += utxo.value;
            }
            return total;
        }

        return 0;
    }

    uint64_t Wallet::get_total_balance(MidnightBlockchain *adapter) const
    {
        uint64_t total = 0;
        for (const auto &address : addresses_)
        {
            total += get_balance(address, adapter);
        }
        return total;
    }

    std::string Wallet::derive_key(const std::string &derivation_path)
    {
        if (extended_private_key_.empty())
        {
            throw std::runtime_error("Wallet seed is not initialized");
        }

        uint32_t account = 0;
        uint32_t role = 0;
        uint32_t address_index = 0;
        parse_derivation_path(derivation_path, account, role, address_index);

        const auto &seed = extended_private_key_.bytes();
        if (seed.size() != 64)
        {
            throw std::runtime_error("Wallet seed must be 64 bytes");
        }
        const auto payload = derive_role_payload(seed, account, role, address_index);
        return bech32m_encode(role_to_hrp(role), payload);
    }

    std::string Wallet::pbkdf2_derive(const std::string &password, const std::string &salt)
    {
#if MIDNIGHT_HAS_OPENSSL_CRYPTO
        const std::string pbkdf2_salt = "mnemonic" + salt;
        const auto seed = pbkdf2_hmac_sha512_bytes(password, pbkdf2_salt, 2048, 64);
        return hex_encode(seed.data(), seed.size());
#else
        (void)password;
        (void)salt;
        throw std::runtime_error("OpenSSL crypto support is required for PBKDF2 derivation");
#endif
    }

    std::string Wallet::hmac_sha512(const std::string &key, const std::string &message)
    {
#if MIDNIGHT_HAS_OPENSSL_CRYPTO
        const std::vector<uint8_t> key_bytes(key.begin(), key.end());
        const std::vector<uint8_t> msg_bytes(message.begin(), message.end());
        const auto digest = hmac_sha512_bytes(key_bytes, msg_bytes);
        return hex_encode(digest.data(), digest.size());
#else
        (void)key;
        (void)message;
        throw std::runtime_error("OpenSSL crypto support is required for HMAC-SHA512");
#endif
    }

} // namespace midnight::blockchain
