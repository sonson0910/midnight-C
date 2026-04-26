#include <gtest/gtest.h>

#include "midnight/network/network_config.hpp"

using midnight::network::NetworkConfig;

TEST(NetworkConfigTest, FromString_AcceptsMidnightPreprodAlias)
{
    EXPECT_EQ(NetworkConfig::from_string("midnight-preprod"), NetworkConfig::Network::TESTNET);
}

TEST(NetworkConfigTest, FromString_IsCaseInsensitiveForMidnightAliases)
{
    EXPECT_EQ(NetworkConfig::from_string("MIDNIGHT-MAINNET"), NetworkConfig::Network::MAINNET);
}

TEST(NetworkConfigTest, FromString_UnknownDefaultsToPreview)
{
    EXPECT_EQ(NetworkConfig::from_string("unknown-network"), NetworkConfig::Network::PREVIEW);
}
