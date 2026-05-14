#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <cstring>

#if defined(MIDNIGHT_ENABLE_LIBSODIUM) && MIDNIGHT_ENABLE_LIBSODIUM
#include <sodium.h>
#define MIDNIGHT_HAS_LIBSODIUM 1
#else
#define MIDNIGHT_HAS_LIBSODIUM 0
#endif

namespace midnight::blockchain
{

    class Transaction;
    class MidnightBlockchain;

    /**
     * @brief RAII wrapper that securely wipes memory on destruction.
     *        Uses libsodium's sodium_memzero to zero out sensitive data
     *        (seeds, private keys) before deallocation, preventing
     *        recovery from memory dumps or swap files.
     */
    class SecureBytes
    {
    public:
        SecureBytes() = default;
        explicit SecureBytes(const std::vector<uint8_t> &data) : data_(data) {}
        explicit SecureBytes(std::vector<uint8_t> &&data) : data_(std::move(data)) {}

        ~SecureBytes()
        {
            secure_clear();
        }

        SecureBytes(const SecureBytes &) = delete;
        SecureBytes &operator=(const SecureBytes &) = delete;

        SecureBytes(SecureBytes &&other) noexcept
            : data_(std::move(other.data_))
        {
            other.data_.clear();
        }
        SecureBytes &operator=(SecureBytes &&other) noexcept
        {
            if (this != &other)
            {
                secure_clear();
                data_ = std::move(other.data_);
                other.data_.clear();
            }
            return *this;
        }

        void assign(const std::vector<uint8_t> &data)
        {
            secure_clear();
            data_ = data;
        }

        void assign(std::vector<uint8_t> &&data)
        {
            secure_clear();
            data_ = std::move(data);
        }

        void secure_clear()
        {
            if (!data_.empty())
            {
                // Always zero the memory - this is critical for security
                // memset is always available and sufficient for zeroing
                std::memset(data_.data(), 0, data_.size());
#if defined(MIDNIGHT_ENABLE_LIBSODIUM) && MIDNIGHT_ENABLE_LIBSODIUM
                sodium_memzero(data_.data(), data_.size());
#endif
                data_.clear();
            }
        }

        bool empty() const { return data_.empty(); }
        size_t size() const { return data_.size(); }
        const std::vector<uint8_t> &bytes() const { return data_; }

        std::string as_hex() const
        {
            static constexpr char kHex[] = "0123456789abcdef";
            std::string out;
            out.reserve(data_.size() * 2);
            for (uint8_t b : data_)
            {
                out.push_back(kHex[(b >> 4) & 0x0F]);
                out.push_back(kHex[b & 0x0F]);
            }
            return out;
        }

    private:
        std::vector<uint8_t> data_;
    };

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
         * @brief Legacy local signer retained for ABI compatibility.
         *
         * Production Midnight transactions must be built, proved, serialized,
         * and signed by the native ledger backend. This method intentionally
         * hard-fails instead of producing non-canonical signatures.
         *
         * @param tx_hex Hex-encoded ledger signing payload
         * @return Hex-encoded signature
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
        SecureBytes extended_private_key_;
        std::string public_key_;
        std::vector<std::string> addresses_;
        std::map<std::string, uint64_t> balances_;
        WalletType wallet_type_ = WalletType::MNEMONIC;

        // Helper methods for key derivation
        std::string pbkdf2_derive(const std::string &password, const std::string &salt);
        std::string hmac_sha512(const std::string &key, const std::string &message);
    };

} // namespace midnight::blockchain
