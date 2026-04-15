#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/core/logger.hpp"
#include <array>
#include <vector>
#include <sstream>
#include <iomanip>
#include <functional>
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

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
        constexpr uint32_t kBech32mConst = 0x2BC830A3;
        constexpr const char *kBech32Charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

        struct Bip32Node
        {
            std::array<uint8_t, 32> key{};
            std::array<uint8_t, 32> chain{};
        };

        uint32_t bech32_polymod(const std::vector<uint8_t> &values)
        {
            static constexpr uint32_t gen[5] = {0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3};
            uint32_t chk = 1;
            for (auto value : values)
            {
                const uint32_t top = chk >> 25;
                chk = ((chk & 0x1FFFFFF) << 5) ^ value;
                for (int i = 0; i < 5; ++i)
                {
                    chk ^= ((top >> i) & 1) ? gen[i] : 0;
                }
            }
            return chk;
        }

        std::vector<uint8_t> bech32_hrp_expand(const std::string &hrp)
        {
            std::vector<uint8_t> ret;
            ret.reserve(hrp.size() * 2 + 1);
            for (unsigned char c : hrp)
            {
                ret.push_back(c >> 5);
            }
            ret.push_back(0);
            for (unsigned char c : hrp)
            {
                ret.push_back(c & 31);
            }
            return ret;
        }

        std::vector<uint8_t> convert_bits(const uint8_t *data, size_t data_len, int from_bits, int to_bits, bool pad)
        {
            int acc = 0;
            int bits = 0;
            const int maxv = (1 << to_bits) - 1;
            const int max_acc = (1 << (from_bits + to_bits - 1)) - 1;
            std::vector<uint8_t> ret;
            ret.reserve((data_len * from_bits + to_bits - 1) / to_bits);

            for (size_t i = 0; i < data_len; ++i)
            {
                const int value = data[i];
                acc = ((acc << from_bits) | value) & max_acc;
                bits += from_bits;
                while (bits >= to_bits)
                {
                    bits -= to_bits;
                    ret.push_back((acc >> bits) & maxv);
                }
            }

            if (pad)
            {
                if (bits != 0)
                {
                    ret.push_back(static_cast<uint8_t>((acc << (to_bits - bits)) & maxv));
                }
            }
            else if (bits >= from_bits || ((acc << (to_bits - bits)) & maxv) != 0)
            {
                return {};
            }

            return ret;
        }

        std::string bech32m_encode(const std::string &hrp, const std::array<uint8_t, 32> &payload)
        {
            std::vector<uint8_t> data;
            data.push_back(0);
            auto converted = convert_bits(payload.data(), payload.size(), 8, 5, true);
            data.insert(data.end(), converted.begin(), converted.end());

            auto values = bech32_hrp_expand(hrp);
            values.insert(values.end(), data.begin(), data.end());
            values.insert(values.end(), 6, 0);
            const uint32_t polymod = bech32_polymod(values) ^ kBech32mConst;

            std::array<uint8_t, 6> checksum{};
            for (int i = 0; i < 6; ++i)
            {
                checksum[i] = (polymod >> (5 * (5 - i))) & 31;
            }

            std::string out = hrp + "1";
            out.reserve(hrp.size() + 1 + data.size() + checksum.size());
            for (auto v : data)
            {
                out.push_back(kBech32Charset[v]);
            }
            for (auto v : checksum)
            {
                out.push_back(kBech32Charset[v]);
            }
            return out;
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

        std::string role_to_hrp(uint32_t role)
        {
            switch (role)
            {
            case 3:
                return "mn_shield-addr_preprod";
            case 2:
                return "mn_dust_preprod";
            default:
                return "mn_addr_preprod";
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

        std::ostringstream msg;
        msg << "Creating wallet from mnemonic (" << mnemonic.length() / 15 << " words)";
        midnight::g_logger->info(msg.str());

        const std::string normalized_mnemonic = normalize_words(mnemonic);
        if (normalized_mnemonic.empty())
        {
            throw std::invalid_argument("Mnemonic cannot be empty");
        }

        // BIP39 seed: PBKDF2-HMAC-SHA512(mnemonic, "mnemonic" + passphrase, 2048, 64 bytes)
        extended_private_key_ = pbkdf2_derive(normalized_mnemonic, passphrase);

        addresses_.clear();
        balances_.clear();

        // Generate primary address
        generate_address(0, 0, 0);
        midnight::g_logger->info("Wallet created successfully");
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
                extended_private_key_ = hex_encode(maybe_seed.data(), maybe_seed.size());
            }
            else
            {
                extended_private_key_ = pbkdf2_derive(xprv, passphrase);
            }
        }
        catch (const std::invalid_argument &)
        {
            extended_private_key_ = pbkdf2_derive(xprv, passphrase);
        }

        addresses_.clear();
        balances_.clear();

        std::ostringstream msg;
        msg << "Creating wallet from extended private key: " << xprv.substr(0, std::min<size_t>(20, xprv.size())) << "...";
        midnight::g_logger->info(msg.str());

        generate_address(0, 0, 0);
    }

    void Wallet::load(const std::string &wallet_path, const std::string &passphrase)
    {
        std::ostringstream msg;
        msg << "Loading wallet from: " << wallet_path;
        midnight::g_logger->info(msg.str());

        // In production, would:
        // 1. Read encrypted JSON from file
        // 2. Decrypt with passphrase
        // 3. Extract extended private key
        // 4. Regenerate addresses from derivation paths
        (void)passphrase;
    }

    void Wallet::save(const std::string &wallet_path, const std::string &passphrase)
    {
        std::ostringstream msg;
        msg << "Saving wallet to: " << wallet_path;
        midnight::g_logger->info(msg.str());

        // In production, would:
        // 1. Create JSON structure with wallet metadata
        // 2. Encrypt extended private key with passphrase
        // 3. Write to file with proper permissions
        (void)passphrase;
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
        midnight::g_logger->debug("Signing transaction");

#ifdef ENABLE_CARDANO_REAL
        // In production, would use cardano-c:
        // uint8_t tx_cbor[4096];
        // // ... hex to bytes conversion ...
        // uint8_t signature[64];
        // cardano_sign_transaction(extended_private_key_.c_str(), tx_cbor, signature);
        return "";
#else
        // Placeholder signing logic - replace with real signing path once wire format is finalized.
        return "sig_" + tx_hex.substr(0, std::min<size_t>(32, tx_hex.size()));
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
                total += utxo.amount;
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

        const auto seed = decode_seed_hex_or_throw(extended_private_key_);
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
