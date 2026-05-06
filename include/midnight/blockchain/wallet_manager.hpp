#pragma once

#include <optional>
#include <string>
#include <vector>

namespace midnight::blockchain
{
    struct ManagedWalletEntry
    {
        std::string alias;
        std::string network;
        std::string seed_hex;
        std::string created_at;
        std::string updated_at;
    };

    class WalletManager
    {
    public:
        static std::string default_store_dir();

        static bool save_seed_hex(
            const std::string &alias,
            const std::string &seed_hex,
            const std::string &network = "",
            const std::string &store_dir = "",
            std::string *error = nullptr);

        static std::optional<ManagedWalletEntry> load_wallet(
            const std::string &alias,
            const std::string &store_dir = "",
            std::string *error = nullptr);

        static std::optional<std::string> load_seed_hex(
            const std::string &alias,
            const std::string &store_dir = "",
            std::string *error = nullptr);

        static std::vector<std::string> list_aliases(
            const std::string &store_dir = "",
            std::string *error = nullptr);

        static bool remove_wallet(
            const std::string &alias,
            const std::string &store_dir = "",
            std::string *error = nullptr);

        /**
         * @brief Save wallet state with encryption
         * @param alias Wallet alias
         * @param encrypted_state Base64-encoded encrypted wallet state
         * @param store_dir Directory to store the wallet
         * @param error Error message output
         * @return true if saved successfully
         */
        static bool save_encrypted(
            const std::string &alias,
            const std::string &encrypted_state,
            const std::string &store_dir = "",
            std::string *error = nullptr);

        /**
         * @brief Load encrypted wallet state
         * @param alias Wallet alias
         * @param password Decryption password
         * @param store_dir Directory to load from
         * @param error Error message output
         * @return Decrypted wallet state JSON, or nullopt
         */
        static std::optional<std::string> load_encrypted(
            const std::string &alias,
            const std::string &password,
            const std::string &store_dir = "",
            std::string *error = nullptr);

        /**
         * @brief Check if wallet file is encrypted format
         * @param alias Wallet alias
         * @param store_dir Directory containing the wallet
         * @return true if the wallet is stored in encrypted format
         */
        static bool is_encrypted_wallet(
            const std::string &alias,
            const std::string &store_dir = "");
    };
} // namespace midnight::blockchain
