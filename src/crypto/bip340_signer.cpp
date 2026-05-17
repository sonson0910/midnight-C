/**
 * @file bip340_signer.cpp
 * @brief BIP-340 Schnorr signature implementation for Midnight
 *
 * Midnight uses BIP-340 (Schnorr signatures on secp256k1) for unshielded
 * transaction signing. This implementation follows the BIP-340 specification.
 */

#include "midnight/crypto/bip340_signer.hpp"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace midnight::crypto
{
    namespace
    {
        // secp256k1 curve order n
        constexpr uint8_t SECP256K1_N[32] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
            0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
            0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
        };

        // Get secp256k1 EC_GROUP (cached)
        EC_GROUP* get_secp256k1_group() {
            static EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
            return group;
        }

        // SHA-256 hash
        std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len) {
            std::array<uint8_t, 32> out{};
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
            EVP_DigestUpdate(ctx, data, len);
            unsigned int out_len = 32;
            EVP_DigestFinal_ex(ctx, out.data(), &out_len);
            EVP_MD_CTX_free(ctx);
            return out;
        }

        // BIP-340 tagged hash
        std::array<uint8_t, 32> tagged_hash(const uint8_t* msg, size_t msg_len, const char* tag) {
            auto tag_hash = sha256(reinterpret_cast<const uint8_t*>(tag), std::strlen(tag));
            std::vector<uint8_t> data;
            data.reserve(64 + msg_len);
            data.insert(data.end(), tag_hash.begin(), tag_hash.end());
            data.insert(data.end(), tag_hash.begin(), tag_hash.end());
            data.insert(data.end(), msg, msg + msg_len);
            return sha256(data.data(), data.size());
        }

        // Convert BIGNUM to bytes (big-endian)
        void bn_to_bytes(const BIGNUM* bn, uint8_t* out, size_t len) {
            std::memset(out, 0, len);
            int bn_len = BN_num_bytes(bn);
            if (bn_len > 0 && bn_len <= (int)len) {
                BN_bn2bin(bn, out + (len - bn_len));
            }
        }

        // Convert hex string to bytes
        bool hex_to_bytes(const std::string& hex, uint8_t* out, size_t out_len) {
            std::string clean = hex;
            if (clean.size() >= 2 && clean.substr(0, 2) == "0x") {
                clean = clean.substr(2);
            }
            if (clean.size() != out_len * 2) return false;
            for (size_t i = 0; i < clean.size() && i / 2 < out_len; i += 2) {
                unsigned int byte;
                std::istringstream(clean.substr(i, 2)) >> std::hex >> byte;
                out[i / 2] = static_cast<uint8_t>(byte);
            }
            return true;
        }

        // Compute point = scalar * G, returns (x, y) coordinates
        bool point_mul_base(const BIGNUM* scalar, uint8_t* out_x, uint8_t* out_y, int* out_y_parity) {
            EC_GROUP* group = get_secp256k1_group();
            if (!group) return false;

            BN_CTX* ctx = BN_CTX_new();
            if (!ctx) return false;

            const EC_POINT* gen = EC_GROUP_get0_generator(group);
            EC_POINT* G = EC_POINT_new(group);
            if (!gen || !G || !EC_POINT_copy(G, gen)) {
                if (G) EC_POINT_free(G);
                BN_CTX_free(ctx);
                return false;
            }

            EC_POINT* point = EC_POINT_new(group);
            bool success = false;
            int parity = 0;

            if (EC_POINT_mul(group, point, nullptr, G, scalar, ctx)) {
                BIGNUM* x = BN_new();
                BIGNUM* y = BN_new();
                if (x && y) {
                    if (EC_POINT_get_affine_coordinates(group, point, x, y, ctx)) {
                        bn_to_bytes(x, out_x, 32);
                        bn_to_bytes(y, out_y, 32);
                        parity = BN_is_odd(y);
                        if (out_y_parity) *out_y_parity = parity;
                        success = true;
                    }
                    BN_free(y);
                }
                if (x) BN_free(x);
            }

            EC_POINT_free(point);
            EC_POINT_free(G);
            BN_CTX_free(ctx);
            return success;
        }

        // Derive x-only public key from private key (BIP-340: force odd y)
        std::array<uint8_t, 32> derive_pubkey_internal(const uint8_t* private_key) {
            std::array<uint8_t, 32> pk{};
            BIGNUM* sk = BN_bin2bn(private_key, 32, nullptr);
            uint8_t y[32];
            int parity = 0;
            if (point_mul_base(sk, pk.data(), y, &parity)) {
                if (parity == 0) {
                    EC_GROUP* group = get_secp256k1_group();
                    BN_CTX* ctx = BN_CTX_new();
                    BIGNUM* p = BN_new();
                    EC_GROUP_get_curve(group, p, nullptr, nullptr, ctx);

                    BIGNUM* x = BN_bin2bn(pk.data(), 32, nullptr);
                    BIGNUM* y_bn = BN_bin2bn(y, 32, nullptr);
                    EC_POINT* pt = EC_POINT_new(group);
                    EC_POINT_set_affine_coordinates(group, pt, x, y_bn, ctx);

                    BN_mod_sub(y_bn, p, y_bn, p, ctx);
                    EC_POINT_set_affine_coordinates(group, pt, x, y_bn, ctx);

                    BN_free(x);
                    x = BN_new();
                    BN_free(y_bn);
                    y_bn = BN_new();
                    EC_POINT_get_affine_coordinates(group, pt, x, y_bn, ctx);
                    bn_to_bytes(x, pk.data(), 32);

                    BN_free(x); BN_free(y_bn); BN_free(p);
                    EC_POINT_free(pt);
                    BN_CTX_free(ctx);
                }
            }
            BN_free(sk);
            return pk;
        }

        // RFC 6979 deterministic nonce (simplified BIP-340 version)
        void nonce_rfc6979(const uint8_t* message,
                           const uint8_t* private_key,
                           const uint8_t* x_only_pubkey,
                           uint8_t* nonce_out) {
            uint8_t V[32] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                             0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                             0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                             0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
            uint8_t K[32] = {0};

            uint8_t data1[32 + 1 + 32 + 32 + 32];
            memcpy(data1, V, 32);
            data1[32] = 0x00;
            memcpy(data1 + 33, private_key, 32);
            memcpy(data1 + 65, x_only_pubkey, 32);
            memcpy(data1 + 97, message, 32);
            HMAC(EVP_sha256(), K, 32, data1, sizeof(data1), K, nullptr);
            HMAC(EVP_sha256(), K, 32, V, 32, V, nullptr);

            uint8_t data2[32 + 1 + 32 + 32 + 32];
            memcpy(data2, V, 32);
            data2[32] = 0x01;
            memcpy(data2 + 33, private_key, 32);
            memcpy(data2 + 65, x_only_pubkey, 32);
            memcpy(data2 + 97, message, 32);
            HMAC(EVP_sha256(), K, 32, data2, sizeof(data2), K, nullptr);
            HMAC(EVP_sha256(), K, 32, V, 32, V, nullptr);

            int attempts = 0;
            while (attempts < 100) {
                HMAC(EVP_sha256(), K, 32, V, 32, V, nullptr);

                bool is_zero = true;
                for (int i = 0; i < 32; i++) {
                    if (V[i] != 0) { is_zero = false; break; }
                }

                if (!is_zero) {
                    // BIP-340 for secp256k1: clear bit 7, bits 1,2
                    V[0] &= 0x7F;
                    V[31] &= 0xFC;
                    memcpy(nonce_out, V, 32);
                    return;
                }

                uint8_t K_tmp[32];
                HMAC(EVP_sha256(), K, 32, V, 33, K_tmp, nullptr);
                memcpy(K, K_tmp, 32);
                attempts++;
            }
        }
    } // anonymous namespace

    Bip340Signer::Bip340Signer() : has_key_(false) {}

    Bip340Signer::Bip340Signer(const std::string& hex_sk) : has_key_(false) {
        std::string clean_hex = hex_sk;
        if (clean_hex.size() >= 2 && clean_hex.substr(0, 2) == "0x") {
            clean_hex = clean_hex.substr(2);
        }
        if (clean_hex.size() != 64) {
            throw std::invalid_argument("BIP-340 private key must be 32 bytes");
        }
        std::array<uint8_t, 32> sk{};
        for (size_t i = 0; i < 64; i += 2) {
            unsigned int byte;
            std::istringstream(clean_hex.substr(i, 2)) >> std::hex >> byte;
            sk[i / 2] = static_cast<uint8_t>(byte);
        }
        set_key(sk);
    }

    void Bip340Signer::set_key(const std::array<uint8_t, 32>& secret_key) {
        // BIP-340 for secp256k1: ensure scalar is in valid range [1, n-1]
        // - Clear bit 7 of first byte (ensures < 2^255)
        // - Clear bits 1,2 of last byte (BIP-340 spec for secp256k1)
        // - Keep bit 0 of last byte (unlike Ed25519 which clears it)
        std::array<uint8_t, 32> clamped = secret_key;
        clamped[0] &= 0x7F;  // Clear bit 7 (ensures < 2^255)
        clamped[31] &= 0xFC;  // Clear bits 1,2 (BIP-340 for secp256k1)
        
        // Check for zero scalar
        bool is_zero = true;
        for (auto b : clamped) { if (b != 0) { is_zero = false; break; } }
        if (is_zero) {
            throw std::invalid_argument("BIP-340 private key cannot be zero");
        }
        
        secret_key_ = clamped;
        public_key_ = derive_pubkey_internal(secret_key_.data());
        has_key_ = true;
    }

    std::array<uint8_t, 32> Bip340Signer::derive_public_key(const std::array<uint8_t, 32>& secret_key) {
        return derive_pubkey_internal(secret_key.data());
    }

    std::pair<std::array<uint8_t, 32>, std::array<uint8_t, 32>> Bip340Signer::generate_keypair() {
        std::array<uint8_t, 32> private_key{};
        if (RAND_bytes(private_key.data(), 32) != 1) {
            throw std::runtime_error("Failed to generate random bytes");
        }
        // BIP-340 for secp256k1: clear bit 7 of first byte, bits 1,2 of last byte
        private_key[0] &= 0x7F;
        private_key[31] &= 0xFC;
        bool is_zero = true;
        for (auto b : private_key) { if (b != 0) { is_zero = false; break; } }
        if (is_zero) private_key[31] = 1;

        auto public_key = derive_pubkey_internal(private_key.data());
        return {private_key, public_key};
    }

    std::string Bip340Signer::sign_message(const std::vector<uint8_t>& message) const {
        if (!has_key_) {
            throw std::runtime_error("BIP-340 signer has no key set");
        }

        OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS | OPENSSL_INIT_ADD_ALL_CIPHERS, nullptr);

        // Midnight uses BIP-340 with custom domain separator for transaction signing
        auto msg_hash = tagged_hash(message.data(), message.size(), "Midnight/transaction");

        std::array<uint8_t, 32> nonce{};
        nonce_rfc6979(msg_hash.data(), secret_key_.data(), public_key_.data(), nonce.data());

        std::array<uint8_t, 32> Rx{};
        uint8_t Ry[32];
        int Ry_parity = 0;
        BIGNUM* k_orig = BN_bin2bn(nonce.data(), 32, nullptr);
        if (!k_orig) {
            throw std::runtime_error("BN_bin2bn failed for nonce");
        }
        if (!point_mul_base(k_orig, Rx.data(), Ry, &Ry_parity)) {
            BN_free(k_orig);
            throw std::runtime_error("Failed to compute R");
        }

        BIGNUM* k = nullptr;
        if (Ry_parity == 1) {
            EC_GROUP* group = get_secp256k1_group();
            BN_CTX* tmp_ctx = BN_CTX_new();
            BIGNUM* n = BN_new();
            EC_GROUP_get_order(group, n, tmp_ctx);
            k = BN_new();
            BN_mod_sub(k, n, k_orig, n, tmp_ctx);
            BN_free(n);
            BN_CTX_free(tmp_ctx);
        } else {
            k = k_orig;
        }

        uint8_t e_input[96];
        memcpy(e_input, Rx.data(), 32);
        memcpy(e_input + 32, public_key_.data(), 32);
        memcpy(e_input + 64, msg_hash.data(), 32);
        auto e_hash = sha256(e_input, 96);

        BN_CTX* ctx = BN_CTX_new();
        if (!ctx) {
            BN_free(k);
            throw std::runtime_error("BN_CTX allocation failed");
        }

        BIGNUM* n = BN_bin2bn(SECP256K1_N, 32, nullptr);
        BIGNUM* e = BN_bin2bn(e_hash.data(), 32, nullptr);
        BIGNUM* d = BN_bin2bn(secret_key_.data(), 32, nullptr);

        if (!n || !e || !d) {
            if (ctx) BN_CTX_free(ctx);
            BN_free(k);
            BN_free(n); BN_free(e); BN_free(d);
            throw std::runtime_error("BN allocation failed");
        }

        BN_mod(e, e, n, ctx);

        BIGNUM* e_d = BN_new();
        if (!e_d) {
            if (ctx) BN_CTX_free(ctx);
            BN_free(k);
            BN_free(n); BN_free(e); BN_free(d);
            throw std::runtime_error("BN allocation failed for e_d");
        }
        BN_mod_mul(e_d, e, d, n, ctx);

        BIGNUM* s = BN_new();
        if (!s) {
            if (ctx) BN_CTX_free(ctx);
            BN_free(k);
            BN_free(n); BN_free(e); BN_free(d); BN_free(e_d);
            throw std::runtime_error("BN allocation failed for s");
        }
        BN_mod_add(s, k, e_d, n, ctx);

        std::array<uint8_t, 64> sig{};
        memcpy(sig.data(), Rx.data(), 32);
        bn_to_bytes(s, sig.data() + 32, 32);

        if (ctx) BN_CTX_free(ctx);
        BN_free(k);
        BN_free(n); BN_free(e); BN_free(d); BN_free(e_d); BN_free(s);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < 64; i++) {
            oss << std::setw(2) << (int)sig[i];
        }
        return oss.str();
    }

    std::string Bip340Signer::signature_to_hex(const std::array<uint8_t, 64>& signature) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < 64; i++) {
            oss << std::setw(2) << (int)signature[i];
        }
        return oss.str();
    }

    std::string Bip340Signer::public_key_to_hex(const std::array<uint8_t, 32>& public_key) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < 32; i++) {
            oss << std::setw(2) << (int)public_key[i];
        }
        return oss.str();
    }

    std::array<uint8_t, 32> Bip340Signer::derive_pubkey(const uint8_t* private_key) {
        return derive_pubkey_internal(private_key);
    }

    // BIP-340 VERIFY
    bool Bip340Signer::verify_signature(const std::vector<uint8_t>& message,
                                       const std::string& signature_hex,
                                       const std::string& public_key_hex) {
        std::string sig_hex = signature_hex;
        if (sig_hex.size() >= 2 && sig_hex.substr(0, 2) == "0x") {
            sig_hex = sig_hex.substr(2);
        }
        if (sig_hex.size() != 128) return false;

        std::array<uint8_t, 32> Rx_bytes{};
        std::array<uint8_t, 32> s_bytes{};
        if (!hex_to_bytes(sig_hex.substr(0, 64), Rx_bytes.data(), 32)) return false;
        if (!hex_to_bytes(sig_hex.substr(64, 64), s_bytes.data(), 32)) return false;

        std::string pk_hex = public_key_hex;
        if (pk_hex.size() >= 2 && pk_hex.substr(0, 2) == "0x") {
            pk_hex = pk_hex.substr(2);
        }
        if (pk_hex.size() != 64) return false;
        std::array<uint8_t, 32> Px_bytes{};
        if (!hex_to_bytes(pk_hex, Px_bytes.data(), 32)) return false;

        // Midnight uses BIP-340 with custom domain separator for transaction signing
        auto msg_hash = tagged_hash(message.data(), message.size(), "Midnight/transaction");

        uint8_t e_input[96];
        memcpy(e_input, Rx_bytes.data(), 32);
        memcpy(e_input + 32, Px_bytes.data(), 32);
        memcpy(e_input + 64, msg_hash.data(), 32);
        auto e_hash = sha256(e_input, 96);

        EC_GROUP* group = get_secp256k1_group();
        if (!group) return false;

        BN_CTX* ctx = BN_CTX_new();
        if (!ctx) return false;

        const EC_POINT* gen = EC_GROUP_get0_generator(group);
        if (!gen) {
            BN_CTX_free(ctx);
            return false;
        }

        EC_POINT* G = EC_POINT_new(group);
        if (!G || !EC_POINT_copy(G, gen)) {
            if (G) EC_POINT_free(G);
            BN_CTX_free(ctx);
            return false;
        }

        BIGNUM* n = BN_bin2bn(SECP256K1_N, 32, nullptr);
        BIGNUM* e = BN_bin2bn(e_hash.data(), 32, nullptr);
        BIGNUM* s = BN_bin2bn(s_bytes.data(), 32, nullptr);
        BIGNUM* Rx = BN_bin2bn(Rx_bytes.data(), 32, nullptr);
        BIGNUM* Px = BN_bin2bn(Px_bytes.data(), 32, nullptr);
        if (!n || !e || !s || !Rx || !Px) {
            BN_free(n); BN_free(e); BN_free(s); BN_free(Rx); BN_free(Px);
            EC_POINT_free(G); BN_CTX_free(ctx);
            return false;
        }

        BN_mod(e, e, n, ctx);

        bool result = false;
        for (int parity_P = 0; parity_P <= 1 && !result; parity_P++) {
            EC_POINT* P = EC_POINT_new(group);
            if (!P || !EC_POINT_set_compressed_coordinates(group, P, Px, parity_P, ctx)) {
                if (P) EC_POINT_free(P);
                continue;
            }

            EC_POINT* sG = EC_POINT_new(group);
            EC_POINT* eP = EC_POINT_new(group);
            EC_POINT* R = EC_POINT_new(group);

            if (!sG || !eP || !R) {
                if (sG) EC_POINT_free(sG);
                if (eP) EC_POINT_free(eP);
                if (R) EC_POINT_free(R);
                EC_POINT_free(P);
                continue;
            }

            EC_POINT_mul(group, sG, nullptr, G, s, ctx);
            EC_POINT_mul(group, eP, nullptr, P, e, ctx);
            EC_POINT_invert(group, eP, ctx);
            EC_POINT_add(group, R, sG, eP, ctx);

            BIGNUM* R_x = BN_new();
            BIGNUM* R_y = BN_new();
            if (R_x && R_y) {
                EC_POINT_get_affine_coordinates(group, R, R_x, R_y, ctx);
                if (BN_cmp(R_x, Rx) == 0) {
                    result = true;
                }
                BN_free(R_x); BN_free(R_y);
            }

            EC_POINT_free(R); EC_POINT_free(eP); EC_POINT_free(sG); EC_POINT_free(P);
        }

        BN_free(n); BN_free(e); BN_free(s); BN_free(Rx); BN_free(Px);
        EC_POINT_free(G); BN_CTX_free(ctx);
        return result;
    }

} // namespace midnight::crypto
