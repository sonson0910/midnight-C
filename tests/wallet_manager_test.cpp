#include <gtest/gtest.h>

#include "midnight/blockchain/wallet_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace
{
    std::filesystem::path make_temp_wallet_store_dir()
    {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto dir = std::filesystem::temp_directory_path() / ("midnight-wallet-manager-test-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

    constexpr const char *kSeedHex = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

    std::string get_env_value(const std::string &name)
    {
        const char *value = std::getenv(name.c_str());
        return value != nullptr ? std::string(value) : std::string();
    }

    void set_env_value(const std::string &name, const std::string &value)
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    void unset_env_value(const std::string &name)
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

    class ScopedEnvVar
    {
    public:
        ScopedEnvVar(std::string name, std::string value)
            : name_(std::move(name)), had_previous_(false)
        {
            const std::string existing = get_env_value(name_);
            if (!existing.empty())
            {
                had_previous_ = true;
                previous_value_ = existing;
            }

            set_env_value(name_, value);
        }

        ~ScopedEnvVar()
        {
            if (had_previous_)
            {
                set_env_value(name_, previous_value_);
            }
            else
            {
                unset_env_value(name_);
            }
        }

    private:
        std::string name_;
        std::string previous_value_;
        bool had_previous_;
    };
} // namespace

TEST(WalletManagerTest, SaveAndLoadSeedByAlias)
{
    const auto wallet_store = make_temp_wallet_store_dir();
    ScopedEnvVar passphrase("MIDNIGHT_WALLET_STORE_PASSPHRASE", "wallet-manager-passphrase-1234");

    std::string error;
    ASSERT_TRUE(midnight::blockchain::WalletManager::save_seed_hex(
        "alice-main",
        kSeedHex,
        "preprod",
        wallet_store.string(),
        &error))
        << error;

    const auto entry = midnight::blockchain::WalletManager::load_wallet(
        "alice-main",
        wallet_store.string(),
        &error);

    ASSERT_TRUE(entry.has_value()) << error;
    EXPECT_EQ(entry->alias, "alice-main");
    EXPECT_EQ(entry->network, "preprod");
    EXPECT_EQ(entry->seed_hex, std::string(kSeedHex));
    EXPECT_FALSE(entry->created_at.empty());
    EXPECT_FALSE(entry->updated_at.empty());

    const auto loaded_seed = midnight::blockchain::WalletManager::load_seed_hex(
        "alice-main",
        wallet_store.string(),
        &error);

    ASSERT_TRUE(loaded_seed.has_value()) << error;
    EXPECT_EQ(*loaded_seed, std::string(kSeedHex));

    const auto wallet_file = wallet_store / "alice-main.json";
    std::ifstream input(wallet_file, std::ios::in | std::ios::binary);
    ASSERT_TRUE(input.good());

    nlohmann::json payload;
    input >> payload;
    EXPECT_TRUE(payload.contains("seedCipher"));
    EXPECT_FALSE(payload.contains("seedHex"));
    EXPECT_EQ(payload.value("version", 0), 2);

    std::error_code ec;
    std::filesystem::remove_all(wallet_store, ec);
}

TEST(WalletManagerTest, SaveRequiresPassphrase)
{
    const auto wallet_store = make_temp_wallet_store_dir();
    const std::string previous_passphrase = get_env_value("MIDNIGHT_WALLET_STORE_PASSPHRASE");
    unset_env_value("MIDNIGHT_WALLET_STORE_PASSPHRASE");

    std::string error;
    const bool ok = midnight::blockchain::WalletManager::save_seed_hex(
        "alice-main",
        kSeedHex,
        "preprod",
        wallet_store.string(),
        &error);

    EXPECT_FALSE(ok);
    EXPECT_NE(error.find("MIDNIGHT_WALLET_STORE_PASSPHRASE"), std::string::npos);

    if (!previous_passphrase.empty())
    {
        set_env_value("MIDNIGHT_WALLET_STORE_PASSPHRASE", previous_passphrase);
    }

    std::error_code ec;
    std::filesystem::remove_all(wallet_store, ec);
}

TEST(WalletManagerTest, RejectsInvalidAlias)
{
    const auto wallet_store = make_temp_wallet_store_dir();
    ScopedEnvVar passphrase("MIDNIGHT_WALLET_STORE_PASSPHRASE", "wallet-manager-passphrase-1234");

    std::string error;
    EXPECT_FALSE(midnight::blockchain::WalletManager::save_seed_hex(
        "../bad",
        kSeedHex,
        "preprod",
        wallet_store.string(),
        &error));
    EXPECT_NE(error.find("Invalid wallet alias"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(wallet_store, ec);
}

TEST(WalletManagerTest, ListAndRemoveAliases)
{
    const auto wallet_store = make_temp_wallet_store_dir();
    ScopedEnvVar passphrase("MIDNIGHT_WALLET_STORE_PASSPHRASE", "wallet-manager-passphrase-1234");

    std::string error;
    ASSERT_TRUE(midnight::blockchain::WalletManager::save_seed_hex(
        "alpha01",
        kSeedHex,
        "preprod",
        wallet_store.string(),
        &error))
        << error;

    ASSERT_TRUE(midnight::blockchain::WalletManager::save_seed_hex(
        "beta02",
        kSeedHex,
        "preview",
        wallet_store.string(),
        &error))
        << error;

    const auto aliases = midnight::blockchain::WalletManager::list_aliases(wallet_store.string(), &error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(aliases.size(), 2U);
    EXPECT_TRUE(std::find(aliases.begin(), aliases.end(), "alpha01") != aliases.end());
    EXPECT_TRUE(std::find(aliases.begin(), aliases.end(), "beta02") != aliases.end());

    ASSERT_TRUE(midnight::blockchain::WalletManager::remove_wallet("alpha01", wallet_store.string(), &error))
        << error;

    const auto removed_seed = midnight::blockchain::WalletManager::load_seed_hex("alpha01", wallet_store.string(), &error);
    EXPECT_FALSE(removed_seed.has_value());

    std::error_code ec;
    std::filesystem::remove_all(wallet_store, ec);
}

TEST(WalletManagerTest, LoadSupportsLegacyPlaintextWalletFormat)
{
    const auto wallet_store = make_temp_wallet_store_dir();

    const auto wallet_file = wallet_store / "legacy01.json";
    std::ofstream output(wallet_file, std::ios::out | std::ios::trunc | std::ios::binary);
    output << R"({
  "version": 1,
  "alias": "legacy01",
  "network": "preprod",
  "seedHex": "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
  "createdAt": "2026-01-01T00:00:00Z",
  "updatedAt": "2026-01-01T00:00:00Z"
})";
    output.close();

    std::string error;
    const auto entry = midnight::blockchain::WalletManager::load_wallet("legacy01", wallet_store.string(), &error);

    ASSERT_TRUE(entry.has_value()) << error;
    EXPECT_EQ(entry->seed_hex, std::string(kSeedHex));
    EXPECT_EQ(entry->network, "preprod");

    std::error_code ec;
    std::filesystem::remove_all(wallet_store, ec);
}
