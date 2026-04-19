#include <gtest/gtest.h>
#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"

using namespace midnight::blockchain;
namespace
{
    constexpr size_t kBech32ChecksumLength = 6;
    constexpr const char *kTestMnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    constexpr const char *kExpectedAddr = "mn_addr_preprod1u9vv9jcrkl5v75nz5jd9hfvcyq328jm50309qf065yqwfkgk0cgsdfyzhd";
    constexpr const char *kExpectedShield = "mn_shield-addr_preprod17hltlkdthaw00gfhm7phdfgtnv2hd40wn5hh7awhsadwk7t78zfsdetafd";
    constexpr const char *kExpectedDust = "mn_dust_preprod15xldjvn6ymmskvtzg0xly8jk2wxhw23swmvyg457k8a6fgcmll6q4cm2j7";
}

namespace midnight::blockchain
{
    std::vector<UTXO> MidnightBlockchain::query_utxos(const std::string &)
    {
        return {};
    }
}

TEST(WalletGenerationTest, CreateFromMnemonic_GeneratesMidnightBech32mAddress)
{
    Wallet wallet;
    wallet.create_from_mnemonic(kTestMnemonic);

    const std::string address = wallet.get_address();
    EXPECT_FALSE(address.empty());
    EXPECT_EQ(address.rfind("mn_addr_preprod1", 0), 0u);
    EXPECT_GT(address.size(), std::string("mn_addr_preprod1").size() + kBech32ChecksumLength);
}

TEST(WalletGenerationTest, GenerateAddress_SamePath_IsDeterministic)
{
    Wallet wallet;
    wallet.create_from_mnemonic(kTestMnemonic);

    const std::string first = wallet.get_address();
    const std::string second = wallet.generate_address(0, 0, 0);
    EXPECT_EQ(first, second);
}

TEST(WalletGenerationTest, GenerateAddress_DifferentPath_ChangesAddress)
{
    Wallet wallet;
    wallet.create_from_mnemonic(kTestMnemonic);

    const std::string first = wallet.generate_address(0, 0, 0);
    const std::string second = wallet.generate_address(0, 1, 0);
    EXPECT_NE(first, second);
}

TEST(WalletGenerationTest, FixedVectors_MidnightRoleAddresses_MatchExpected)
{
    Wallet wallet;
    wallet.create_from_mnemonic(kTestMnemonic);

    const std::string addr = wallet.derive_key("m/0/0/0");
    const std::string shield = wallet.derive_key("m/0/3/0");
    const std::string dust = wallet.derive_key("m/0/2/0");

    EXPECT_EQ(addr, kExpectedAddr);
    EXPECT_EQ(shield, kExpectedShield);
    EXPECT_EQ(dust, kExpectedDust);
}

TEST(WalletGenerationTest, FixedVectors_Prefixes_AreCorrect)
{
    Wallet wallet;
    wallet.create_from_mnemonic(kTestMnemonic);

    const std::string addr = wallet.derive_key("m/0/0/0");
    const std::string shield = wallet.derive_key("m/0/3/0");
    const std::string dust = wallet.derive_key("m/0/2/0");

    EXPECT_EQ(addr.rfind("mn_addr_preprod1", 0), 0u);
    EXPECT_EQ(shield.rfind("mn_shield-addr_preprod1", 0), 0u);
    EXPECT_EQ(dust.rfind("mn_dust_preprod1", 0), 0u);
}
