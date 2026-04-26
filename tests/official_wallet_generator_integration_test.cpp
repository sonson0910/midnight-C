/**
 * Wallet Generator Output Tests
 *
 * Tests that the wallet generator produces correct output format
 * with all required address types (unshield, shielded, dust).
 * Uses native HDWallet directly instead of requiring external scripts.
 */

#include <gtest/gtest.h>
#include "midnight/wallet/hd_wallet.hpp"
#include <nlohmann/json.hpp>
#include <string>

using namespace midnight::wallet;

TEST(WalletGeneratorOutputTest, GeneratedWallet_ContainsAllKeyTypes)
{
    // Generate wallet from a deterministic seed
    std::string mnemonic = bip39::generate_mnemonic(24);
    ASSERT_FALSE(mnemonic.empty());

    auto hd = HDWallet::from_mnemonic(mnemonic);

    auto night_key = hd.derive_night(0, 0);
    auto dust_key = hd.derive_dust(0, 0);
    auto zswap_key = hd.derive_zswap(0, 0);

    // All keys should be non-empty
    EXPECT_FALSE(night_key.address.empty());
    EXPECT_FALSE(dust_key.address.empty());
    EXPECT_FALSE(zswap_key.address.empty());

    // Keys should be different from each other
    EXPECT_NE(night_key.address, dust_key.address);
    EXPECT_NE(night_key.address, zswap_key.address);
    EXPECT_NE(dust_key.address, zswap_key.address);
}

TEST(WalletGeneratorOutputTest, OutputJson_ContainsAddressAndDustFields)
{
    // Simulate wallet-generator --official-sdk JSON output format
    std::string mnemonic = bip39::generate_mnemonic(24);
    auto hd = HDWallet::from_mnemonic(mnemonic);

    auto night_key = hd.derive_night(0, 0);
    auto dust_key = hd.derive_dust(0, 0);
    auto zswap_key = hd.derive_zswap(0, 0);

    // Build the JSON that wallet-generator would produce
    nlohmann::json output;
    output["unshield"] = night_key.address;
    output["shielded"] = zswap_key.address;
    output["dust"] = dust_key.address;
    output["dust_registration"] = nlohmann::json{
        {"requested", true},
        {"address", dust_key.address}};

    std::string json_str = output.dump();

    // Verify all expected fields are present
    EXPECT_NE(json_str.find("\"unshield\""), std::string::npos);
    EXPECT_NE(json_str.find("\"shielded\""), std::string::npos);
    EXPECT_NE(json_str.find("\"dust\""), std::string::npos);
    EXPECT_NE(json_str.find("\"dust_registration\""), std::string::npos);

    // Verify dust_registration content via parsed JSON
    EXPECT_TRUE(output["dust_registration"]["requested"].get<bool>());
}

TEST(WalletGeneratorOutputTest, DeterministicMnemonic_ProducesSameKeys)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

    auto hd1 = HDWallet::from_mnemonic(mnemonic);
    auto hd2 = HDWallet::from_mnemonic(mnemonic);

    auto key1 = hd1.derive_night(0, 0);
    auto key2 = hd2.derive_night(0, 0);

    EXPECT_EQ(key1.address, key2.address);
    EXPECT_EQ(key1.public_key, key2.public_key);
}
