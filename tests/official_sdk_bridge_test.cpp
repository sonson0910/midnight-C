#include <gtest/gtest.h>

#include "midnight/blockchain/official_sdk_bridge.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace
{
    bool is_node_available()
    {
#ifdef _WIN32
        return std::system("node --version >NUL 2>&1") == 0;
#else
        return std::system("node --version >/dev/null 2>&1") == 0;
#endif
    }

    std::filesystem::path make_temp_script(const std::string &prefix, const std::string &content)
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const std::filesystem::path script_path =
            std::filesystem::temp_directory_path() /
            (prefix + "-" + std::to_string(now) + ".mjs");

        std::ofstream out(script_path, std::ios::out | std::ios::trunc);
        out << content;
        out.close();

        return script_path;
    }

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

TEST(OfficialSdkBridgeTest, QueryWalletStateParsesSnapshotPayload)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-wallet-state-bridge",
        "console.log('bridge-log');\n"
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  sourceAddress: 'mn_addr_test_source',\n"
        "  synced: true,\n"
        "  syncError: '',\n"
        "  unshieldedBalance: '100',\n"
        "  pendingUnshieldedBalance: '3',\n"
        "  dustBalance: '8',\n"
        "  dustBalanceError: '',\n"
        "  dustPendingBalance: '1',\n"
        "  dustAvailableCoins: 2,\n"
        "  dustPendingCoins: 1,\n"
        "  availableUtxoCount: 5,\n"
        "  registeredUtxoCount: 2,\n"
        "  unregisteredUtxoCount: 3,\n"
        "  pendingUtxoCount: 1,\n"
        "  likelyDustDesignationLock: false,\n"
        "  likelyPendingUtxoLock: true\n"
        "}, null, 2));\n");

    const auto result = midnight::blockchain::query_official_wallet_state(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "undeployed",
        "",
        "",
        "",
        "",
        30,
        script_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.network, "undeployed");
    EXPECT_EQ(result.source_address, "mn_addr_test_source");
    EXPECT_TRUE(result.synced);
    EXPECT_EQ(result.unshielded_balance, "100");
    EXPECT_EQ(result.pending_unshielded_balance, "3");
    EXPECT_EQ(result.dust_balance, "8");
    EXPECT_EQ(result.dust_pending_balance, "1");
    EXPECT_EQ(result.available_utxo_count, 5U);
    EXPECT_EQ(result.registered_utxo_count, 2U);
    EXPECT_EQ(result.unregistered_utxo_count, 3U);
    EXPECT_EQ(result.pending_utxo_count, 1U);
    EXPECT_FALSE(result.likely_dust_designation_lock);
    EXPECT_TRUE(result.likely_pending_utxo_lock);
    EXPECT_TRUE(result.error.empty());

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, QueryWalletStatePropagatesBridgeFailures)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-wallet-state-bridge-failure",
        "console.log(JSON.stringify({ success: false, error: 'wallet sync failed' }));\n"
        "process.exit(5);\n");

    const auto result = midnight::blockchain::query_official_wallet_state(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "undeployed",
        "",
        "",
        "",
        "",
        10,
        script_path.string());

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("wallet sync failed"), std::string::npos);
    EXPECT_NE(result.error.find("exit code 5"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, QueryWalletStateParsesExplicitLockAndUnlockFlags)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-wallet-state-lock-unlock",
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  sourceAddress: 'mn_addr_lock_unlock',\n"
        "  synced: true,\n"
        "  likelyDustDesignationLock: true,\n"
        "  likelyPendingUtxoLock: false\n"
        "}, null, 2));\n");

    const auto result = midnight::blockchain::query_official_wallet_state(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "undeployed",
        "",
        "",
        "",
        "",
        30,
        script_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.likely_dust_designation_lock);
    EXPECT_FALSE(result.likely_pending_utxo_lock);
    EXPECT_TRUE(result.error.empty());

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, QueryWalletStateLockFlagsDefaultToUnlockedWhenMissing)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-wallet-state-lock-default",
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  sourceAddress: 'mn_addr_lock_default',\n"
        "  synced: true\n"
        "}, null, 2));\n");

    const auto result = midnight::blockchain::query_official_wallet_state(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "undeployed",
        "",
        "",
        "",
        "",
        30,
        script_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.likely_dust_designation_lock);
    EXPECT_FALSE(result.likely_pending_utxo_lock);
    EXPECT_TRUE(result.error.empty());

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, DeployCompactContractParsesSuccessPayload)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-deploy-bridge",
        "console.log('deploy-log');\n"
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  contract: 'FaucetAMM',\n"
        "  contractExport: 'FaucetAMM',\n"
        "  contractModule: '@midnight-ntwrk/counter-contract',\n"
        "  seedHex: 'abc',\n"
        "  sourceAddress: 'mn_addr_source',\n"
        "  feeBps: '10',\n"
        "  constructorNonceHex: '00ff',\n"
        "  contractAddress: '0xcontract',\n"
        "  txId: '0xtxid',\n"
        "  unshieldedBalance: '42',\n"
        "  dustBalance: '7',\n"
        "  dustRegistration: { attempted: true, success: true, registeredUtxos: 2, message: 'ok' },\n"
        "  endpoints: {\n"
        "    nodeUrl: 'http://127.0.0.1:9944',\n"
        "    indexerUrl: 'http://127.0.0.1:8088/api/v3/graphql',\n"
        "    indexerWsUrl: 'ws://127.0.0.1:8088/api/v3/graphql/ws',\n"
        "    proofServer: 'http://127.0.0.1:6300'\n"
        "  }\n"
        "}, null, 2));\n");

    const auto result = midnight::blockchain::deploy_official_compact_contract(
        "FaucetAMM",
        "undeployed",
        "",
        10,
        "",
        "",
        "",
        "",
        "",
        120,
        120,
        300,
        1,
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        script_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.network, "undeployed");
    EXPECT_EQ(result.contract, "FaucetAMM");
    EXPECT_EQ(result.contract_address, "0xcontract");
    EXPECT_EQ(result.txid, "0xtxid");
    EXPECT_EQ(result.unshielded_balance, "42");
    EXPECT_EQ(result.dust_balance, "7");
    EXPECT_TRUE(result.dust_registration_attempted);
    EXPECT_TRUE(result.dust_registration_success);
    EXPECT_EQ(result.dust_registration_registered_utxos, 2U);
    EXPECT_EQ(result.endpoints_node_url, "http://127.0.0.1:9944");
    EXPECT_TRUE(result.error.empty());

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, QueryIndexerSyncStatusParsesDiagnostics)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-indexer-sync-bridge",
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  sourceAddress: 'mn_addr_diag',\n"
        "  syncError: '',\n"
        "  syncDiagnostics: {\n"
        "    facadeIsSynced: true,\n"
        "    shielded: { isConnected: true, isCompleteWithin50: true, applyLag: '0' },\n"
        "    unshielded: { isConnected: true, isCompleteWithin50: false, applyLag: '12' },\n"
        "    dust: { isConnected: false, isCompleteWithin50: false, applyLag: '21' }\n"
        "  }\n"
        "}, null, 2));\n");

    const auto result = midnight::blockchain::query_official_indexer_sync_status(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "undeployed",
        "",
        "",
        "http://127.0.0.1:8088/api/v3/graphql",
        "ws://127.0.0.1:8088/api/v3/graphql/ws",
        30,
        script_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.network, "undeployed");
    EXPECT_EQ(result.source_address, "mn_addr_diag");
    EXPECT_TRUE(result.facade_is_synced);
    EXPECT_TRUE(result.shielded_connected);
    EXPECT_TRUE(result.unshielded_connected);
    EXPECT_FALSE(result.dust_connected);
    EXPECT_TRUE(result.shielded_complete_within_50);
    EXPECT_FALSE(result.unshielded_complete_within_50);
    EXPECT_FALSE(result.dust_complete_within_50);
    EXPECT_FALSE(result.all_connected);
    EXPECT_EQ(result.shielded_apply_lag, "0");
    EXPECT_EQ(result.unshielded_apply_lag, "12");
    EXPECT_EQ(result.dust_apply_lag, "21");
    EXPECT_EQ(result.indexer_url, "http://127.0.0.1:8088/api/v3/graphql");
    EXPECT_EQ(result.indexer_ws_url, "ws://127.0.0.1:8088/api/v3/graphql/ws");
    EXPECT_TRUE(result.error.empty());

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, QueryIndexerSyncStatusPropagatesBridgeFailures)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-indexer-sync-bridge-failure",
        "console.log(JSON.stringify({ success: false, error: 'indexer unavailable' }));\n"
        "process.exit(3);\n");

    const auto result = midnight::blockchain::query_official_indexer_sync_status(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "undeployed",
        "",
        "",
        "",
        "",
        10,
        script_path.string());

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("indexer unavailable"), std::string::npos);
    EXPECT_NE(result.error.find("exit code 3"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, CAbiWalletStateSmallBuffersAreSafelyTruncated)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-wallet-state-cabi-success",
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  sourceAddress: 'mn_addr_very_long_source_address_for_truncation',\n"
        "  synced: true,\n"
        "  syncError: '',\n"
        "  unshieldedBalance: '100',\n"
        "  pendingUnshieldedBalance: '0',\n"
        "  dustBalance: '8',\n"
        "  dustBalanceError: '',\n"
        "  dustPendingBalance: '1',\n"
        "  dustAvailableCoins: 11,\n"
        "  dustPendingCoins: 1,\n"
        "  availableUtxoCount: 22,\n"
        "  registeredUtxoCount: 7,\n"
        "  unregisteredUtxoCount: 15,\n"
        "  pendingUtxoCount: 2,\n"
        "  likelyDustDesignationLock: false,\n"
        "  likelyPendingUtxoLock: false\n"
        "}, null, 2));\n");

    ScopedEnvVar env("MIDNIGHT_OFFICIAL_TRANSFER_SCRIPT", script_path.string());

    int synced = 0;
    uint64_t available_utxos = 0;
    uint64_t registered_utxos = 0;
    char source_address[8] = {};
    char state_json[24] = {};
    char error[128] = {};

    const int rc = midnight_query_official_wallet_state(
        "undeployed",
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "",
        "",
        "",
        "",
        15,
        &synced,
        &available_utxos,
        &registered_utxos,
        source_address,
        sizeof(source_address),
        state_json,
        sizeof(state_json),
        error,
        sizeof(error));

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(synced, 1);
    EXPECT_EQ(available_utxos, 22U);
    EXPECT_EQ(registered_utxos, 7U);
    EXPECT_EQ(std::strlen(source_address), sizeof(source_address) - 1);
    EXPECT_EQ(std::strlen(state_json), sizeof(state_json) - 1);
    EXPECT_STREQ(error, "");

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, CAbiWalletStateFailurePropagatesErrorMessage)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-wallet-state-cabi-failure",
        "console.log(JSON.stringify({ success: false, error: 'wallet state failed in bridge' }));\n"
        "process.exit(9);\n");

    ScopedEnvVar env("MIDNIGHT_OFFICIAL_TRANSFER_SCRIPT", script_path.string());

    int synced = 0;
    uint64_t available_utxos = 0;
    uint64_t registered_utxos = 0;
    char source_address[64] = {};
    char state_json[256] = {};
    char error[256] = {};

    const int rc = midnight_query_official_wallet_state(
        "undeployed",
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "",
        "",
        "",
        "",
        15,
        &synced,
        &available_utxos,
        &registered_utxos,
        source_address,
        sizeof(source_address),
        state_json,
        sizeof(state_json),
        error,
        sizeof(error));

    EXPECT_EQ(rc, 1);
    EXPECT_EQ(std::string(source_address), "");
    EXPECT_EQ(std::string(state_json), "");
    EXPECT_NE(std::string(error).find("wallet state failed in bridge"), std::string::npos);
    EXPECT_NE(std::string(error).find("exit code 9"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, CAbiWalletStateJsonIncludesLockAndUnlockFlags)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-wallet-state-cabi-lock-unlock",
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  sourceAddress: 'mn_addr_cabi_lock_unlock',\n"
        "  synced: true,\n"
        "  likelyDustDesignationLock: true,\n"
        "  likelyPendingUtxoLock: false\n"
        "}, null, 2));\n");

    ScopedEnvVar env("MIDNIGHT_OFFICIAL_TRANSFER_SCRIPT", script_path.string());

    int synced = 0;
    uint64_t available_utxos = 0;
    uint64_t registered_utxos = 0;
    char source_address[128] = {};
    char state_json[1024] = {};
    char error[128] = {};

    const int rc = midnight_query_official_wallet_state(
        "undeployed",
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "",
        "",
        "",
        "",
        15,
        &synced,
        &available_utxos,
        &registered_utxos,
        source_address,
        sizeof(source_address),
        state_json,
        sizeof(state_json),
        error,
        sizeof(error));

    ASSERT_EQ(rc, 0);
    ASSERT_EQ(synced, 1);
    ASSERT_STREQ(error, "");

    const auto parsed = nlohmann::json::parse(state_json);
    ASSERT_TRUE(parsed.contains("likelyDustDesignationLock"));
    ASSERT_TRUE(parsed.contains("likelyPendingUtxoLock"));
    EXPECT_TRUE(parsed.at("likelyDustDesignationLock").get<bool>());
    EXPECT_FALSE(parsed.at("likelyPendingUtxoLock").get<bool>());

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, CAbiDeploySmallBuffersAreSafelyTruncated)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-deploy-cabi-success",
        "console.log(JSON.stringify({\n"
        "  success: true,\n"
        "  network: 'undeployed',\n"
        "  contract: 'FaucetAMM',\n"
        "  contractExport: 'FaucetAMM',\n"
        "  contractModule: '@midnight-ntwrk/counter-contract',\n"
        "  sourceAddress: 'mn_addr_source',\n"
        "  contractAddress: '0xcontract_very_long_for_truncation',\n"
        "  txId: '0xtxid_very_long_for_truncation',\n"
        "  unshieldedBalance: '42',\n"
        "  dustBalance: '7',\n"
        "  dustRegistration: { attempted: true, success: true, registeredUtxos: 2, message: 'ok' },\n"
        "  endpoints: {\n"
        "    nodeUrl: 'http://127.0.0.1:9944',\n"
        "    indexerUrl: 'http://127.0.0.1:8088/api/v3/graphql',\n"
        "    indexerWsUrl: 'ws://127.0.0.1:8088/api/v3/graphql/ws',\n"
        "    proofServer: 'http://127.0.0.1:6300'\n"
        "  }\n"
        "}, null, 2));\n");

    ScopedEnvVar env("MIDNIGHT_OFFICIAL_DEPLOY_SCRIPT", script_path.string());

    char contract_address[10] = {};
    char txid[9] = {};
    char deploy_json[32] = {};
    char error[128] = {};

    const int rc = midnight_deploy_official_compact_contract(
        "FaucetAMM",
        "undeployed",
        "",
        10,
        "",
        "",
        "",
        "",
        30,
        30,
        30,
        contract_address,
        sizeof(contract_address),
        txid,
        sizeof(txid),
        deploy_json,
        sizeof(deploy_json),
        error,
        sizeof(error));

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(std::strlen(contract_address), sizeof(contract_address) - 1);
    EXPECT_EQ(std::strlen(txid), sizeof(txid) - 1);
    EXPECT_EQ(std::strlen(deploy_json), sizeof(deploy_json) - 1);
    EXPECT_STREQ(error, "");

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, CAbiDeployFailurePropagatesErrorMessage)
{
    if (!is_node_available())
    {
        GTEST_SKIP() << "Node.js is not available in PATH";
    }

    const auto script_path = make_temp_script(
        "official-deploy-cabi-failure",
        "console.log(JSON.stringify({ success: false, error: 'deploy bridge failed' }));\n"
        "process.exit(7);\n");

    ScopedEnvVar env("MIDNIGHT_OFFICIAL_DEPLOY_SCRIPT", script_path.string());

    char contract_address[64] = {};
    char txid[64] = {};
    char deploy_json[512] = {};
    char error[256] = {};

    const int rc = midnight_deploy_official_compact_contract(
        "FaucetAMM",
        "undeployed",
        "",
        10,
        "",
        "",
        "",
        "",
        30,
        30,
        30,
        contract_address,
        sizeof(contract_address),
        txid,
        sizeof(txid),
        deploy_json,
        sizeof(deploy_json),
        error,
        sizeof(error));

    EXPECT_EQ(rc, 1);
    EXPECT_EQ(std::string(contract_address), "");
    EXPECT_EQ(std::string(txid), "");
    EXPECT_EQ(std::string(deploy_json), "");
    EXPECT_NE(std::string(error).find("deploy bridge failed"), std::string::npos);
    EXPECT_NE(std::string(error).find("exit code 7"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove(script_path, ec);
}

TEST(OfficialSdkBridgeTest, TransferWithWalletAliasFailsWhenAliasMissing)
{
    const auto wallet_store = std::filesystem::temp_directory_path() /
                              ("official-bridge-wallet-missing-" +
                               std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    std::error_code ec;
    std::filesystem::create_directories(wallet_store, ec);

    const auto result = midnight::blockchain::transfer_official_night_with_wallet(
        "missing01",
        "mn_addr_test_destination",
        "1",
        "preprod",
        "",
        30,
        wallet_store.string(),
        "tools/official-transfer-night.mjs");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("Wallet"), std::string::npos);

    std::filesystem::remove_all(wallet_store, ec);
}

TEST(OfficialSdkBridgeTest, TransferWithWalletAliasRejectsNetworkMismatch)
{
    const auto wallet_store = std::filesystem::temp_directory_path() /
                              ("official-bridge-wallet-network-" +
                               std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    std::error_code ec;
    std::filesystem::create_directories(wallet_store, ec);

    ScopedEnvVar passphrase("MIDNIGHT_WALLET_STORE_PASSPHRASE", "official-bridge-test-passphrase-1234");

    std::string registration_error;
    ASSERT_TRUE(midnight::blockchain::register_wallet_seed_hex(
        "alias001",
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "preprod",
        wallet_store.string(),
        &registration_error))
        << registration_error;

    const auto result = midnight::blockchain::transfer_official_night_with_wallet(
        "alias001",
        "mn_addr_test_destination",
        "1",
        "preview",
        "",
        30,
        wallet_store.string(),
        "tools/official-transfer-night.mjs");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("requested network"), std::string::npos);

    std::filesystem::remove_all(wallet_store, ec);
}

TEST(OfficialSdkBridgeTest, DeployWithWalletAliasFailsWhenAliasMissing)
{
    const auto wallet_store = std::filesystem::temp_directory_path() /
                              ("official-bridge-deploy-wallet-missing-" +
                               std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    std::error_code ec;
    std::filesystem::create_directories(wallet_store, ec);

    const auto result = midnight::blockchain::deploy_official_compact_contract_with_wallet(
        "missing01",
        "FaucetAMM",
        "undeployed",
        10,
        "",
        "",
        "",
        "",
        "",
        30,
        30,
        30,
        1,
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        wallet_store.string(),
        "tools/official-deploy-contract.mjs");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("Wallet"), std::string::npos);

    std::filesystem::remove_all(wallet_store, ec);
}

TEST(OfficialSdkBridgeTest, DeployWithWalletAliasRejectsNetworkMismatch)
{
    const auto wallet_store = std::filesystem::temp_directory_path() /
                              ("official-bridge-deploy-wallet-network-" +
                               std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    std::error_code ec;
    std::filesystem::create_directories(wallet_store, ec);

    ScopedEnvVar passphrase("MIDNIGHT_WALLET_STORE_PASSPHRASE", "official-bridge-test-passphrase-1234");

    std::string registration_error;
    ASSERT_TRUE(midnight::blockchain::register_wallet_seed_hex(
        "aliasdeploy01",
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "preprod",
        wallet_store.string(),
        &registration_error))
        << registration_error;

    const auto result = midnight::blockchain::deploy_official_compact_contract_with_wallet(
        "aliasdeploy01",
        "FaucetAMM",
        "undeployed",
        10,
        "",
        "",
        "",
        "",
        "",
        30,
        30,
        30,
        1,
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        wallet_store.string(),
        "tools/official-deploy-contract.mjs");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("requested network"), std::string::npos);

    std::filesystem::remove_all(wallet_store, ec);
}
