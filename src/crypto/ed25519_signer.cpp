#include "midnight/crypto/ed25519_signer.hpp"
#include "midnight/core/logger.hpp"
#include <stdexcept>
#include <sstream>
#include <iomanip>

#if __has_include(<sodium.h>)
#include <sodium.h>
#define MIDNIGHT_HAS_SODIUM 1
#else
#define MIDNIGHT_HAS_SODIUM 0
#endif

namespace midnight::crypto
{
    void Ed25519Signer::initialize()
    {
#if MIDNIGHT_HAS_SODIUM
        if (sodium_init() < 0)
        {
            std::string error = "libsodium initialization failed";
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }
        midnight::g_logger->info("libsodium initialized for Ed25519 cryptography");
#else
        const std::string error = "libsodium is required for Ed25519 cryptography";
        midnight::g_logger->error(error);
        throw std::runtime_error(error);
#endif
    }

    std::pair<Ed25519Signer::PublicKey, Ed25519Signer::PrivateKey>
    Ed25519Signer::generate_keypair()
    {
#if MIDNIGHT_HAS_SODIUM
        if (sodium_initialized() == 0)
        {
            initialize();
        }

        PublicKey pub_key{};
        PrivateKey priv_key{};

        if (crypto_sign_keypair(pub_key.data(), priv_key.data()) != 0)
        {
            std::string error = "Failed to generate Ed25519 keypair";
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }

        midnight::g_logger->debug("Generated new Ed25519 keypair");
        return {pub_key, priv_key};
#else
        throw std::runtime_error("libsodium is required for Ed25519 key generation");
#endif
    }

    std::pair<Ed25519Signer::PublicKey, Ed25519Signer::PrivateKey>
    Ed25519Signer::keypair_from_seed(const std::array<uint8_t, SECRET_SEED_SIZE> &seed)
    {
#if MIDNIGHT_HAS_SODIUM
        if (sodium_initialized() == 0)
        {
            initialize();
        }

        PublicKey pub_key{};
        PrivateKey priv_key{};

        if (crypto_sign_seed_keypair(pub_key.data(), priv_key.data(), seed.data()) != 0)
        {
            std::string error = "Failed to generate keypair from seed";
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }

        midnight::g_logger->debug("Generated Ed25519 keypair from seed (deterministic)");
        return {pub_key, priv_key};
#else
        (void)seed;
        throw std::runtime_error("libsodium is required for Ed25519 deterministic derivation");
#endif
    }

    Ed25519Signer::Signature Ed25519Signer::sign_message(
        const std::string &message,
        const PrivateKey &private_key)
    {
        return sign_message(
            reinterpret_cast<const uint8_t *>(message.data()),
            message.length(),
            private_key);
    }

    Ed25519Signer::Signature Ed25519Signer::sign_message(
        const uint8_t *message,
        size_t message_len,
        const PrivateKey &private_key)
    {
#if MIDNIGHT_HAS_SODIUM
        if (sodium_initialized() == 0)
        {
            initialize();
        }

        Signature sig{};
        unsigned long long sig_len = 0;

        if (crypto_sign_detached(
                sig.data(),
                &sig_len,
                message,
                message_len,
                private_key.data()) != 0)
        {
            std::string error = "Failed to sign message";
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }

        if (sig_len != SIGNATURE_SIZE)
        {
            std::string error = "Signature size mismatch: expected " +
                                std::to_string(SIGNATURE_SIZE) + " got " +
                                std::to_string(sig_len);
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }

        midnight::g_logger->debug("Message signed successfully (length: " +
                                  std::to_string(message_len) + " bytes)");
        return sig;
#else
        (void)message;
        (void)message_len;
        (void)private_key;
        throw std::runtime_error("libsodium is required for Ed25519 signing");
#endif
    }

    bool Ed25519Signer::verify_signature(
        const std::string &message,
        const Signature &signature,
        const PublicKey &public_key)
    {
        return verify_signature(
            reinterpret_cast<const uint8_t *>(message.data()),
            message.length(),
            signature,
            public_key);
    }

    bool Ed25519Signer::verify_signature(
        const uint8_t *message,
        size_t message_len,
        const Signature &signature,
        const PublicKey &public_key)
    {
#if MIDNIGHT_HAS_SODIUM
        if (sodium_initialized() == 0)
        {
            initialize();
        }

        int result = crypto_sign_verify_detached(
            signature.data(),
            message,
            message_len,
            public_key.data());

        bool valid = (result == 0);
        midnight::g_logger->debug("Signature verification: " + std::string(valid ? "VALID" : "INVALID"));
        return valid;
#else
        (void)message;
        (void)message_len;
        (void)signature;
        (void)public_key;
        throw std::runtime_error("libsodium is required for Ed25519 verification");
#endif
    }

    Ed25519Signer::PublicKey Ed25519Signer::extract_public_key(
        const PrivateKey &private_key)
    {
        // In Ed25519, the private key is actually 64 bytes: [32 bytes secret | 32 bytes public]
        // The public key is the second half
        PublicKey pub_key{};
        std::copy(private_key.begin() + 32, private_key.end(), pub_key.begin());
        return pub_key;
    }

    std::string Ed25519Signer::public_key_to_hex(const PublicKey &key)
    {
        std::ostringstream oss;
        for (uint8_t byte : key)
        {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return oss.str();
    }

    Ed25519Signer::PublicKey Ed25519Signer::public_key_from_hex(
        const std::string &hex_string)
    {
        if (hex_string.length() != 64)
        {
            throw std::invalid_argument(
                "Public key hex string must be exactly 64 characters (32 bytes hex)");
        }

        PublicKey key{};
        for (size_t i = 0; i < 32; ++i)
        {
            std::string byte_str = hex_string.substr(i * 2, 2);
            try
            {
                key[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            }
            catch (...)
            {
                throw std::invalid_argument("Invalid hex string format");
            }
        }
        return key;
    }

    std::string Ed25519Signer::signature_to_hex(const Signature &sig)
    {
        std::ostringstream oss;
        for (uint8_t byte : sig)
        {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return oss.str();
    }

    Ed25519Signer::Signature Ed25519Signer::signature_from_hex(
        const std::string &hex_string)
    {
        if (hex_string.length() != 128)
        {
            throw std::invalid_argument(
                "Signature hex string must be exactly 128 characters (64 bytes hex)");
        }

        Signature sig{};
        for (size_t i = 0; i < 64; ++i)
        {
            std::string byte_str = hex_string.substr(i * 2, 2);
            try
            {
                sig[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            }
            catch (...)
            {
                throw std::invalid_argument("Invalid hex string format");
            }
        }
        return sig;
    }

} // namespace midnight::crypto
