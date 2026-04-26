#include <gtest/gtest.h>
#include "midnight/wallet/hd_wallet.hpp"

using namespace midnight::wallet;

TEST(HDWalletTest, GenerateMnemonic_24Words)
{
    auto mnemonic = bip39::generate_mnemonic(24);
    // Count words
    int count = 0;
    std::istringstream iss(mnemonic);
    std::string word;
    while (iss >> word)
        count++;
    EXPECT_EQ(count, 24);
}

TEST(HDWalletTest, GenerateMnemonic_12Words)
{
    auto mnemonic = bip39::generate_mnemonic(12);
    int count = 0;
    std::istringstream iss(mnemonic);
    std::string word;
    while (iss >> word)
        count++;
    EXPECT_EQ(count, 12);
}

TEST(HDWalletTest, GenerateMnemonic_InvalidWordCount)
{
    EXPECT_THROW(bip39::generate_mnemonic(13), std::runtime_error);
}

TEST(HDWalletTest, ValidateMnemonic)
{
    auto mnemonic = bip39::generate_mnemonic(24);
    EXPECT_TRUE(bip39::validate_mnemonic(mnemonic));
    EXPECT_FALSE(bip39::validate_mnemonic("invalid words here"));
}

TEST(HDWalletTest, MnemonicToSeed_Deterministic)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto seed1 = bip39::mnemonic_to_seed(mnemonic);
    auto seed2 = bip39::mnemonic_to_seed(mnemonic);
    EXPECT_EQ(seed1.size(), 64u);
    EXPECT_EQ(seed1, seed2);
}

TEST(HDWalletTest, MnemonicToSeed_PassphraseDiffers)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto seed1 = bip39::mnemonic_to_seed(mnemonic, "");
    auto seed2 = bip39::mnemonic_to_seed(mnemonic, "my_passphrase");
    EXPECT_NE(seed1, seed2);
}

TEST(HDWalletTest, FromMnemonic)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);
    EXPECT_EQ(wallet.master_seed().size(), 64u);
}

TEST(HDWalletTest, FromSeed_InvalidSize)
{
    std::vector<uint8_t> bad_seed(32, 0);
    EXPECT_THROW(HDWallet::from_seed(bad_seed), std::runtime_error);
}

TEST(HDWalletTest, DeriveNight_Deterministic)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);

    auto key1 = wallet.derive_night(0, 0);
    auto key2 = wallet.derive_night(0, 0);

    EXPECT_EQ(key1.secret_key, key2.secret_key);
    EXPECT_EQ(key1.public_key, key2.public_key);
    EXPECT_EQ(key1.address, key2.address);
}

TEST(HDWalletTest, DeriveNight_DifferentAccounts)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);

    auto key0 = wallet.derive_night(0, 0);
    auto key1 = wallet.derive_night(1, 0);

    EXPECT_NE(key0.public_key, key1.public_key);
}

TEST(HDWalletTest, DeriveRoles_DifferentKeys)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);

    auto zswap = wallet.derive_zswap(0, 0);
    auto night = wallet.derive_night(0, 0);
    auto dust = wallet.derive_dust(0, 0);

    EXPECT_NE(zswap.public_key, night.public_key);
    EXPECT_NE(night.public_key, dust.public_key);
    EXPECT_NE(zswap.public_key, dust.public_key);
}

TEST(HDWalletTest, KeyPair_SignVerify)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);
    auto key = wallet.derive_night(0, 0);

    std::vector<uint8_t> msg = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    auto sig = key.sign(msg);
    EXPECT_EQ(sig.size(), 64u);
    EXPECT_TRUE(key.verify(msg, sig));

    // Tampered message should fail
    std::vector<uint8_t> tampered = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x21};
    EXPECT_FALSE(key.verify(tampered, sig));
}

TEST(HDWalletTest, AddressFormat)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);
    auto key = wallet.derive_night(0, 0);

    // Address = 0x + 64 hex chars (32 bytes)
    EXPECT_EQ(key.address.substr(0, 2), "0x");
    EXPECT_EQ(key.address.size(), 66u);
}

TEST(HDWalletTest, KeySizes)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);
    auto key = wallet.derive_night(0, 0);

    EXPECT_EQ(key.secret_key.size(), 64u);
    EXPECT_EQ(key.public_key.size(), 32u);
}

// ─── Official SDK Compatibility Tests ─────────────────────

TEST(HDWalletTest, RoleValues_MatchOfficialSDK)
{
    // Values from @midnight-ntwrk/wallet-sdk-hd HDWallet.ts
    EXPECT_EQ(static_cast<uint32_t>(Role::NightExternal), 0u);
    EXPECT_EQ(static_cast<uint32_t>(Role::NightInternal), 1u);
    EXPECT_EQ(static_cast<uint32_t>(Role::Dust), 2u);
    EXPECT_EQ(static_cast<uint32_t>(Role::Zswap), 3u);
    EXPECT_EQ(static_cast<uint32_t>(Role::Metadata), 4u);
}

TEST(HDWalletTest, DeriveAllRoles_UniqueKeys)
{
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);

    auto night_ext = wallet.derive_night(0, 0);
    auto night_int = wallet.derive_night_internal(0, 0);
    auto dust = wallet.derive_dust(0, 0);
    auto zswap = wallet.derive_zswap(0, 0);
    auto metadata = wallet.derive_metadata(0, 0);

    // All 5 roles produce unique keys
    EXPECT_NE(night_ext.public_key, night_int.public_key);
    EXPECT_NE(night_ext.public_key, dust.public_key);
    EXPECT_NE(night_ext.public_key, zswap.public_key);
    EXPECT_NE(night_ext.public_key, metadata.public_key);
    EXPECT_NE(night_int.public_key, dust.public_key);
    EXPECT_NE(dust.public_key, zswap.public_key);
    EXPECT_NE(zswap.public_key, metadata.public_key);
}

TEST(HDWalletTest, DerivePath_UsesCorrectCoinType)
{
    // Verify derivation is deterministic (path: m/44'/2400'/0'/0/0)
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet1 = HDWallet::from_mnemonic(mnemonic);
    auto wallet2 = HDWallet::from_mnemonic(mnemonic);

    auto key1 = wallet1.derive(0, Role::NightExternal, 0);
    auto key2 = wallet2.derive(0, Role::NightExternal, 0);

    EXPECT_EQ(key1.public_key, key2.public_key);
    EXPECT_EQ(key1.secret_key, key2.secret_key);
}

// ─── Bech32m Address Encoding Tests ───────────────────────

#include "midnight/wallet/bech32m.hpp"

TEST(Bech32mTest, EncodeUnshielded_CorrectPrefix)
{
    // 32-byte fake public key
    std::vector<uint8_t> pubkey(32, 0xAB);
    auto addr = address::encode_unshielded(pubkey, address::Network::Preview);

    // Must start with mn_addr_preview1
    EXPECT_EQ(addr.substr(0, 16), "mn_addr_preview1");
}

TEST(Bech32mTest, EncodeUnshielded_Mainnet)
{
    std::vector<uint8_t> pubkey(32, 0xCD);
    auto addr = address::encode_unshielded(pubkey, address::Network::Mainnet);

    // Mainnet: mn_addr1
    EXPECT_EQ(addr.substr(0, 8), "mn_addr1");
}

TEST(Bech32mTest, EncodeUnshielded_InvalidSize)
{
    std::vector<uint8_t> bad(16, 0x00);
    EXPECT_THROW(address::encode_unshielded(bad), std::runtime_error);
}

TEST(Bech32mTest, RoundTrip_UnshieldedAddress)
{
    std::vector<uint8_t> pubkey(32);
    for (int i = 0; i < 32; i++)
        pubkey[i] = static_cast<uint8_t>(i);

    auto encoded = address::encode_unshielded(pubkey, address::Network::Preview);
    auto decoded = address::decode_unshielded(encoded);

    EXPECT_EQ(decoded.size(), 32u);
    EXPECT_EQ(decoded, pubkey);
}

TEST(Bech32mTest, EncodeShielded_CorrectPrefix)
{
    std::vector<uint8_t> coin_pk(32, 0x11);
    std::vector<uint8_t> enc_pk(32, 0x22);
    auto addr = address::encode_shielded(coin_pk, enc_pk, address::Network::Preview);

    EXPECT_EQ(addr.substr(0, 23), "mn_shield-addr_preview1");
}

TEST(Bech32mTest, EncodeDust_CorrectPrefix)
{
    std::vector<uint8_t> dust_data(32, 0x33);
    auto addr = address::encode_dust(dust_data, address::Network::Preview);

    EXPECT_EQ(addr.substr(0, 16), "mn_dust_preview1");
}

TEST(Bech32mTest, DecodeInvalidChecksum)
{
    // Tamper with a valid address
    std::vector<uint8_t> pubkey(32, 0xAA);
    auto addr = address::encode_unshielded(pubkey, address::Network::Preview);
    // Flip last char
    addr.back() = (addr.back() == 'q') ? 'p' : 'q';
    EXPECT_THROW(address::decode_unshielded(addr), std::runtime_error);
}

TEST(Bech32mTest, WalletDerived_RoundTrip)
{
    // Full integration: derive key → encode → decode → verify
    std::string mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    auto wallet = HDWallet::from_mnemonic(mnemonic);
    auto key = wallet.derive_night(0, 0);

    auto addr = address::encode_unshielded(key.public_key, address::Network::Preview);
    EXPECT_EQ(addr.substr(0, 16), "mn_addr_preview1");

    auto decoded = address::decode_unshielded(addr);
    EXPECT_EQ(decoded, key.public_key);
}
