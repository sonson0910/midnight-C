#include "midnight/ledger/ledger_backend.hpp"
#include "midnight/production/midnight_client.hpp"
#include "midnight/production/state_backend.hpp"
#include "midnight/wallet/wallet_facade.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::string env_value(const char *name)
    {
        const char *value = std::getenv(name);
        return value ? std::string(value) : std::string();
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

    class CapturingLedgerBackend final : public midnight::ledger::ILedgerBackend
    {
    public:
        bool transfer_called = false;
        bool sync_called = false;
        midnight::ledger::TransferNightParams last_transfer;
        midnight::ledger::SyncLedgerStateParams last_sync;
        midnight::ledger::BuildResult sync_result;

        midnight::ledger::BuildResult build_transfer_night(
            const midnight::ledger::TransferNightParams &params) override
        {
            transfer_called = true;
            last_transfer = params;
            midnight::ledger::BuildResult result;
            result.error = "captured";
            return result;
        }

        midnight::ledger::BuildResult build_register_dust(
            const midnight::ledger::RegisterDustParams &) override
        {
            return {};
        }
        midnight::ledger::BuildResult build_deregister_dust(
            const midnight::ledger::DeregisterDustParams &) override
        {
            return {};
        }
        midnight::ledger::BuildResult build_simple_contract_deploy(
            const midnight::ledger::SimpleContractDeployParams &) override
        {
            return {};
        }
        midnight::ledger::BuildResult build_simple_contract_call(
            const midnight::ledger::SimpleContractCallParams &) override
        {
            return {};
        }
        midnight::ledger::BuildResult build_custom_contract_transaction(
            const midnight::ledger::CustomContractIntentParams &) override
        {
            return {};
        }
        midnight::ledger::BuildResult sync_ledger_state(
            const midnight::ledger::SyncLedgerStateParams &params) override
        {
            sync_called = true;
            last_sync = params;
            return sync_result;
        }
    };

    midnight::ledger::TransferNightParams valid_transfer_params()
    {
        midnight::ledger::TransferNightParams params;
        params.source.src_url = "wss://rpc.preview.midnight.network";
        params.source_seed =
            "0000000000000000000000000000000000000000000000000000000000000001";
        params.destination_addresses = {
            "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q"};
        params.amount = "1";
        return params;
    }

    std::filesystem::path write_dummy_ledger_state_cache(const std::string &name)
    {
        auto path = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(path);
        std::ofstream(path / "wallet-state.bin") << "cached";
        return path;
    }

    std::filesystem::path clean_temp_path(const std::string &name)
    {
        auto path = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path);
        return path;
    }

    TEST(LedgerBackendTest, UnavailableBackendFailsClearly)
    {
        auto backend = midnight::ledger::make_unavailable_ledger_backend();

        midnight::ledger::TransferNightParams params;
        params.amount = "1000000000";

        const auto result = backend->build_transfer_night(params);
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("native Midnight ledger backend"), std::string::npos);
    }

    TEST(LedgerBackendTest, BuildResultCarriesContractAndArtifactFields)
    {
        midnight::ledger::BuildResult result;
        result.success = true;
        result.transaction_hash =
            "0xe8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e";
        result.contract_address =
            "939906000000000000000000000000000000000000000000000000000000cc2c";
        result.intent_path = "intent.json";
        result.private_state_path = "private-state.json";
        result.zswap_state_path = "zswap-state.json";
        result.onchain_state_path = "onchain-state.json";
        result.result_path = "result.json";

        EXPECT_TRUE(result.success);
        EXPECT_FALSE(result.contract_address.empty());
        EXPECT_FALSE(result.intent_path.empty());
        EXPECT_FALSE(result.private_state_path.empty());
        EXPECT_FALSE(result.zswap_state_path.empty());
        EXPECT_FALSE(result.onchain_state_path.empty());
        EXPECT_FALSE(result.result_path.empty());
    }

    TEST(LedgerBackendTest, FfiBackendLoadsConfiguredNativeLibrary)
    {
        const auto library = configured_ledger_ffi_library();
        if (library.empty() || !std::filesystem::exists(library))
        {
            GTEST_SKIP() << "Set MIDNIGHT_LEDGER_FFI_LIBRARY or build midnight-ledger-ffi to run native FFI load check";
        }

        auto backend = midnight::ledger::make_ffi_ledger_backend(library);
        auto *ffi_backend = dynamic_cast<midnight::ledger::FfiLedgerBackend *>(backend.get());
        ASSERT_NE(ffi_backend, nullptr);
        EXPECT_TRUE(ffi_backend->is_available()) << ffi_backend->last_error();
    }

    TEST(LedgerBackendTest, FfiBackendReportsVersionAndCapabilities)
    {
        const auto library = configured_ledger_ffi_library();
        if (library.empty() || !std::filesystem::exists(library))
        {
            GTEST_SKIP() << "Set MIDNIGHT_LEDGER_FFI_LIBRARY or build midnight-ledger-ffi to run native FFI info check";
        }

        auto backend = midnight::ledger::make_ffi_ledger_backend(library);
        auto *ffi_backend = dynamic_cast<midnight::ledger::FfiLedgerBackend *>(backend.get());
        ASSERT_NE(ffi_backend, nullptr);
        ASSERT_TRUE(ffi_backend->can_report_backend_info()) << ffi_backend->last_error();

        const auto info = ffi_backend->backend_info();
        ASSERT_TRUE(info.success) << info.error;
        EXPECT_GE(info.abi_version, 2u);
        EXPECT_FALSE(info.ledger.empty());
        EXPECT_FALSE(info.capabilities.empty());
    }

    TEST(LedgerBackendTest, FfiBackendValidatesContractAddressAndRejectsWalletAsContract)
    {
        const auto library = configured_ledger_ffi_library();
        if (library.empty() || !std::filesystem::exists(library))
        {
            GTEST_SKIP() << "Set MIDNIGHT_LEDGER_FFI_LIBRARY or build midnight-ledger-ffi to run native validation check";
        }

        auto backend = midnight::ledger::make_ffi_ledger_backend(library);
        auto *ffi_backend = dynamic_cast<midnight::ledger::FfiLedgerBackend *>(backend.get());
        ASSERT_NE(ffi_backend, nullptr);
        ASSERT_TRUE(ffi_backend->can_validate()) << ffi_backend->last_error();

        const auto valid = ffi_backend->validate(
            "contract-address",
            "0000000000000000000000000000000000000000000000000000000000000000",
            "preview");
        EXPECT_TRUE(valid.success) << valid.error;
        EXPECT_TRUE(valid.valid) << valid.error;
        EXPECT_EQ(valid.normalized,
                  "0000000000000000000000000000000000000000000000000000000000000000");

        const auto invalid = ffi_backend->validate(
            "contract-address",
            "mn_addr_preview1h3ssm5ru2t6eqy4g3she78zlxn96e36ms6pq996aduvmateh9p9sk96u7s",
            "preview");
        EXPECT_FALSE(invalid.valid);
        EXPECT_FALSE(invalid.error.empty());
    }

    TEST(LedgerBackendTest, FfiDerivesOfficialWalletAddressVectors)
    {
        const auto library = configured_ledger_ffi_library();
        if (library.empty() || !std::filesystem::exists(library))
        {
            GTEST_SKIP() << "Set MIDNIGHT_LEDGER_FFI_LIBRARY or build midnight-ledger-ffi to run wallet vector check";
        }

        auto backend = midnight::ledger::make_ffi_ledger_backend(library);
        auto *ffi_backend = dynamic_cast<midnight::ledger::FfiLedgerBackend *>(backend.get());
        ASSERT_NE(ffi_backend, nullptr);
        ASSERT_TRUE(ffi_backend->can_derive_wallet_addresses()) << ffi_backend->last_error();

        const auto addresses = ffi_backend->derive_wallet_addresses(
            "0000000000000000000000000000000000000000000000000000000000000001",
            "testnet");

        ASSERT_TRUE(addresses.success) << addresses.error;
        EXPECT_EQ(addresses.unshielded,
                  "mn_addr_testnet1h3ssm5ru2t6eqy4g3she78zlxn96e36ms6pq996aduvmateh9p9snacw06");
        EXPECT_EQ(addresses.shielded,
                  "mn_shield-addr_testnet1r020sfa7jllsz0z2wqhykz8npmphsu5223nsea7vjt9ekxs5almtvtnrpgpszud4uyd0yjrlqyp7v5xvwqljsng2g79j5w4al9c4kuqmrxx6k");
        EXPECT_EQ(addresses.dust,
                  "mn_dust_testnet1w0l54txthpu8q05j9j9ttk3j5dyu5766dc9zkz2s435vdglzwdr35jzxfk0");
        EXPECT_EQ(addresses.dust_public,
                  "73ff4aaccbb878703e922c8ab5da32a349ca7b5a6e0a2b0950ac68c6a3e273471a");
        EXPECT_EQ(addresses.coin_public,
                  "1bd4f827be97ff013c4a702e4b08f30ec378728a54670cf7cc92cb9b1a14eff6");
        EXPECT_EQ(addresses.verifying_key,
                  "86f71b8a7a21bfe16a2bb2eab74475073fa5b773854f151ed548dba77a2c5815");
        EXPECT_EQ(addresses.user_address,
                  "bc610dd07c52f59012a88c2f9f1c5f34cbacc75b868202975d6f19beaf37284b");
    }

    TEST(ProductionClientTest, BuildOnlyApiValidatesBeforeLedgerBackend)
    {
        midnight::production::ClientConfig config;
        config.node_url = "http://127.0.0.1:9944";
        config.indexer_url = "http://127.0.0.1:8088/api/v4/graphql";

        midnight::production::MidnightClient client(
            config,
            midnight::ledger::make_unavailable_ledger_backend());

        midnight::ledger::TransferNightParams params;
        params.source_seed =
            "0000000000000000000000000000000000000000000000000000000000000001";
        params.destination_addresses = {
            "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q"};
        params.amount = "1";

        const auto result = client.build_transfer_night(params);
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("src_url or src_files"), std::string::npos);
    }

    TEST(ProductionClientTest, AutoSourceModeUsesLocalCacheForTransactionBuild)
    {
        midnight::production::ClientConfig config;
        config.node_url = "http://127.0.0.1:9944";
        config.indexer_url = "http://127.0.0.1:8088/api/v4/graphql";
        auto backend = std::make_shared<CapturingLedgerBackend>();
        midnight::production::MidnightClient client(config, backend);

        auto params = valid_transfer_params();
        params.source.ledger_state_db =
            write_dummy_ledger_state_cache("midnight-cxx-auto-cache").string();

        const auto result = client.build_transfer_night(params);
        EXPECT_EQ(result.error, "captured");
        ASSERT_TRUE(backend->transfer_called);
        EXPECT_EQ(backend->last_transfer.source.source_mode,
                  midnight::ledger::SourceMode::LocalCache);
        EXPECT_TRUE(backend->last_transfer.source.fetch_only_cached);
    }

    TEST(ProductionClientTest, ColdSyncSourceModeAllowsExplicitRpcBuild)
    {
        midnight::production::ClientConfig config;
        config.node_url = "http://127.0.0.1:9944";
        config.indexer_url = "http://127.0.0.1:8088/api/v4/graphql";
        auto backend = std::make_shared<CapturingLedgerBackend>();
        midnight::production::MidnightClient client(config, backend);

        auto params = valid_transfer_params();
        params.source.source_mode = midnight::ledger::SourceMode::ColdSync;
        params.source.ledger_state_db = "/tmp/definitely-missing-midnight-cache";

        const auto result = client.build_transfer_night(params);
        EXPECT_EQ(result.error, "captured");
        ASSERT_TRUE(backend->transfer_called);
        EXPECT_EQ(backend->last_transfer.source.source_mode,
                  midnight::ledger::SourceMode::ColdSync);
        EXPECT_FALSE(backend->last_transfer.source.fetch_only_cached);
    }

    TEST(ProductionClientTest, LocalCacheModeFailsBeforeBackendWhenCacheMissing)
    {
        midnight::production::ClientConfig config;
        config.node_url = "http://127.0.0.1:9944";
        config.indexer_url = "http://127.0.0.1:8088/api/v4/graphql";
        auto backend = std::make_shared<CapturingLedgerBackend>();
        midnight::production::MidnightClient client(config, backend);

        auto params = valid_transfer_params();
        params.source.source_mode = midnight::ledger::SourceMode::LocalCache;
        params.source.ledger_state_db = "/tmp/definitely-missing-midnight-cache";

        const auto result = client.build_transfer_night(params);
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("Local ledger state cache is not synced"),
                  std::string::npos);
        EXPECT_FALSE(backend->transfer_called);
    }

    TEST(StateBackendTest, LocalStatusDetectsReadyLedgerState)
    {
        const auto root = clean_temp_path("midnight-cxx-state-status");
        std::filesystem::create_directories(root / "ledger");
        std::ofstream(root / "fetch.db") << "fetch-cache";
        std::ofstream(root / "ledger" / "wallet-state.bin") << "cached-state";

        midnight::ledger::SourceConfig source;
        source.fetch_cache = "redb:" + (root / "fetch.db").string();
        source.ledger_state_db = (root / "ledger").string();

        auto state = midnight::production::make_local_state_backend(
            midnight::ledger::make_unavailable_ledger_backend());
        const auto status = state->status(source, 765810);

        EXPECT_TRUE(status.success);
        EXPECT_TRUE(status.ready);
        EXPECT_FALSE(status.stale);
        EXPECT_TRUE(status.fetch_cache_available);
        EXPECT_TRUE(status.ledger_state_available);
        EXPECT_GT(status.fetch_cache_bytes, 0u);
        EXPECT_GT(status.ledger_state_bytes, 0u);
        EXPECT_EQ(status.remote_tip_height, 765810u);
    }

    TEST(StateBackendTest, LocalRefreshColdSyncsAutoSource)
    {
        auto backend = std::make_shared<CapturingLedgerBackend>();
        backend->sync_result.success = true;
        auto state = midnight::production::make_local_state_backend(backend);

        midnight::ledger::SyncLedgerStateParams params;
        params.source.src_url = "wss://rpc.preview.midnight.network";
        params.source.source_mode = midnight::ledger::SourceMode::Auto;
        params.wallet_seeds = {
            "0000000000000000000000000000000000000000000000000000000000000001"};

        const auto result = state->refresh(params);

        EXPECT_TRUE(result.success);
        ASSERT_TRUE(backend->sync_called);
        EXPECT_EQ(backend->last_sync.source.source_mode,
                  midnight::ledger::SourceMode::ColdSync);
        EXPECT_FALSE(backend->last_sync.source.fetch_only_cached);
    }

    TEST(StateBackendTest, SnapshotExportImportCopiesState)
    {
        const auto root = clean_temp_path("midnight-cxx-state-snapshot");
        const auto snapshot = root / "snapshot";
        const auto imported = root / "imported";
        std::filesystem::create_directories(root / "source-ledger");
        std::ofstream(root / "source-fetch.db") << "fetch-cache";
        std::ofstream(root / "source-ledger" / "wallet-state.bin") << "cached-state";

        midnight::ledger::SourceConfig source;
        source.fetch_cache = "redb:" + (root / "source-fetch.db").string();
        source.ledger_state_db = (root / "source-ledger").string();

        auto state = midnight::production::make_local_state_backend(
            midnight::ledger::make_unavailable_ledger_backend(),
            midnight::production::StateBackendMode::Snapshot);
        const auto exported = state->export_snapshot(source, snapshot);

        ASSERT_TRUE(exported.success) << exported.error.detail;
        EXPECT_TRUE(std::filesystem::exists(snapshot / "manifest.json"));
        EXPECT_GT(exported.manifest.value("fetch_cache_bytes", 0u), 0u);
        EXPECT_GT(exported.manifest.value("ledger_state_bytes", 0u), 0u);

        midnight::ledger::SourceConfig destination;
        destination.fetch_cache = "redb:" + (imported / "fetch.db").string();
        destination.ledger_state_db = (imported / "ledger").string();
        const auto imported_result = state->import_snapshot(snapshot, destination);

        ASSERT_TRUE(imported_result.success) << imported_result.error.detail;
        EXPECT_TRUE(std::filesystem::exists(imported / "fetch.db"));
        EXPECT_TRUE(std::filesystem::exists(imported / "ledger" / "wallet-state.bin"));
    }

    TEST(StateBackendTest, ManagedBackendRequiresServiceUrl)
    {
        midnight::production::ManagedStateServiceConfig config;
        auto state = midnight::production::make_managed_state_backend(config);

        midnight::ledger::SourceConfig source;
        const auto status = state->status(source);

        EXPECT_FALSE(status.success);
        EXPECT_FALSE(status.ready);
        EXPECT_NE(status.error.find("URL is not configured"), std::string::npos);
    }

    TEST(ProductionClientTest, NetworkFactoryUsesPreviewEndpoints)
    {
        const auto config = midnight::production::ClientConfig::for_network(
            midnight::network::NetworkConfig::Network::PREVIEW);

        EXPECT_EQ(config.network_name, "preview");
        EXPECT_EQ(config.node_url, "https://rpc.preview.midnight.network");
        EXPECT_EQ(config.indexer_url, "https://indexer.preview.midnight.network/api/v4/graphql");
        EXPECT_EQ(config.proof_server_url, "http://127.0.0.1:6300");
    }

    TEST(ProductionClientTest, DevnetUsesUndeployedLedgerNetworkName)
    {
        const auto config = midnight::production::ClientConfig::for_network(
            midnight::network::NetworkConfig::Network::DEVNET);

        EXPECT_EQ(config.network_name, "undeployed");
        EXPECT_EQ(config.node_url, "http://localhost:9944");
    }

    TEST(ProductionClientTest, WalletDerivationRequiresFfiBackend)
    {
        midnight::production::ClientConfig config =
            midnight::production::ClientConfig::for_network(
                midnight::network::NetworkConfig::Network::PREVIEW);

        midnight::production::MidnightClient client(
            config,
            midnight::ledger::make_unavailable_ledger_backend());

        const auto result = client.derive_wallet_addresses(
            "0000000000000000000000000000000000000000000000000000000000000001",
            "preview");
        EXPECT_FALSE(result.success);
        EXPECT_NE(result.error.find("libmidnight_ledger_ffi"), std::string::npos);
    }

    TEST(WalletFacadeTest, ProductionConfigRequiresCanonicalDerivation)
    {
        midnight::wallet::WalletFacadeConfig config;
        config.indexer_http_url = "https://indexer.preview.midnight.network/api/v4/graphql";
        config.network = midnight::wallet::address::Network::Preview;
        config.ledger_ffi_library = "/definitely/missing/libmidnight_ledger_ffi.dylib";
        config.require_canonical_wallet_derivation = true;

        EXPECT_THROW(
            midnight::wallet::WalletFacade::from_mnemonic_with_config(
                "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
                config),
            std::runtime_error);
    }

    TEST(WalletFacadeTest, ExplicitLegacyFallbackCanBuildLocalFacade)
    {
        midnight::wallet::WalletFacadeConfig config;
        config.indexer_http_url = "https://indexer.preview.midnight.network/api/v4/graphql";
        config.network = midnight::wallet::address::Network::Preview;
        config.require_canonical_wallet_derivation = false;

        auto facade = midnight::wallet::WalletFacade::from_mnemonic_with_config(
            "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
            config);

        EXPECT_EQ(facade.unshielded_address().rfind("mn_addr_preview1", 0), 0u);
    }
}
