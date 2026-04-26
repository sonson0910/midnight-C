#include <gtest/gtest.h>
#include "midnight/codec/scale_codec.hpp"

using namespace midnight::codec;

// ─── Compact Integer Encoding ─────────────────────────────

TEST(ScaleCodecTest, CompactEncode_SingleByte)
{
    // 0 → 0x00, 1 → 0x04, 42 → 0xa8, 63 → 0xfc
    ScaleEncoder enc;
    enc.encode_compact(0);
    EXPECT_EQ(enc.data(), std::vector<uint8_t>{0x00});

    enc.clear();
    enc.encode_compact(1);
    EXPECT_EQ(enc.data(), std::vector<uint8_t>{0x04});

    enc.clear();
    enc.encode_compact(42);
    EXPECT_EQ(enc.data(), std::vector<uint8_t>{0xa8});

    enc.clear();
    enc.encode_compact(63);
    EXPECT_EQ(enc.data(), std::vector<uint8_t>{0xfc});
}

TEST(ScaleCodecTest, CompactEncode_TwoBytes)
{
    // 64 → 0x0101, 69 → 0x1501
    ScaleEncoder enc;
    enc.encode_compact(64);
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0x01, 0x01}));

    enc.clear();
    enc.encode_compact(69);
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0x15, 0x01}));

    enc.clear();
    enc.encode_compact(16383);
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0xfd, 0xff}));
}

TEST(ScaleCodecTest, CompactEncode_FourBytes)
{
    // 16384 → 0x02 0x00 0x01 0x00
    ScaleEncoder enc;
    enc.encode_compact(16384);
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0x02, 0x00, 0x01, 0x00}));

    enc.clear();
    enc.encode_compact(1073741823); // 2^30 - 1
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0xfe, 0xff, 0xff, 0xff}));
}

TEST(ScaleCodecTest, CompactEncode_BigInteger)
{
    // 1073741824 (2^30) → big-int mode
    ScaleEncoder enc;
    enc.encode_compact(1073741824ULL);
    ASSERT_GE(enc.size(), 5u);
    EXPECT_EQ(enc.data()[0] & 0x03, 0x03); // big-int mode flag
}

TEST(ScaleCodecTest, CompactEncode_LargeValue)
{
    // 100000000000 (100 billion)
    ScaleEncoder enc;
    enc.encode_compact(100000000000ULL);
    ASSERT_GE(enc.size(), 5u);

    // Decode roundtrip
    ScaleDecoder dec(enc.data());
    EXPECT_EQ(dec.decode_compact(), 100000000000ULL);
}

// ─── Compact Decode Roundtrip ─────────────────────────────

TEST(ScaleCodecTest, CompactRoundtrip)
{
    std::vector<uint64_t> test_values = {
        0, 1, 42, 63, 64, 255, 16383, 16384, 65535,
        1073741823ULL, 1073741824ULL,
        1000000000ULL, 100000000000ULL};

    for (auto val : test_values)
    {
        ScaleEncoder enc;
        enc.encode_compact(val);
        ScaleDecoder dec(enc.data());
        EXPECT_EQ(dec.decode_compact(), val) << "Failed for value: " << val;
    }
}

// ─── Fixed-Width Integers ─────────────────────────────────

TEST(ScaleCodecTest, U32LE)
{
    ScaleEncoder enc;
    enc.encode_u32_le(0xDEADBEEF);
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0xEF, 0xBE, 0xAD, 0xDE}));

    ScaleDecoder dec(enc.data());
    EXPECT_EQ(dec.decode_u32_le(), 0xDEADBEEFu);
}

TEST(ScaleCodecTest, U64LE)
{
    ScaleEncoder enc;
    enc.encode_u64_le(0x0102030405060708ULL);
    ASSERT_EQ(enc.size(), 8u);
    EXPECT_EQ(enc.data()[0], 0x08);
    EXPECT_EQ(enc.data()[7], 0x01);

    ScaleDecoder dec(enc.data());
    EXPECT_EQ(dec.decode_u64_le(), 0x0102030405060708ULL);
}

TEST(ScaleCodecTest, U16LE)
{
    ScaleEncoder enc;
    enc.encode_u16_le(0xABCD);
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0xCD, 0xAB}));

    ScaleDecoder dec(enc.data());
    EXPECT_EQ(dec.decode_u16_le(), 0xABCDu);
}

// ─── Bool ─────────────────────────────────────────────────

TEST(ScaleCodecTest, Bool)
{
    ScaleEncoder enc;
    enc.encode_bool(true);
    enc.encode_bool(false);
    EXPECT_EQ(enc.data(), (std::vector<uint8_t>{0x01, 0x00}));

    ScaleDecoder dec(enc.data());
    EXPECT_TRUE(dec.decode_bool());
    EXPECT_FALSE(dec.decode_bool());
}

// ─── Bytes (length-prefixed) ──────────────────────────────

TEST(ScaleCodecTest, BytesEncoding)
{
    ScaleEncoder enc;
    std::vector<uint8_t> payload = {0xCA, 0xFE, 0xBA, 0xBE};
    enc.encode_bytes(payload);
    // compact(4) = 0x10, then 4 bytes
    ASSERT_EQ(enc.size(), 5u);
    EXPECT_EQ(enc.data()[0], 0x10); // compact(4)

    ScaleDecoder dec(enc.data());
    auto decoded = dec.decode_bytes();
    EXPECT_EQ(decoded, payload);
}

TEST(ScaleCodecTest, StringEncoding)
{
    ScaleEncoder enc;
    enc.encode_string("hello");
    // compact(5) = 0x14, then "hello"
    ASSERT_EQ(enc.size(), 6u);
    EXPECT_EQ(enc.data()[0], 0x14);

    ScaleDecoder dec(enc.data());
    EXPECT_EQ(dec.decode_string(), "hello");
}

// ─── Era Encoding ─────────────────────────────────────────

TEST(ScaleCodecTest, ImmortalEra)
{
    ScaleEncoder enc;
    enc.encode_era_immortal();
    EXPECT_EQ(enc.data(), std::vector<uint8_t>{0x00});
}

TEST(ScaleCodecTest, MortalEra)
{
    ScaleEncoder enc;
    enc.encode_era_mortal(1000, 64);
    ASSERT_EQ(enc.size(), 2u);
    // Low nibble should encode period_log2 - 1 = 5
    EXPECT_EQ(enc.data()[0] & 0x0F, 5);
}

// ─── MultiAddress ─────────────────────────────────────────

TEST(ScaleCodecTest, MultiAddressId)
{
    ScaleEncoder enc;
    std::vector<uint8_t> account(32, 0xAB);
    enc.encode_multi_address_id(account);
    ASSERT_EQ(enc.size(), 33u);
    EXPECT_EQ(enc.data()[0], 0x00); // MultiAddress::Id variant
    EXPECT_EQ(enc.data()[1], 0xAB);
}

TEST(ScaleCodecTest, MultiAddressId_InvalidSize)
{
    ScaleEncoder enc;
    std::vector<uint8_t> bad_account(16, 0x00);
    EXPECT_THROW(enc.encode_multi_address_id(bad_account), std::runtime_error);
}

// ─── FinalizeWithLength ───────────────────────────────────

TEST(ScaleCodecTest, FinalizeWithLength)
{
    ScaleEncoder enc;
    enc.encode_u32_le(42);
    auto result = enc.finalize_with_length();
    // compact(4) + 4 bytes = 5 total
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], 0x10); // compact(4)
}

// ─── Hex Utilities ────────────────────────────────────────

TEST(ScaleCodecTest, BytesToHex)
{
    auto hex = util::bytes_to_hex({0xDE, 0xAD, 0xBE, 0xEF});
    EXPECT_EQ(hex, "0xdeadbeef");
}

TEST(ScaleCodecTest, HexToBytes)
{
    auto bytes = util::hex_to_bytes("0xDEADBEEF");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST(ScaleCodecTest, HexToBytes_NoPrefx)
{
    auto bytes = util::hex_to_bytes("cafebabe");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0xCA, 0xFE, 0xBA, 0xBE}));
}

TEST(ScaleCodecTest, HexToBytes_OddLength)
{
    EXPECT_THROW(util::hex_to_bytes("0xABC"), std::runtime_error);
}

// ─── Decoder Error Handling ───────────────────────────────

TEST(ScaleCodecTest, DecoderInsufficientData)
{
    std::vector<uint8_t> data = {0x01};
    ScaleDecoder dec(data);
    EXPECT_THROW(dec.decode_u32_le(), std::runtime_error);
}

// ─── Complex Sequence ─────────────────────────────────────

TEST(ScaleCodecTest, ComplexSequence)
{
    ScaleEncoder enc;
    enc.encode_compact(42);         // nonce
    enc.encode_compact(0);          // tip
    enc.encode_u32_le(22000);       // spec_version
    enc.encode_u32_le(2);           // tx_version
    enc.encode_era_immortal();

    ScaleDecoder dec(enc.data());
    EXPECT_EQ(dec.decode_compact(), 42u);
    EXPECT_EQ(dec.decode_compact(), 0u);
    EXPECT_EQ(dec.decode_u32_le(), 22000u);
    EXPECT_EQ(dec.decode_u32_le(), 2u);
    EXPECT_EQ(dec.decode_u8(), 0x00); // immortal era
}
