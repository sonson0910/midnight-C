#include "midnight/crypto/bip340_signer.hpp"

#include <gtest/gtest.h>
#include <vector>

TEST(Bip340SignerTest, SignaturesVerifyAndDependOnMessage)
{
    midnight::crypto::Bip340Signer::PrivateKey key{};
    key[31] = 4;

    midnight::crypto::Bip340Signer signer;
    signer.set_key(key);

    const std::vector<uint8_t> message_a = {'m', 'i', 'd', 'n', 'i', 'g', 'h', 't'};
    const std::vector<uint8_t> message_b = {'m', 'i', 'd', 'n', 'i', 'g', 'h', 'u'};
    const auto public_key = midnight::crypto::Bip340Signer::public_key_to_hex(
        midnight::crypto::Bip340Signer::derive_public_key(key));

    const auto signature_a = signer.sign_message(message_a);
    const auto signature_b = signer.sign_message(message_b);

    EXPECT_NE(signature_a, signature_b);
    EXPECT_TRUE(midnight::crypto::Bip340Signer::verify_signature(
        message_a, signature_a, public_key));
    EXPECT_FALSE(midnight::crypto::Bip340Signer::verify_signature(
        message_b, signature_a, public_key));
}
