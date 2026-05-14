#include "midnight/production/midnight_client.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
    std::string env_value(const char *name)
    {
        const char *value = std::getenv(name);
        return value ? std::string(value) : std::string();
    }

    bool env_enabled(const char *name)
    {
        return env_value(name) == "1";
    }

    std::string first_present(std::initializer_list<const char *> names)
    {
        for (const char *name : names)
        {
            auto value = env_value(name);
            if (!value.empty())
            {
                return value;
            }
        }
        return {};
    }

    std::string configured_ledger_ffi_library()
    {
        auto value = env_value("MIDNIGHT_LEDGER_FFI_LIBRARY");
        if (!value.empty())
        {
            return value;
        }
#ifdef MIDNIGHT_TEST_LEDGER_FFI_LIBRARY
        return MIDNIGHT_TEST_LEDGER_FFI_LIBRARY;
#else
        return {};
#endif
    }

    uint64_t env_u64_or(const char *name, uint64_t fallback)
    {
        const auto value = env_value(name);
        if (value.empty())
        {
            return fallback;
        }
        return std::stoull(value);
    }
}

TEST(ProductionLiveSubmitTest, GatedPreprodNightTransferBuildSubmitAndConfirm)
{
    if (!env_enabled("MIDNIGHT_RUN_LIVE_SUBMIT_TESTS"))
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_LIVE_SUBMIT_TESTS=1 to run a real live submit";
    }

    const auto ledger_library = configured_ledger_ffi_library();
    if (ledger_library.empty())
    {
        GTEST_SKIP() << "Set MIDNIGHT_LEDGER_FFI_LIBRARY to run gated live submit test";
    }
    if (!std::filesystem::exists(ledger_library))
    {
        GTEST_SKIP() << "Ledger FFI library does not exist: " << ledger_library;
    }

    const auto network = first_present({"MIDNIGHT_NETWORK"});
    const auto node_url = first_present({"MIDNIGHT_LIVE_NODE_URL", "MIDNIGHT_PREVIEW_NODE_URL", "MIDNIGHT_PREPROD_NODE_URL"});
    const auto source_url = first_present({"MIDNIGHT_LIVE_SOURCE_URL", "MIDNIGHT_PREVIEW_SOURCE_URL", "MIDNIGHT_PREPROD_SOURCE_URL"});
    const auto source_seed = first_present({"MIDNIGHT_LIVE_SOURCE_SEED", "MIDNIGHT_PREVIEW_SOURCE_SEED", "MIDNIGHT_PREPROD_SOURCE_SEED"});
    const auto destination =
        first_present({"MIDNIGHT_LIVE_DESTINATION_ADDRESS", "MIDNIGHT_PREVIEW_DESTINATION_ADDRESS", "MIDNIGHT_PREPROD_DESTINATION_ADDRESS"});
    const auto amount = first_present({"MIDNIGHT_LIVE_AMOUNT", "MIDNIGHT_PREVIEW_AMOUNT", "MIDNIGHT_PREPROD_AMOUNT"});

    if (node_url.empty())
    {
        GTEST_SKIP() << "Set MIDNIGHT_LIVE_NODE_URL to run gated live submit test";
    }
    if (source_url.empty())
    {
        GTEST_SKIP() << "Set MIDNIGHT_LIVE_SOURCE_URL to run gated live submit test";
    }
    if (source_seed.empty())
    {
        GTEST_SKIP() << "Set MIDNIGHT_LIVE_SOURCE_SEED to run gated live submit test";
    }
    if (destination.empty())
    {
        GTEST_SKIP() << "Set MIDNIGHT_LIVE_DESTINATION_ADDRESS to run gated live submit test";
    }
    if (amount.empty())
    {
        GTEST_SKIP() << "Set MIDNIGHT_LIVE_AMOUNT to run gated live submit test";
    }

    midnight::production::ClientConfig config;
    config.network_name = network.empty() ? "preview" : network;
    config.node_url = node_url;
    config.indexer_url = first_present({"MIDNIGHT_LIVE_INDEXER_URL", "MIDNIGHT_PREVIEW_INDEXER_URL", "MIDNIGHT_PREPROD_INDEXER_URL"});
    if (config.indexer_url.empty())
    {
        config.indexer_url = "https://indexer." + config.network_name + ".midnight.network/api/v4/graphql";
    }
    config.proof_server_url = first_present({"MIDNIGHT_LIVE_PROOF_SERVER_URL", "MIDNIGHT_PROOF_SERVER_URL"});
    config.ledger_ffi_library = ledger_library;
    config.node_timeout_ms = 30000;
    config.proof_timeout_ms = 300000;

    midnight::ledger::TransferNightParams params;
    params.source.src_url = source_url;
    params.source.fetch_cache = first_present({"MIDNIGHT_LIVE_FETCH_CACHE", "MIDNIGHT_PREVIEW_FETCH_CACHE", "MIDNIGHT_PREPROD_FETCH_CACHE"});
    if (params.source.fetch_cache.empty())
    {
        params.source.fetch_cache = "redb:midnight_cache/live_submit_fetch_cache.db";
    }
    params.source.ledger_state_db =
        first_present({"MIDNIGHT_LIVE_LEDGER_STATE_DB", "MIDNIGHT_PREVIEW_LEDGER_STATE_DB", "MIDNIGHT_PREPROD_LEDGER_STATE_DB"});
    if (params.source.ledger_state_db.empty())
    {
        params.source.ledger_state_db = "midnight_cache/live_submit_ledger_state_db";
    }
    params.source_seed = source_seed;
    params.destination_addresses = {destination};
    params.amount = amount;

    midnight::production::PipelineOptions options;
    options.artifacts.root_dir =
        first_present({"MIDNIGHT_LIVE_ARTIFACT_DIR", "MIDNIGHT_PREVIEW_ARTIFACT_DIR", "MIDNIGHT_PREPROD_ARTIFACT_DIR"});
    if (options.artifacts.root_dir.empty())
    {
        options.artifacts.root_dir = "midnight-artifacts";
    }
    options.artifacts.network = network.empty() ? "preview" : network;
    options.artifacts.wallet_id = first_present({"MIDNIGHT_LIVE_WALLET_ID", "MIDNIGHT_PREVIEW_WALLET_ID", "MIDNIGHT_PREPROD_WALLET_ID"});
    if (options.artifacts.wallet_id.empty())
    {
        options.artifacts.wallet_id = "live-submit";
    }
    options.confirmation.timeout =
        std::chrono::milliseconds(env_u64_or("MIDNIGHT_LIVE_CONFIRMATION_TIMEOUT_MS", 300000));
    options.confirmation.poll_interval = std::chrono::milliseconds(5000);

    midnight::production::MidnightClient client(config);
    const auto result = client.transfer_night(options, params);

    ASSERT_TRUE(result.transaction.build.success)
        << result.transaction.build.error;
    ASSERT_FALSE(result.transaction.build.transaction_hex.empty());
    ASSERT_FALSE(result.transaction.build.transaction_hash.empty());
    ASSERT_TRUE(result.transaction.submit.success)
        << result.transaction.submit.error;
    ASSERT_TRUE(result.success)
        << result.error.message << ": " << result.error.detail;
    EXPECT_TRUE(result.artifacts.success)
        << result.artifacts.error.message << ": " << result.artifacts.error.detail;
    EXPECT_TRUE(result.confirmation.success)
        << result.confirmation.error.message << ": " << result.confirmation.error.detail;
    EXPECT_FALSE(result.confirmation.block_hash.empty());
    EXPECT_GT(result.confirmation.block_height, 0u);
    EXPECT_GT(result.confirmation.transaction_id, 0u);
}
