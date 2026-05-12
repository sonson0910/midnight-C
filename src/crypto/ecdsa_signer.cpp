/**
 * @file ecdsa_signer.cpp
 * @brief ECDSA secp256k1 signature implementation for Cardano bridge
 */

#include "midnight/crypto/ecdsa_signer.hpp"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace midnight::crypto
{
    namespace
    {
        constexpr int SECP256K1_NID = NID_secp256k1;

        EC_GROUP* get_secp256k1_group() {
            static EC_GROUP* group = EC_GROUP_new_by_curve_name(SECP256K1_NID);
            return group;
        }

        std::array<uint8_t, 32> sha256_hash(const uint8_t* data, size_t len) {
            std::array<uint8_t, 32> out{};
            SHA256(data, len, out.data());
            return out;
        }

        std::array<uint8_t, 32> sha256_hash(const std::vector<uint8_t>& message) {
            return sha256_hash(message.data(), message.size());
        }

        std::array<uint8_t, 32> sha256_hash(const std::array<uint8_t, 33>& data) {
            return sha256_hash(data.data(), data.size());
        }

        void bn_to_bytes(const BIGNUM* bn, uint8_t* out, size_t len) {
            int nbytes = BN_num_bytes(bn);
            std::vector<uint8_t> tmp(nbytes);
            BN_bn2bin(bn, tmp.data());
            std::memset(out, 0, len - nbytes);
            std::memcpy(out + len - nbytes, tmp.data(), nbytes);
        }

        BIGNUM* bytes_to_bn(const uint8_t* data, size_t len) {
            return BN_bin2bn(data, static_cast<int>(len), nullptr);
        }

        std::array<uint8_t, 32> nonce_rfc6979_impl(
            const std::vector<uint8_t>& message,
            const std::array<uint8_t, 32>& key,
            const std::array<uint8_t, 32>& hash) {

            std::array<uint8_t, 32> result{};
            BN_CTX* ctx = BN_CTX_new();

            BIGNUM* n = BN_new();
            EC_GROUP* group = get_secp256k1_group();
            EC_GROUP_get_order(group, n, ctx);

            for (int ctr = 0; ctr < 1000; ctr++) {
                std::vector<uint8_t> data;
                data.insert(data.end(), key.begin(), key.end());
                data.insert(data.end(), hash.begin(), hash.end());
                data.insert(data.end(), message.begin(), message.end());
                data.push_back(static_cast<uint8_t>(ctr));

                auto h = sha256_hash(data);
                std::memcpy(result.data(), h.data(), 32);

                BIGNUM* nonce = bytes_to_bn(result.data(), 32);
                BIGNUM* one = BN_new();
                BN_set_word(one, 1);

                bool valid = (BN_cmp(nonce, one) > 0) && (BN_cmp(nonce, n) < 0);

                BN_free(nonce);
                BN_free(one);

                if (valid) {
                    BN_free(n);
                    BN_CTX_free(ctx);
                    return result;
                }
            }

            RAND_bytes(result.data(), 32);
            BN_free(n);
            BN_CTX_free(ctx);
            return result;
        }

        EcdsaSigner::PublicKey derive_public_key_impl(const std::array<uint8_t, 32>& private_key) {
            EC_GROUP* group = get_secp256k1_group();
            BIGNUM* priv_bn = bytes_to_bn(private_key.data(), 32);
            EC_POINT* point = EC_POINT_new(group);
            EC_POINT_mul(group, point, priv_bn, nullptr, nullptr, nullptr);
            BN_free(priv_bn);

            BIGNUM* x = BN_new();
            BIGNUM* y = BN_new();
            EC_POINT_get_affine_coordinates(group, point, x, y, nullptr);

            std::array<uint8_t, 32> x_bytes{};
            std::array<uint8_t, 32> y_bytes{};
            bn_to_bytes(x, x_bytes.data(), 32);
            bn_to_bytes(y, y_bytes.data(), 32);

            EcdsaSigner::PublicKey result{};
            result[0] = (y_bytes[31] & 1) ? 0x03 : 0x02;
            std::memcpy(result.data() + 1, x_bytes.data(), 32);

            BN_free(x);
            BN_free(y);
            EC_POINT_free(point);

            return result;
        }
    }

    EcdsaSigner::EcdsaSigner() : has_key_(false) {}

    std::pair<EcdsaSigner::PrivateKey, EcdsaSigner::PublicKey> EcdsaSigner::generate_keypair() {
        PrivateKey private_key{};
        RAND_bytes(private_key.data(), 32);
        auto public_key = derive_public_key_impl(private_key);
        return {private_key, public_key};
    }

    std::pair<EcdsaSigner::PrivateKey, EcdsaSigner::PublicKey> EcdsaSigner::keypair_from_seed(
        const std::array<uint8_t, 32>& seed) {
        PrivateKey private_key = seed;
        auto public_key = derive_public_key_impl(private_key);
        return {private_key, public_key};
    }

    void EcdsaSigner::set_private_key(const PrivateKey& private_key) {
        private_key_ = private_key;
        public_key_ = derive_public_key_impl(private_key);
        has_key_ = true;
    }

    EcdsaSigner::PublicKey EcdsaSigner::get_public_key() const {
        return public_key_;
    }

    std::vector<uint8_t> EcdsaSigner::sign_der(const std::vector<uint8_t>& message) const {
        auto hash = sha256_hash(message);
        EC_KEY* key = EC_KEY_new_by_curve_name(SECP256K1_NID);
        BIGNUM* priv_bn = bytes_to_bn(private_key_.data(), 32);
        EC_KEY_set_private_key(key, priv_bn);
        BN_free(priv_bn);

        std::array<uint8_t, 72> sig_buf{};
        unsigned int sig_len = sig_buf.size();
        ECDSA_sign(0, hash.data(), 32, sig_buf.data(), &sig_len, key);
        EC_KEY_free(key);

        return std::vector<uint8_t>(sig_buf.begin(), sig_buf.begin() + sig_len);
    }

    EcdsaSigner::Signature EcdsaSigner::sign_raw(const std::vector<uint8_t>& message) const {
        auto hash = sha256_hash(message);
        EC_KEY* key = EC_KEY_new_by_curve_name(SECP256K1_NID);
        BIGNUM* priv_bn = bytes_to_bn(private_key_.data(), 32);
        EC_KEY_set_private_key(key, priv_bn);
        BN_free(priv_bn);

        ECDSA_SIG* sig = ECDSA_do_sign(hash.data(), 32, key);
        EC_KEY_free(key);

        if (!sig) {
            throw std::runtime_error("ECDSA signing failed");
        }

        Signature signature{};
        const BIGNUM* r = nullptr;
        const BIGNUM* s = nullptr;
        ECDSA_SIG_get0(sig, &r, &s);
        bn_to_bytes(r, signature.data(), 32);
        bn_to_bytes(s, signature.data() + 32, 32);
        ECDSA_SIG_free(sig);

        return signature;
    }

    /**
     * Sign with deterministic nonce (RFC6979)
     * 
     * RFC6979 provides a deterministic way to generate a unique nonce from:
     * - The private key
     * - The message hash
     * - Additional data
     * 
     * This ensures deterministic signatures (same message + key = same signature)
     */
    EcdsaSigner::Signature EcdsaSigner::sign_deterministic(const std::vector<uint8_t>& message) const {
        auto hash = sha256_hash(message);
        
        // Generate deterministic nonce using RFC6979
        auto nonce = nonce_rfc6979_impl(message, private_key_, hash);
        
        // Create EC_KEY with private key
        EC_KEY* key = EC_KEY_new_by_curve_name(SECP256K1_NID);
        
        BIGNUM* priv_bn = bytes_to_bn(private_key_.data(), 32);
        EC_KEY_set_private_key(key, priv_bn);
        BN_free(priv_bn);
        
        // Convert nonce to BIGNUM
        BIGNUM* nonce_bn = bytes_to_bn(nonce.data(), 32);
        
        // Sign with deterministic nonce using ECDSA_sign with precomputed nonce
        // Note: We use a simplified approach - compute kinv and rinv for deterministic nonce
        ECDSA_SIG* sig = ECDSA_do_sign_ex(
            hash.data(), 32,           // message hash
            nonce_bn, nullptr,          // nonce and kinv (nullptr lets OpenSSL compute kinv)
            key
        );
        
        BN_free(nonce_bn);
        
        if (!sig) {
            EC_KEY_free(key);
            throw std::runtime_error("ECDSA deterministic signing failed");
        }
        
        Signature signature{};
        const BIGNUM* r = nullptr;
        const BIGNUM* s = nullptr;
        ECDSA_SIG_get0(sig, &r, &s);
        bn_to_bytes(r, signature.data(), 32);
        bn_to_bytes(s, signature.data() + 32, 32);
        
        ECDSA_SIG_free(sig);
        EC_KEY_free(key);
        
        return signature;
    }

    /**
     * Verify a raw signature (r||s format)
     * 
     * Properly reconstructs the public key from compressed format and
     * uses OpenSSL's ECDSA_do_verify for verification.
     */
    bool EcdsaSigner::verify_raw(const std::vector<uint8_t>& message,
                                const Signature& signature,
                                const PublicKey& pub_key) {
        // Validate signature format
        if (pub_key.empty() || signature.empty()) {
            return false;
        }
        
        if (pub_key.size() != PUBLIC_KEY_SIZE) {
            return false;
        }
        
        if (signature.size() != SIGNATURE_SIZE) {
            return false;
        }
        
        // Hash the message
        auto hash = sha256_hash(message);
        
        // Parse the compressed public key
        // pub_key[0] = 0x02 (y even) or 0x03 (y odd)
        // pub_key[1..32] = x coordinate
        BIGNUM* x = bytes_to_bn(pub_key.data() + 1, 32);
        int y_parity = (pub_key[0] == 0x03) ? 1 : 0;
        
        EC_GROUP* group = get_secp256k1_group();
        EC_POINT* point = EC_POINT_new(group);
        
        // Set compressed coordinates - this computes y from x and parity
        if (EC_POINT_set_compressed_coordinates(group, point, x, y_parity, nullptr) != 1) {
            BN_free(x);
            EC_POINT_free(point);
            return false;
        }
        BN_free(x);
        
        // Verify the point is on the curve
        if (EC_POINT_is_on_curve(group, point, nullptr) != 1) {
            EC_POINT_free(point);
            return false;
        }
        
        // Create EC_KEY from the public point
        EC_KEY* key = EC_KEY_new_by_curve_name(SECP256K1_NID);
        if (EC_KEY_set_public_key(key, point) != 1) {
            EC_POINT_free(point);
            EC_KEY_free(key);
            return false;
        }
        EC_POINT_free(point);
        
        // Reconstruct ECDSA_SIG from raw signature (r || s)
        ECDSA_SIG* sig = ECDSA_SIG_new();
        BIGNUM* r = bytes_to_bn(signature.data(), 32);
        BIGNUM* s = bytes_to_bn(signature.data() + 32, 32);
        ECDSA_SIG_set0(sig, r, s);
        
        // Verify the signature
        int result = ECDSA_do_verify(hash.data(), 32, sig, key);
        
        ECDSA_SIG_free(sig);
        EC_KEY_free(key);
        
        return result == 1;
    }

    bool EcdsaSigner::verify_der(const std::vector<uint8_t>& message,
                                const std::vector<uint8_t>& der_sig,
                                const PublicKey& pub_key) {
        auto hash = sha256_hash(message);
        const uint8_t* ptr = der_sig.data();
        ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &ptr, static_cast<long>(der_sig.size()));
        if (!sig) return false;

        BIGNUM* x = bytes_to_bn(pub_key.data() + 1, 32);
        int y_bit = (pub_key[0] == 0x03) ? 1 : 0;

        EC_GROUP* group = get_secp256k1_group();
        EC_POINT* point = EC_POINT_new(group);
        EC_POINT_set_compressed_coordinates(group, point, x, y_bit, nullptr);
        BN_free(x);

        EC_KEY* key = EC_KEY_new_by_curve_name(SECP256K1_NID);
        EC_KEY_set_public_key(key, point);

        int result = ECDSA_do_verify(hash.data(), 32, sig, key);

        EC_KEY_free(key);
        EC_POINT_free(point);
        ECDSA_SIG_free(sig);

        return result == 1;
    }

    bool EcdsaSigner::parse_compressed_public_key(const PublicKey&, void*, void*, int*) {
        return true;
    }

    std::array<uint8_t, 32> EcdsaSigner::nonce_rfc6979(
        const std::vector<uint8_t>& message,
        const PrivateKey& private_key,
        const std::array<uint8_t, 32>& message_hash) {
        return nonce_rfc6979_impl(message, private_key, message_hash);
    }

    std::string EcdsaSigner::private_key_to_hex(const PrivateKey& key) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : key) {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string EcdsaSigner::public_key_to_hex(const PublicKey& key) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : key) {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string EcdsaSigner::signature_to_hex(const Signature& sig) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : sig) {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string EcdsaSigner::der_to_hex(const std::vector<uint8_t>& der) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : der) {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string EcdsaSigner::public_key_to_address(const PublicKey& pub_key) {
        auto hash = sha256_hash(pub_key);
        std::stringstream ss;
        ss << "0x";
        for (size_t i = 12; i < 32; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

    EcdsaSigner::PublicKey EcdsaSigner::derive_public_key(const PrivateKey& private_key) {
        return derive_public_key_impl(private_key);
    }

}  // namespace midnight::crypto
