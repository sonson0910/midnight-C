#include <gtest/gtest.h>
#include "midnight/tx/extrinsic_builder.hpp"
#include "midnight/codec/scale_codec.hpp"
#include "midnight/wallet/hd_wallet.hpp"

using namespace midnight::tx;
using namespace midnight::codec;
using namespace midnight::wallet;

class ExtrinsicBuilderTest : public ::testing::Test
{
protected:
    ExtrinsicParams default_params()
    {
        ExtrinsicParams params;
        params.spec_version = 22000;
        params.tx_version = 2;
        params.genesis_hash.resize(32, 0xAA);
        params.block_hash.resize(32, 0xBB);
        params.nonce = 0;
        params.tip = 0;
        params.mortal_era = false;
        return params;
    }

    KeyPair test_keypair()
    {
        std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                               "abandon abandon abandon abandon abandon about";
        auto wallet = HDWallet::from_mnemonic(mnemonic);
        return wallet.derive_night(0, 0);
    }
};

TEST_F(ExtrinsicBuilderTest, SystemRemark)
{
    auto call = PalletCall::system_remark({0x48, 0x65, 0x6C, 0x6C, 0x6F});
    EXPECT_EQ(call.pallet_index, 0x00);
    EXPECT_EQ(call.call_index, 0x00);
    EXPECT_FALSE(call.call_data.empty());
}

TEST_F(ExtrinsicBuilderTest, CustomCall)
{
    auto call = PalletCall::custom(0x0A, 0x01, {0xDE, 0xAD});
    EXPECT_EQ(call.pallet_index, 0x0A);
    EXPECT_EQ(call.call_index, 0x01);
    EXPECT_EQ(call.call_data.size(), 2u);
}

TEST_F(ExtrinsicBuilderTest, BuildSigned_Structure)
{
    auto params = default_params();
    ExtrinsicBuilder builder(params);
    auto key = test_keypair();

    auto call = PalletCall::system_remark({0x48, 0x65, 0x6C, 0x6C, 0x6F});
    auto extrinsic = builder.build_signed(call, key.secret_key, key.public_key);

    // Minimum size: compact_len + version(1) + multiaddr(33) + sig_type(1) +
    //               sig(64) + era(1) + nonce(1) + tip(1) + call(2+)
    EXPECT_GT(extrinsic.size(), 100u);

    // Decode compact length
    ScaleDecoder dec(extrinsic);
    auto body_len = dec.decode_compact();
    EXPECT_EQ(body_len + dec.offset(), extrinsic.size());

    // Version byte should be 0x84 (signed, v4)
    EXPECT_EQ(dec.decode_u8(), 0x84);
}

TEST_F(ExtrinsicBuilderTest, BuildSigned_Deterministic)
{
    auto params = default_params();
    ExtrinsicBuilder builder(params);
    auto key = test_keypair();

    auto call = PalletCall::system_remark({0x01, 0x02, 0x03});
    auto ext1 = builder.build_signed(call, key.secret_key, key.public_key);
    auto ext2 = builder.build_signed(call, key.secret_key, key.public_key);

    // Ed25519 is deterministic, so same key + message = same signature
    EXPECT_EQ(ext1, ext2);
}

TEST_F(ExtrinsicBuilderTest, BuildUnsigned)
{
    auto params = default_params();
    ExtrinsicBuilder builder(params);

    auto call = PalletCall::system_remark({0xFF});
    auto extrinsic = builder.build_unsigned(call);

    ScaleDecoder dec(extrinsic);
    auto body_len = dec.decode_compact();
    EXPECT_EQ(body_len + dec.offset(), extrinsic.size());

    // Version byte: 0x04 (unsigned, v4)
    EXPECT_EQ(dec.decode_u8(), 0x04);
}

TEST_F(ExtrinsicBuilderTest, SigningPayload_SmallNotHashed)
{
    auto params = default_params();
    ExtrinsicBuilder builder(params);

    auto call = PalletCall::system_remark({0x01});
    auto payload = builder.build_signing_payload(call);

    // Small payload should NOT be hashed (> 32 bytes)
    EXPECT_GT(payload.size(), 32u);
}

TEST_F(ExtrinsicBuilderTest, ToHex)
{
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto hex = ExtrinsicBuilder::to_hex(data);
    EXPECT_EQ(hex, "0xdeadbeef");
}

TEST_F(ExtrinsicBuilderTest, InvalidParams_GenesisHash)
{
    ExtrinsicParams params;
    params.spec_version = 1;
    params.tx_version = 1;
    params.genesis_hash.resize(16, 0); // Wrong size
    params.block_hash.resize(32, 0);
    EXPECT_THROW(ExtrinsicBuilder{params}, std::runtime_error);
}

TEST_F(ExtrinsicBuilderTest, InvalidKey_SecretKeySize)
{
    auto params = default_params();
    ExtrinsicBuilder builder(params);
    auto call = PalletCall::system_remark({0x01});

    std::vector<uint8_t> bad_sk(32, 0);
    std::vector<uint8_t> pk(32, 0);
    EXPECT_THROW(builder.build_signed(call, bad_sk, pk), std::runtime_error);
}

TEST_F(ExtrinsicBuilderTest, MortalEra)
{
    auto params = default_params();
    params.mortal_era = true;
    params.era_block = 1000;
    params.era_period = 64;
    ExtrinsicBuilder builder(params);
    auto key = test_keypair();

    auto call = PalletCall::system_remark({0x01});
    auto extrinsic = builder.build_signed(call, key.secret_key, key.public_key);

    // Should succeed without throwing
    EXPECT_GT(extrinsic.size(), 100u);
}

TEST_F(ExtrinsicBuilderTest, WithNonceAndTip)
{
    auto params = default_params();
    params.nonce = 42;
    params.tip = 1000;
    ExtrinsicBuilder builder(params);
    auto key = test_keypair();

    auto call = PalletCall::system_remark({0x01});
    auto extrinsic = builder.build_signed(call, key.secret_key, key.public_key);

    // Different nonce/tip = different extrinsic
    params.nonce = 0;
    params.tip = 0;
    ExtrinsicBuilder builder2(params);
    auto extrinsic2 = builder2.build_signed(call, key.secret_key, key.public_key);

    EXPECT_NE(extrinsic, extrinsic2);
}
