#include "midnight/wallet/hd_wallet.hpp"
#include <sodium.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <random>

// BIP39 English wordlist (2048 words)
// Abbreviated - include full list for production
#include "midnight/wallet/bip39_wordlist.hpp"

namespace midnight::wallet
{

    // ─── KeyPair ──────────────────────────────────────────────

    std::vector<uint8_t> KeyPair::sign(const std::vector<uint8_t> &message) const
    {
        if (secret_key.size() != 64)
            throw std::runtime_error("Invalid secret key size");

        std::vector<uint8_t> sig(crypto_sign_BYTES);
        unsigned long long sig_len;
        crypto_sign_detached(sig.data(), &sig_len,
                             message.data(), message.size(),
                             secret_key.data());
        sig.resize(sig_len);
        return sig;
    }

    bool KeyPair::verify(const std::vector<uint8_t> &message,
                         const std::vector<uint8_t> &signature) const
    {
        if (public_key.size() != 32 || signature.size() != 64)
            return false;

        return crypto_sign_verify_detached(
                   signature.data(), message.data(), message.size(),
                   public_key.data()) == 0;
    }

    // ─── BIP39 ────────────────────────────────────────────────

    namespace bip39
    {
        std::string generate_mnemonic(int word_count)
        {
            if (word_count != 12 && word_count != 15 && word_count != 18 &&
                word_count != 21 && word_count != 24)
                throw std::runtime_error("word_count must be 12, 15, 18, 21, or 24");

            if (sodium_init() < 0)
                throw std::runtime_error("libsodium init failed");

            // entropy bits: word_count * 11 * 32/33 → 128/160/192/224/256
            int entropy_bits = word_count * 32 / 3;
            int entropy_bytes = entropy_bits / 8;

            std::vector<uint8_t> entropy(entropy_bytes);
            randombytes_buf(entropy.data(), entropy_bytes);

            // SHA-256 hash for checksum
            uint8_t hash[crypto_hash_sha256_BYTES];
            crypto_hash_sha256(hash, entropy.data(), entropy_bytes);

            // Combine entropy + checksum bits
            std::vector<bool> bits;
            bits.reserve(entropy_bits + entropy_bits / 32);

            for (int i = 0; i < entropy_bytes; i++)
                for (int bit = 7; bit >= 0; bit--)
                    bits.push_back((entropy[i] >> bit) & 1);

            int checksum_bits = entropy_bits / 32;
            for (int i = 0; i < checksum_bits; i++)
                bits.push_back((hash[i / 8] >> (7 - (i % 8))) & 1);

            // Convert to word indices (11 bits each)
            std::string result;
            for (int i = 0; i < word_count; i++)
            {
                uint16_t index = 0;
                for (int b = 0; b < 11; b++)
                    index = (index << 1) | (bits[i * 11 + b] ? 1 : 0);

                if (i > 0)
                    result += " ";
                result += BIP39_WORDLIST[index];
            }

            return result;
        }

        bool validate_mnemonic(const std::string &mnemonic)
        {
            // Count words
            int count = 0;
            std::istringstream iss(mnemonic);
            std::string word;
            while (iss >> word)
            {
                bool found = false;
                for (int i = 0; i < 2048; i++)
                {
                    if (BIP39_WORDLIST[i] == word)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
                count++;
            }
            return count == 12 || count == 15 || count == 18 ||
                   count == 21 || count == 24;
        }

        std::vector<uint8_t> mnemonic_to_seed(const std::string &mnemonic,
                                              const std::string &passphrase)
        {
            std::string salt = "mnemonic" + passphrase;
            std::vector<uint8_t> seed(64);

            // PBKDF2-HMAC-SHA512, 2048 iterations
            PKCS5_PBKDF2_HMAC(
                mnemonic.c_str(), static_cast<int>(mnemonic.size()),
                reinterpret_cast<const unsigned char *>(salt.c_str()),
                static_cast<int>(salt.size()),
                2048, EVP_sha512(),
                64, seed.data());

            return seed;
        }
    }

    // ─── HDWallet ─────────────────────────────────────────────

    HDWallet HDWallet::from_mnemonic(const std::string &mnemonic,
                                     const std::string &passphrase)
    {
        if (!bip39::validate_mnemonic(mnemonic))
            throw std::runtime_error("Invalid BIP39 mnemonic");

        auto seed = bip39::mnemonic_to_seed(mnemonic, passphrase);
        return from_seed(seed);
    }

    HDWallet HDWallet::from_seed(const std::vector<uint8_t> &seed_64)
    {
        if (seed_64.size() != 64)
            throw std::runtime_error("Seed must be 64 bytes");

        if (sodium_init() < 0)
            throw std::runtime_error("libsodium init failed");

        HDWallet wallet;
        wallet.seed_ = seed_64;
        return wallet;
    }

    HDWallet::ChainNode HDWallet::slip10_master() const
    {
        // SLIP-10: HMAC-SHA512 with key "ed25519 seed" on the master seed
        const std::string key_str = "ed25519 seed";
        unsigned int out_len = 64;
        uint8_t hmac_out[64];

        HMAC(EVP_sha512(),
             key_str.data(), static_cast<int>(key_str.size()),
             seed_.data(), seed_.size(),
             hmac_out, &out_len);

        ChainNode node;
        std::memcpy(node.key.data(), hmac_out, 32);
        std::memcpy(node.chain_code.data(), hmac_out + 32, 32);
        return node;
    }

    HDWallet::ChainNode HDWallet::slip10_derive_child(const ChainNode &parent,
                                                       uint32_t index) const
    {
        // SLIP-10 Ed25519: only hardened derivation (index |= 0x80000000)
        uint32_t hardened = index | 0x80000000;

        // Data = 0x00 || parent.key || big-endian(hardened_index)
        std::vector<uint8_t> data;
        data.push_back(0x00);
        data.insert(data.end(), parent.key.begin(), parent.key.end());
        data.push_back(static_cast<uint8_t>((hardened >> 24) & 0xFF));
        data.push_back(static_cast<uint8_t>((hardened >> 16) & 0xFF));
        data.push_back(static_cast<uint8_t>((hardened >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>(hardened & 0xFF));

        unsigned int out_len = 64;
        uint8_t hmac_out[64];

        HMAC(EVP_sha512(),
             parent.chain_code.data(), 32,
             data.data(), data.size(),
             hmac_out, &out_len);

        ChainNode child;
        std::memcpy(child.key.data(), hmac_out, 32);
        std::memcpy(child.chain_code.data(), hmac_out + 32, 32);
        return child;
    }

    HDWallet::ChainNode HDWallet::derive_path(uint32_t account,
                                               uint32_t role,
                                               uint32_t index) const
    {
        // Path: m/44'/2400'/account'/role'/index'
        // COIN_TYPE = 2400 (official Midnight SLIP-44 registration)
        auto node = slip10_master();
        node = slip10_derive_child(node, 44);     // purpose
        node = slip10_derive_child(node, 2400);   // coin type (Midnight)
        node = slip10_derive_child(node, account);
        node = slip10_derive_child(node, role);
        node = slip10_derive_child(node, index);
        return node;
    }

    KeyPair HDWallet::derive(uint32_t account, Role role, uint32_t index) const
    {
        auto node = derive_path(account, static_cast<uint32_t>(role), index);

        // Ed25519 key pair from seed (the derived 32-byte key IS the seed)
        KeyPair kp;
        kp.secret_key.resize(crypto_sign_SECRETKEYBYTES); // 64
        kp.public_key.resize(crypto_sign_PUBLICKEYBYTES); // 32

        crypto_sign_seed_keypair(kp.public_key.data(), kp.secret_key.data(),
                                 node.key.data());

        // Address = 0x + hex(public_key)
        std::ostringstream oss;
        oss << "0x";
        for (auto b : kp.public_key)
            oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        kp.address = oss.str();

        return kp;
    }

    KeyPair HDWallet::derive_night(uint32_t account, uint32_t index) const
    {
        return derive(account, Role::NightExternal, index);
    }

    KeyPair HDWallet::derive_night_internal(uint32_t account, uint32_t index) const
    {
        return derive(account, Role::NightInternal, index);
    }

    KeyPair HDWallet::derive_dust(uint32_t account, uint32_t index) const
    {
        return derive(account, Role::Dust, index);
    }

    KeyPair HDWallet::derive_zswap(uint32_t account, uint32_t index) const
    {
        return derive(account, Role::Zswap, index);
    }

    KeyPair HDWallet::derive_metadata(uint32_t account, uint32_t index) const
    {
        return derive(account, Role::Metadata, index);
    }

} // namespace midnight::wallet
