#include <gtest/gtest.h>
#include "midnight/network/substrate_rpc.hpp"
#include "midnight/codec/scale_codec.hpp"
#include <sodium.h>
#include <vector>
#include <string>

using namespace midnight::network;
using namespace midnight::codec;

// ═══════════════════════════════════════════════════════════════
// Unit Tests — storage key computation, SCALE AccountInfo decode
// ═══════════════════════════════════════════════════════════════

class SubstrateRPCTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure sodium is initialized for hash-based tests
        if (sodium_init() < 0)
            GTEST_SKIP() << "libsodium unavailable";
    }
};

// ─── Storage Key Computation ──────────────────────────────────

TEST_F(SubstrateRPCTest, ComputeAccountStorageKey_StartsWithSystemAccountPrefix)
{
    // System.Account prefix: xxHash128("System") ++ xxHash128("Account")
    std::string key = SubstrateRPC::compute_account_storage_key(
        "d43593c715fdd31c61141abd04a99fd6822c8558854ccde39a5684e7a56da27d");

    // Must start with the pre-computed prefix
    EXPECT_TRUE(key.substr(0, 2) == "0x");
    EXPECT_TRUE(key.find("26aa394eea5630e07c48ae0c9558cef7") != std::string::npos);
    EXPECT_TRUE(key.find("b99d880ec681799c0cf30e8886371da9") != std::string::npos);
}

TEST_F(SubstrateRPCTest, ComputeAccountStorageKey_ContainsAccountId)
{
    std::string account_id = "d43593c715fdd31c61141abd04a99fd6822c8558854ccde39a5684e7a56da27d";
    std::string key = SubstrateRPC::compute_account_storage_key(account_id);

    // blake2_128_concat appends the raw account id
    EXPECT_TRUE(key.find(account_id) != std::string::npos);
}

TEST_F(SubstrateRPCTest, ComputeAccountStorageKey_HandlesHexPrefix)
{
    std::string with_prefix = "0xd43593c715fdd31c61141abd04a99fd6822c8558854ccde39a5684e7a56da27d";
    std::string without_prefix = "d43593c715fdd31c61141abd04a99fd6822c8558854ccde39a5684e7a56da27d";

    auto key1 = SubstrateRPC::compute_account_storage_key(with_prefix);
    auto key2 = SubstrateRPC::compute_account_storage_key(without_prefix);

    EXPECT_EQ(key1, key2);
}

TEST_F(SubstrateRPCTest, ComputeAccountStorageKey_CorrectLength)
{
    std::string account_id = "d43593c715fdd31c61141abd04a99fd6822c8558854ccde39a5684e7a56da27d";
    std::string key = SubstrateRPC::compute_account_storage_key(account_id);

    // 0x + 32 (System) + 32 (Account) + 32 (blake2-128) + 64 (account_id) = 162 chars
    EXPECT_EQ(key.size(), 162u);
}

TEST_F(SubstrateRPCTest, ComputeAccountStorageKey_DeterministicOutput)
{
    std::string account_id = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    auto key1 = SubstrateRPC::compute_account_storage_key(account_id);
    auto key2 = SubstrateRPC::compute_account_storage_key(account_id);

    EXPECT_EQ(key1, key2);
}

TEST_F(SubstrateRPCTest, ComputeAccountStorageKey_DifferentAccountsDifferentKeys)
{
    auto key1 = SubstrateRPC::compute_account_storage_key(
        "d43593c715fdd31c61141abd04a99fd6822c8558854ccde39a5684e7a56da27d");
    auto key2 = SubstrateRPC::compute_account_storage_key(
        "0000000000000000000000000000000000000000000000000000000000000000");

    EXPECT_NE(key1, key2);
}

// ─── AccountInfo SCALE Decode ─────────────────────────────────

TEST_F(SubstrateRPCTest, AccountInfoDecode_EncodedData)
{
    // Manually encode a known AccountInfo:
    //   nonce=5, consumers=1, providers=1, sufficients=0
    //   free=1000000 (u128 LE), reserved=0, frozen=0
    ScaleEncoder enc;

    // nonce (u32 LE)
    enc.encode_u32_le(5);
    // consumers (u32 LE)
    enc.encode_u32_le(1);
    // providers (u32 LE)
    enc.encode_u32_le(1);
    // sufficients (u32 LE)
    enc.encode_u32_le(0);

    // free balance (u128 LE = u64 lower + u64 upper)
    enc.encode_u64_le(1000000);
    enc.encode_u64_le(0); // upper 64 bits

    // reserved (u128 LE)
    enc.encode_u64_le(0);
    enc.encode_u64_le(0);

    // frozen (u128 LE)
    enc.encode_u64_le(0);
    enc.encode_u64_le(0);

    auto bytes = enc.data();
    auto hex = util::bytes_to_hex(bytes); // already includes 0x prefix

    // Decode using the same pattern as get_account_info
    auto decoded_bytes = util::hex_to_bytes(hex);
    ScaleDecoder dec(decoded_bytes);

    uint32_t nonce = dec.decode_u32_le();
    uint32_t consumers = dec.decode_u32_le();
    uint32_t providers = dec.decode_u32_le();
    uint32_t sufficients = dec.decode_u32_le();

    uint64_t free_low = dec.decode_u64_le();
    dec.decode_u64_le(); // upper
    uint64_t reserved_low = dec.decode_u64_le();
    dec.decode_u64_le();
    uint64_t frozen_low = dec.decode_u64_le();
    dec.decode_u64_le();

    EXPECT_EQ(nonce, 5u);
    EXPECT_EQ(consumers, 1u);
    EXPECT_EQ(providers, 1u);
    EXPECT_EQ(sufficients, 0u);
    EXPECT_EQ(free_low, 1000000u);
    EXPECT_EQ(reserved_low, 0u);
    EXPECT_EQ(frozen_low, 0u);
}

// ─── RuntimeVersion Struct ────────────────────────────────────

TEST(RuntimeVersionTest, DefaultValues)
{
    RuntimeVersion v;
    EXPECT_EQ(v.spec_version, 0u);
    EXPECT_EQ(v.tx_version, 0u);
    EXPECT_TRUE(v.spec_name.empty());
}

// ─── SubmitResult Struct ──────────────────────────────────────

TEST(SubmitResultTest, DefaultState)
{
    SubmitResult r;
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.tx_hash.empty());
    EXPECT_TRUE(r.error.empty());
}

// ─── AccountInfo Struct ───────────────────────────────────────

TEST(AccountInfoTest, DefaultValues)
{
    AccountInfo info;
    EXPECT_EQ(info.nonce, 0u);
    EXPECT_EQ(info.free, 0u);
    EXPECT_EQ(info.reserved, 0u);
    EXPECT_EQ(info.frozen, 0u);
}

// ═══════════════════════════════════════════════════════════════
// Integration Tests — connect to live Midnight Preview network
// ═══════════════════════════════════════════════════════════════

static const char *PREVIEW_RPC = "https://rpc.preview.midnight.network";

class SubstrateRPCIntegrationTest : public ::testing::Test
{
protected:
    std::unique_ptr<SubstrateRPC> rpc_;

    void SetUp() override
    {
        // Use env var if set, otherwise default to Preview network
        const char *url = std::getenv("MIDNIGHT_RPC_URL");
        rpc_ = std::make_unique<SubstrateRPC>(url ? url : PREVIEW_RPC, 15000);
    }
};

TEST_F(SubstrateRPCIntegrationTest, GetRuntimeVersion_ReturnsValidVersion)
{
    auto version = rpc_->get_runtime_version();
    EXPECT_FALSE(version.spec_name.empty());
    EXPECT_GT(version.spec_version, 0u);
    EXPECT_GT(version.tx_version, 0u);
}

TEST_F(SubstrateRPCIntegrationTest, GetGenesisHash_Returns64HexChars)
{
    auto hash = rpc_->get_genesis_hash();
    EXPECT_TRUE(hash.substr(0, 2) == "0x");
    EXPECT_EQ(hash.size(), 66u); // 0x + 64 hex chars
}

TEST_F(SubstrateRPCIntegrationTest, GetChainName_ReturnsNonEmpty)
{
    auto name = rpc_->get_chain_name();
    EXPECT_FALSE(name.empty());
}

TEST_F(SubstrateRPCIntegrationTest, GetNodeVersion_ReturnsNonEmpty)
{
    auto version = rpc_->get_node_version();
    EXPECT_FALSE(version.empty());
}

TEST_F(SubstrateRPCIntegrationTest, GetSystemHealth_ReturnsPeersField)
{
    auto health = rpc_->get_system_health();
    EXPECT_TRUE(health.contains("peers"));
    EXPECT_TRUE(health.contains("isSyncing"));
}

TEST_F(SubstrateRPCIntegrationTest, IsSynced_ReturnsBool)
{
    bool synced = rpc_->is_synced();
    // Just verify it doesn't throw
    (void)synced;
}

TEST_F(SubstrateRPCIntegrationTest, GetFinalizedHead_Returns66CharHash)
{
    auto hash = rpc_->get_finalized_head();
    EXPECT_TRUE(hash.substr(0, 2) == "0x");
    EXPECT_EQ(hash.size(), 66u);
}

TEST_F(SubstrateRPCIntegrationTest, GetHeader_ReturnsBlockNumber)
{
    auto header = rpc_->get_header();
    EXPECT_TRUE(header.contains("number"));
}

TEST_F(SubstrateRPCIntegrationTest, GetAccountNonce_ZeroAddressReturnsZeroOrRejects)
{
    // Midnight may reject bare hex addresses for nonce query
    // Both nonce=0 and RPC rejection are valid outcomes
    try
    {
        auto nonce = rpc_->get_account_nonce(
            "0x0000000000000000000000000000000000000000000000000000000000000000");
        EXPECT_EQ(nonce, 0u);
    }
    catch (const std::runtime_error &e)
    {
        // Midnight rejects certain address formats with "Invalid params"
        std::string err = e.what();
        EXPECT_TRUE(err.find("Invalid params") != std::string::npos ||
                    err.find("error") != std::string::npos)
            << "Unexpected error: " << err;
    }
}

TEST_F(SubstrateRPCIntegrationTest, GetFreeBalance_ZeroAddressReturnsZero)
{
    auto balance = rpc_->get_free_balance(
        "0x0000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(balance, 0u);
}

TEST_F(SubstrateRPCIntegrationTest, GetMetadata_ReturnsHexBlob)
{
    auto metadata = rpc_->get_metadata_hex();
    EXPECT_TRUE(metadata.substr(0, 2) == "0x");
    EXPECT_GT(metadata.size(), 100u); // Metadata is typically very large
}

TEST_F(SubstrateRPCIntegrationTest, RuntimeVersionCaching_SameResult)
{
    auto v1 = rpc_->get_runtime_version();
    auto v2 = rpc_->get_runtime_version();
    EXPECT_EQ(v1.spec_version, v2.spec_version);
    EXPECT_EQ(v1.tx_version, v2.tx_version);
}

// ─── Midnight-specific RPC Tests ──────────────────────────────

TEST_F(SubstrateRPCIntegrationTest, MidnightLedgerVersion_ReturnsValue)
{
    auto version = rpc_->midnight_ledger_version();
    // Ledger version should be a non-empty string or object
    EXPECT_FALSE(version.is_null());
}

TEST_F(SubstrateRPCIntegrationTest, MidnightZswapStateRoot_ReturnsNonEmpty)
{
    auto root = rpc_->midnight_zswap_state_root();
    EXPECT_FALSE(root.empty());
}

TEST_F(SubstrateRPCIntegrationTest, MidnightApiVersions_ReturnsValue)
{
    auto versions = rpc_->midnight_api_versions();
    EXPECT_FALSE(versions.is_null());
}
