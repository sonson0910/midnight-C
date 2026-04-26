#include <gtest/gtest.h>
#include "midnight/contract/contract_manager.hpp"
#include "midnight/codec/scale_codec.hpp"
#include <sodium.h>

using namespace midnight::contract;
using namespace midnight::codec;

// ═══════════════════════════════════════════════════════════════
// Unit Tests — artifact, encoding, struct defaults
// ═══════════════════════════════════════════════════════════════

class ContractManagerUnitTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (sodium_init() < 0)
            GTEST_SKIP() << "libsodium unavailable";
    }
};

// ─── Struct Defaults ──────────────────────────────────────────

TEST(ContractTypesTest, DeployResult_DefaultValues)
{
    DeployResult r;
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.tx_hash.empty());
    EXPECT_TRUE(r.contract_address.empty());
    EXPECT_EQ(r.block_number, 0u);
}

TEST(ContractTypesTest, CallResult_DefaultValues)
{
    CallResult r;
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.tx_hash.empty());
    EXPECT_EQ(r.gas_used, 0u);
}

TEST(ContractTypesTest, ContractInfo_DefaultValues)
{
    ContractInfo info;
    EXPECT_FALSE(info.exists);
    EXPECT_TRUE(info.address.empty());
    EXPECT_EQ(info.block_height, 0u);
}

TEST(ContractTypesTest, DeployParams_DefaultGasLimit)
{
    DeployParams dp;
    EXPECT_EQ(dp.gas_limit, 5000000000u);
    EXPECT_EQ(dp.value, 0u);
    EXPECT_EQ(dp.tip, 0u);
}

// ─── ContractArtifact ─────────────────────────────────────────

TEST(ContractArtifactTest, ToJson_IncludesName)
{
    ContractArtifact artifact;
    artifact.name = "TestContract";
    artifact.bytecode = {0x01, 0x02, 0x03};
    artifact.circuits["test_circuit"] = {0xAA, 0xBB};

    auto j = artifact.to_json();
    EXPECT_EQ(j["name"], "TestContract");
    EXPECT_EQ(j["bytecode_size"], 3);
    EXPECT_EQ(j["circuit_count"], 1);
}

TEST(ContractArtifactTest, FromJson_RoundTrip)
{
    ContractArtifact original;
    original.name = "RoundTrip";
    original.metadata = {{"version", "1.0"}};

    auto j = original.to_json();
    auto restored = ContractArtifact::from_json(j);
    EXPECT_EQ(restored.name, "RoundTrip");
}

TEST(ContractArtifactTest, LoadFromDir_InvalidPath_Throws)
{
    EXPECT_THROW(
        ContractArtifact::load_from_dir("/nonexistent/path/to/contract"),
        std::runtime_error);
}

// ─── Argument Encoding ────────────────────────────────────────

TEST_F(ContractManagerUnitTest, EncodeConstructorArgs_IntegerTypes)
{
    // nlohmann::json stores literal 10 as signed integer,
    // which triggers encode_compact(10) = 10<<2 = 0x28
    nlohmann::json args = {10};
    auto bytes = ContractManager::encode_constructor_args(args);
    EXPECT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], 0x28); // Compact(10) = 10 << 2 = 40
}

TEST_F(ContractManagerUnitTest, EncodeConstructorArgs_MultipleArgs)
{
    nlohmann::json args = {42, "hello", true};
    auto bytes = ContractManager::encode_constructor_args(args);
    EXPECT_GT(bytes.size(), 3u);
}

TEST_F(ContractManagerUnitTest, EncodeConstructorArgs_HexBytes)
{
    nlohmann::json args = {"0xdeadbeef"};
    auto bytes = ContractManager::encode_constructor_args(args);
    EXPECT_FALSE(bytes.empty());
}

TEST_F(ContractManagerUnitTest, EncodeConstructorArgs_EmptyArray)
{
    nlohmann::json args = nlohmann::json::array();
    auto bytes = ContractManager::encode_constructor_args(args);
    EXPECT_TRUE(bytes.empty());
}

TEST_F(ContractManagerUnitTest, EncodeCallArgs_HasSelector)
{
    nlohmann::json args = {100};
    auto bytes = ContractManager::encode_call_args("mintTokens", args);

    // Should start with 4-byte selector
    EXPECT_GE(bytes.size(), 4u);
}

TEST_F(ContractManagerUnitTest, EncodeCallArgs_DifferentCircuitsDifferentSelectors)
{
    nlohmann::json args = {};
    auto bytes1 = ContractManager::encode_call_args("mint", args);
    auto bytes2 = ContractManager::encode_call_args("burn", args);

    // Different circuit names should produce different selectors
    EXPECT_NE(bytes1, bytes2);
}

TEST_F(ContractManagerUnitTest, EncodeCallArgs_DeterministicSelector)
{
    nlohmann::json args = {};
    auto bytes1 = ContractManager::encode_call_args("setupPool", args);
    auto bytes2 = ContractManager::encode_call_args("setupPool", args);

    EXPECT_EQ(bytes1, bytes2);
}

// ─── Large Integer Encoding ───────────────────────────────────

TEST_F(ContractManagerUnitTest, EncodeConstructorArgs_LargeUint)
{
    nlohmann::json args = {1000000};
    auto bytes = ContractManager::encode_constructor_args(args);
    EXPECT_FALSE(bytes.empty());

    // 1000000 > 0xFFFF so should use u32
    EXPECT_GE(bytes.size(), 4u);
}

TEST_F(ContractManagerUnitTest, EncodeConstructorArgs_Boolean)
{
    nlohmann::json args = {true, false};
    auto bytes = ContractManager::encode_constructor_args(args);
    EXPECT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0x01);
    EXPECT_EQ(bytes[1], 0x00);
}

// ═══════════════════════════════════════════════════════════════
// Integration Tests — require live services
// ═══════════════════════════════════════════════════════════════

class ContractManagerIntegrationTest : public ::testing::Test
{
protected:
    std::unique_ptr<ContractManager> mgr_;

    static constexpr const char *DEFAULT_RPC = "https://rpc.preview.midnight.network";
    static constexpr const char *DEFAULT_PROOF = "https://proof.preview.midnight.network";
    static constexpr const char *DEFAULT_INDEXER = "https://indexer.preview.midnight.network/api/v4/graphql";

    void SetUp() override
    {
        const char *rpc_url = std::getenv("MIDNIGHT_RPC_URL");
        const char *proof_url = std::getenv("MIDNIGHT_PROOF_URL");
        const char *indexer_url = std::getenv("MIDNIGHT_INDEXER_URL");

        mgr_ = std::make_unique<ContractManager>(
            rpc_url ? rpc_url : DEFAULT_RPC,
            proof_url ? proof_url : DEFAULT_PROOF,
            indexer_url ? indexer_url : DEFAULT_INDEXER);
    }
};

TEST_F(ContractManagerIntegrationTest, HealthCheck_ReturnsNodeStatus)
{
    auto health = mgr_->health_check();
    EXPECT_TRUE(health.contains("node"));
}

TEST_F(ContractManagerIntegrationTest, QueryState_InvalidAddress_ReturnsEmpty)
{
    auto info = mgr_->query_state(
        "0x0000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_FALSE(info.exists);
}

TEST_F(ContractManagerIntegrationTest, RPCAccess_GetChainName)
{
    auto name = mgr_->rpc().get_chain_name();
    EXPECT_FALSE(name.empty());
}
