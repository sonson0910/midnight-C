#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace midnight::blockchain
{

    class Transaction;
    class MidnightBlockchain;

    /**
     * @brief Wallet for managing Midnight addresses and signing transactions
     */
    class Wallet
    {
    public:
        /**
         * @brief Wallet creation type
         */
        enum class WalletType
        {
            HARDWARE,    // Hardware wallet (Ledger, Trezor)
            MNEMONIC,    // BIP39 mnemonic seed
            EXTENDED_KEY // Extended private key
        };

        /**
         * @brief Create new wallet from mnemonic
         */
        void create_from_mnemonic(const std::string &mnemonic, const std::string &passphrase = "");

        /**
         * @brief Create new wallet from extended private key
         */
        void create_from_xprv(const std::string &xprv, const std::string &passphrase = "");

        /**
         * @brief Load wallet from file (encrypted JSON)
         */
        void load(const std::string &wallet_path, const std::string &passphrase);

        /**
         * @brief Save wallet to file (encrypted JSON)
         */
        void save(const std::string &wallet_path, const std::string &passphrase);

        /**
         * @brief Get primary address (payment address)
         */
        std::string get_address() const;

        /**
         * @brief Get all payment addresses
         */
        const std::vector<std::string> &get_addresses() const { return addresses_; }

        /**
         * @brief Generate new address at derivation path
         * @param account Account index
         * @param change Change index (0=external, 1=internal)
         * @param address_index Address index
         * @return New address string
         */
        std::string generate_address(uint32_t account = 0, uint32_t change = 0, uint32_t address_index = 0);

        /**
         * @brief Sign transaction with private key
         * @param tx_hex Transaction in CBOR hex format
         * @return Signed transaction CBOR hex
         */
        std::string sign_transaction(const std::string &tx_hex);

        /**
         * @brief Get balance for address
         * @param address Address to query
         * @param manager Optional MidnightBlockchain to fetch from network
         * @return Balance in basic units
         */
        uint64_t get_balance(const std::string &address, MidnightBlockchain *manager = nullptr) const;

        /**
         * @brief Get total balance across all addresses
         * @param manager Optional MidnightBlockchain to fetch from network
         * @return Total balance in basic units
         */
        uint64_t get_total_balance(MidnightBlockchain *manager = nullptr) const;

        /**
         * @brief Get wallet type
         */
        WalletType get_type() const { return wallet_type_; }

        /**
         * @brief Derive child key from derivation path
         * @param derivation_path Midnight wallet path (m/account/role/index)
         * @return Midnight unshielded address (Bech32m)
         */
        std::string derive_key(const std::string &derivation_path);

    private:
        std::string private_key_;
        std::string extended_private_key_;
        std::string public_key_;
        std::vector<std::string> addresses_;
        std::map<std::string, uint64_t> balances_;
        WalletType wallet_type_ = WalletType::MNEMONIC;

        // Helper methods for key derivation
        std::string pbkdf2_derive(const std::string &password, const std::string &salt);
        std::string hmac_sha512(const std::string &key, const std::string &message);
    };

} // namespace midnight::blockchain
