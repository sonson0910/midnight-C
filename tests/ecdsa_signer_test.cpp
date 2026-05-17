#include "midnight/crypto/ecdsa_signer.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

TEST(EcdsaSignerTest, DeterministicRawSignatureVerifies)
{
    midnight::crypto::EcdsaSigner::PrivateKey key{};
    key[31] = 1;

    midnight::crypto::EcdsaSigner signer;
    signer.set_private_key(key);

    const std::vector<uint8_t> message = {'m', 'i', 'd', 'n', 'i', 'g', 'h', 't'};
    auto sig1 = signer.sign_deterministic(message);
    auto sig2 = signer.sign_deterministic(message);

    EXPECT_EQ(sig1, sig2);
    EXPECT_TRUE(midnight::crypto::EcdsaSigner::verify_raw(
        message, sig1, signer.get_public_key()));

    std::vector<uint8_t> tampered = message;
    tampered[0] ^= 0x01;
    EXPECT_FALSE(midnight::crypto::EcdsaSigner::verify_raw(
        tampered, sig1, signer.get_public_key()));
}

TEST(EcdsaSignerTest, DerSignatureRejectsTrailingGarbage)
{
    midnight::crypto::EcdsaSigner::PrivateKey key{};
    key[31] = 2;

    midnight::crypto::EcdsaSigner signer;
    signer.set_private_key(key);

    const std::vector<uint8_t> message = {'b', 'r', 'i', 'd', 'g', 'e'};
    auto der = signer.sign_der(message);

    EXPECT_TRUE(midnight::crypto::EcdsaSigner::verify_der(
        message, der, signer.get_public_key()));

    der.push_back(0x00);
    EXPECT_FALSE(midnight::crypto::EcdsaSigner::verify_der(
        message, der, signer.get_public_key()));
}

TEST(EcdsaSignerTest, RejectsZeroPrivateKey)
{
    midnight::crypto::EcdsaSigner::PrivateKey key{};
    midnight::crypto::EcdsaSigner signer;

    EXPECT_THROW(signer.set_private_key(key), std::runtime_error);
    EXPECT_THROW(midnight::crypto::EcdsaSigner::keypair_from_seed(key), std::runtime_error);
}
