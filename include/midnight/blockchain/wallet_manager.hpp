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
    };
} // namespace midnight::blockchain
