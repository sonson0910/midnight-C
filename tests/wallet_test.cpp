#include <gtest/gtest.h>
#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"

using namespace midnight::blockchain;
namespace
{
    constexpr size_t kBech32ChecksumLength = 6;
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
    wallet.create_from_mnemonic("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about");

    const std::string address = wallet.get_address();
    EXPECT_FALSE(address.empty());
    EXPECT_EQ(address.rfind("mn_addr_preprod1", 0), 0u);
    EXPECT_GT(address.size(), std::string("mn_addr_preprod1").size() + kBech32ChecksumLength);
}

TEST(WalletGenerationTest, GenerateAddress_SamePath_IsDeterministic)
{
    Wallet wallet;
    wallet.create_from_mnemonic("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about");

    const std::string first = wallet.get_address();
    const std::string second = wallet.generate_address(0, 0, 0);
    EXPECT_EQ(first, second);
}

TEST(WalletGenerationTest, GenerateAddress_DifferentPath_ChangesAddress)
{
    Wallet wallet;
    wallet.create_from_mnemonic("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about");

    const std::string first = wallet.generate_address(0, 0, 0);
    const std::string second = wallet.generate_address(0, 1, 0);
    EXPECT_NE(first, second);
}
