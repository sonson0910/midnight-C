#include <gtest/gtest.h>
#include "midnight/contract/contract_manager.hpp"
#include "midnight/codec/scale_codec.hpp"
#include <cstdlib>
#include <stdexcept>

using namespace midnight::contract;
using namespace midnight::codec;

// ═══════════════════════════════════════════════════════════════
// Unit Tests — artifact, encoding, struct defaults
// ═══════════════════════════════════════════════════════════════

class ContractManagerUnitTest : public ::testing::Test
{
protected:
    void SetUp() override {}
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

TEST_F(ContractManagerUnitTest, LegacyConstructorEncodingIsDisabled)
{
    nlohmann::json args = {10};
    EXPECT_THROW(
        ContractManager::encode_constructor_args(args),
        std::logic_error);
}

TEST_F(ContractManagerUnitTest, LegacyCallEncodingIsDisabled)
{
    nlohmann::json args = {100};
    EXPECT_THROW(
        ContractManager::encode_call_args("mintTokens", args),
        std::logic_error);
}

// ═══════════════════════════════════════════════════════════════
// Integration Tests — require live services
// ═══════════════════════════════════════════════════════════════

class ContractManagerIntegrationTest : public ::testing::Test
{
protected:
    std::unique_ptr<ContractManager> mgr_;

    static constexpr const char *DEFAULT_RPC = "https://rpc.preprod.midnight.network";
    static constexpr const char *DEFAULT_PROOF = "https://proof.preprod.midnight.network";
    static constexpr const char *DEFAULT_INDEXER = "https://indexer.preprod.midnight.network/api/v4/graphql";

    void SetUp() override
    {
        if (std::getenv("MIDNIGHT_RUN_LIVE_RPC_TESTS") == nullptr)
        {
            GTEST_SKIP() << "Set MIDNIGHT_RUN_LIVE_RPC_TESTS=1 to run live contract manager checks";
        }

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
