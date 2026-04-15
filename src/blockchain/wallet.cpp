#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/core/logger.hpp"
#include <array>
#include <vector>
#include <sstream>
#include <iomanip>
#include <functional>

namespace midnight::blockchain
{
    namespace
    {
        constexpr uint32_t kBech32mConst = 0x2BC830A3;
        constexpr const char *kBech32Charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

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

        std::vector<uint8_t> convert_bits(const uint8_t *data, size_t data_len, int from_bits, int to_bits)
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

            if (bits != 0)
            {
                ret.push_back((acc << (to_bits - bits)) & maxv);
            }

            return ret;
        }

        std::string bech32m_encode(const std::string &hrp, const std::array<uint8_t, 32> &payload)
        {
            std::vector<uint8_t> data;
            data.push_back(0); // witness version 0
            auto converted = convert_bits(payload.data(), payload.size(), 8, 5);
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

        std::array<uint8_t, 32> derive_mock_key_material(const std::string &seed_material)
        {
            std::array<uint8_t, 32> out{};
            uint64_t state = 1469598103934665603ULL; // FNV-1a offset
            for (unsigned char c : seed_material)
            {
                state ^= c;
                state *= 1099511628211ULL;
            }

            for (size_t i = 0; i < out.size(); ++i)
            {
                const unsigned char extra = seed_material.empty()
                                                ? static_cast<unsigned char>(i)
                                                : static_cast<unsigned char>(seed_material[(i * 7) % seed_material.size()]);
                state ^= (static_cast<uint64_t>(extra) + i);
                state *= 1099511628211ULL;
                out[i] = static_cast<uint8_t>((state >> ((i % 8) * 8)) & 0xFF);
            }

            return out;
        }
    } // namespace

    void Wallet::create_from_mnemonic(const std::string &mnemonic, const std::string &passphrase)
    {
        wallet_type_ = WalletType::MNEMONIC;

        std::ostringstream msg;
        msg << "Creating wallet from mnemonic (" << mnemonic.length() / 15 << " words)";
        midnight::g_logger->info(msg.str());

#ifdef ENABLE_CARDANO_REAL
        // In production, would use proper libraries:
        // cardano_pbkdf2_t* pbkdf2 = cardano_pbkdf2_new();
        // uint8_t root_key[32];
        // cardano_pbkdf2_derive_from_mnemonic(
        //     pbkdf2, mnemonic.c_str(), passphrase.c_str(),
        //     root_key, sizeof(root_key));
        // extended_private_key_ = to_hex(root_key);
#else
        // Mock implementation - derive from mnemonic string
        extended_private_key_ = pbkdf2_derive(mnemonic, passphrase);
#endif

        // Generate primary address
        generate_address(0, 0, 0);
        midnight::g_logger->info("Wallet created successfully");
    }

    void Wallet::create_from_xprv(const std::string &xprv, const std::string &passphrase)
    {
        wallet_type_ = WalletType::EXTENDED_KEY;
        extended_private_key_ = xprv;

        std::ostringstream msg;
        msg << "Creating wallet from extended private key: " << xprv.substr(0, 20) << "...";
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
            << address_index << "): " << address.substr(0, 30) << "...";
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
#else
        // Mock signing
        std::string signature = "sig_" + tx_hex.substr(0, 32);
        return signature;
#endif

        return "";
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
            // Query from network if adapter provided
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
        // Midnight unshielded address format:
        // mn_addr_<network>1<data+checksum> (Bech32m)
        // TODO(ENABLE_CARDANO_REAL): replace with real key derivation + sr25519 pubkey.
        const std::string derivation_seed = extended_private_key_ + "|" + derivation_path;
        const auto key_material = derive_mock_key_material(derivation_seed);
        return bech32m_encode("mn_addr_preprod", key_material);
    }

    std::string Wallet::pbkdf2_derive(const std::string &password, const std::string &salt)
    {
        // Simplified PBKDF2 mock for demo
        std::ostringstream key;
        key << "xprv_" << std::hex << std::hash<std::string>{}(password + salt);
        return key.str();
    }

    std::string Wallet::hmac_sha512(const std::string &key, const std::string &message)
    {
        // Simplified HMAC-SHA512 mock for demo
        std::ostringstream result;
        result << std::hex << std::hash<std::string>{}(key + message);
        return result.str();
    }

} // namespace midnight::blockchain
