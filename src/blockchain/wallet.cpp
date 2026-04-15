#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <iomanip>
#include <functional>

namespace midnight::blockchain
{

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
        derivation_path << "m/1852'/1815'/" << account << "'/"
                        << change << "/" << address_index;

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
        // CIP1852 derivation path parsing
        // Format: m/1852'/1815'/account'/change/address_index

        std::ostringstream derived;
        derived << "xpub_" << derivation_path.substr(0, 30);

#ifdef ENABLE_CARDANO_REAL
        // In production, would use:
        // cardano_extended_key_derive(extended_private_key_.c_str(),
        //     derivation_path.c_str(), derived_key)
        // Then generate address from derived_key
#endif

        // All keys, regardless of how derived, are prefixed with "addr"
        // In a real implementation, this would go through cardano-c's address generation
        // For now, create a test address
        std::ostringstream addr;
        addr << "addr_test_" << std::hex << std::hash<std::string>{}(derived.str());

        return addr.str();
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
