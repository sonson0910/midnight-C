#include "midnight/network/indexer_client.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char* kPreprodIndexer = "https://indexer.preprod.midnight.network/api/v4/graphql";
constexpr const char* kAddress =
    "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q";
constexpr const char* kTxHashWithPrefix =
    "0xe8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e";
constexpr const char* kTxHash =
    "e8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e";
constexpr const char* kBlockHash =
    "939906f65687c6250fce1eb21a59737f23b382e4a8335004d2b9dd9de33ecc2c";
constexpr uint32_t kBlockHeight = 748878;

bool live_indexer_tests_enabled()
{
    const char* enabled = std::getenv("MIDNIGHT_RUN_LIVE_INDEXER_TESTS");
    return enabled != nullptr && std::string(enabled) == "1";
}
}

TEST(IndexerClientLiveTest, PreprodKnownTransactionAndUtxoFormats)
{
    if (!live_indexer_tests_enabled())
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_LIVE_INDEXER_TESTS=1 to run preprod indexer checks";
    }

    midnight::network::IndexerClient indexer(kPreprodIndexer, 30000);
    indexer.set_cache_enabled(false);

    auto tx = indexer.query_transaction(kTxHashWithPrefix);
    ASSERT_FALSE(tx.empty());
    EXPECT_EQ(tx.value("id", 0), 151162);
    EXPECT_EQ(tx.value("hash", ""), kTxHash);
    ASSERT_TRUE(tx.contains("block"));
    EXPECT_EQ(tx["block"].value("height", 0), static_cast<int>(kBlockHeight));

    auto block = indexer.query_block(kBlockHeight);
    EXPECT_EQ(block.height, kBlockHeight);
    EXPECT_EQ(block.hash, kBlockHash);
    ASSERT_FALSE(block.transactions.empty());
    const auto contains_target_tx = std::any_of(
        block.transactions.begin(),
        block.transactions.end(),
        [](const auto &block_tx) {
            return block_tx.value("hash", "") == kTxHash ||
                   block_tx.value("hash", "") == kTxHashWithPrefix;
        });
    EXPECT_TRUE(contains_target_tx);

    auto block_by_hash = indexer.query_block(0, std::string("0x") + kBlockHash);
    EXPECT_EQ(block_by_hash.height, kBlockHeight);
    EXPECT_EQ(block_by_hash.hash, kBlockHash);

    auto utxos = indexer.query_utxos(kAddress, kBlockHeight, kBlockHeight);
    ASSERT_EQ(utxos.size(), 1u);
    EXPECT_EQ(utxos[0].tx_hash, kTxHash);
    EXPECT_EQ(utxos[0].output_index, 0u);
    EXPECT_EQ(utxos[0].value, 1000000000u);
    EXPECT_EQ(utxos[0].token_type, "NIGHT");
    EXPECT_EQ(utxos[0].block_height, kBlockHeight);
}
