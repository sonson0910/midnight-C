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
#include <memory>

namespace midnight::crypto
{
    namespace
    {
        constexpr int SECP256K1_NID = NID_secp256k1;

        using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
        using BnCtxPtr = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;
        using EcPointPtr = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>;
        using EcdsaSigPtr = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;

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
            if (nbytes < 0 || static_cast<size_t>(nbytes) > len) {
                throw std::runtime_error("BIGNUM does not fit in destination buffer");
            }
            std::vector<uint8_t> tmp(nbytes);
            if (nbytes > 0) {
                BN_bn2bin(bn, tmp.data());
            }
            std::memset(out, 0, len - nbytes);
            if (nbytes > 0) {
                std::memcpy(out + len - nbytes, tmp.data(), nbytes);
            }
        }

        BIGNUM* bytes_to_bn(const uint8_t* data, size_t len) {
            return BN_bin2bn(data, static_cast<int>(len), nullptr);
        }

        BnPtr make_bn() {
            return BnPtr(BN_new(), BN_free);
        }

        BnPtr make_bn_from_bytes(const uint8_t* data, size_t len) {
            return BnPtr(bytes_to_bn(data, len), BN_free);
        }

        BnPtr curve_order(const EC_GROUP* group, BN_CTX* ctx) {
            BnPtr n = make_bn();
            if (!n || EC_GROUP_get_order(group, n.get(), ctx) != 1) {
                throw std::runtime_error("Failed to read secp256k1 curve order");
            }
            return n;
        }

        bool valid_scalar(const BIGNUM* value, const BIGNUM* order) {
            return value && !BN_is_zero(value) && !BN_is_negative(value) &&
                   BN_cmp(value, order) < 0;
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
            const EC_GROUP* group = get_secp256k1_group();
            if (!group) {
                throw std::runtime_error("secp256k1 group unavailable");
            }

            BnCtxPtr ctx(BN_CTX_new(), BN_CTX_free);
            if (!ctx) {
                throw std::runtime_error("BN_CTX allocation failed");
            }

            BnPtr n = curve_order(group, ctx.get());
            BnPtr priv_bn = make_bn_from_bytes(private_key.data(), private_key.size());
            if (!valid_scalar(priv_bn.get(), n.get())) {
                throw std::runtime_error("Invalid ECDSA private key scalar");
            }

            EcPointPtr point(EC_POINT_new(group), EC_POINT_free);
            if (!point ||
                EC_POINT_mul(group, point.get(), priv_bn.get(), nullptr, nullptr, ctx.get()) != 1 ||
                EC_POINT_is_at_infinity(group, point.get()) == 1) {
                throw std::runtime_error("ECDSA public key derivation failed");
            }

            BnPtr x = make_bn();
            BnPtr y = make_bn();
            if (!x || !y ||
                EC_POINT_get_affine_coordinates(group, point.get(), x.get(), y.get(), ctx.get()) != 1) {
                throw std::runtime_error("Failed to read ECDSA public key point");
            }

            std::array<uint8_t, 32> x_bytes{};
            std::array<uint8_t, 32> y_bytes{};
            bn_to_bytes(x.get(), x_bytes.data(), 32);
            bn_to_bytes(y.get(), y_bytes.data(), 32);

            EcdsaSigner::PublicKey result{};
            result[0] = (y_bytes[31] & 1) ? 0x03 : 0x02;
            std::memcpy(result.data() + 1, x_bytes.data(), 32);

            return result;
        }

        EcdsaSigner::Signature sign_hash_manual(
            const std::array<uint8_t, 32>& hash,
            const EcdsaSigner::PrivateKey& private_key,
            const std::array<uint8_t, 32>& nonce) {

            const EC_GROUP* group = get_secp256k1_group();
            if (!group) {
                throw std::runtime_error("secp256k1 group unavailable");
            }

            BnCtxPtr ctx(BN_CTX_new(), BN_CTX_free);
            if (!ctx) {
                throw std::runtime_error("BN_CTX allocation failed");
            }

            BnPtr n = curve_order(group, ctx.get());
            BnPtr d = make_bn_from_bytes(private_key.data(), private_key.size());
            BnPtr e = make_bn_from_bytes(hash.data(), hash.size());
            BnPtr k = make_bn_from_bytes(nonce.data(), nonce.size());
            if (!d || !e || !k) {
                throw std::runtime_error("BIGNUM allocation failed");
            }

            if (!valid_scalar(d.get(), n.get())) {
                throw std::runtime_error("Invalid ECDSA private key scalar");
            }
            if (BN_mod(k.get(), k.get(), n.get(), ctx.get()) != 1 ||
                !valid_scalar(k.get(), n.get())) {
                throw std::runtime_error("Invalid ECDSA nonce scalar");
            }
            if (BN_mod(e.get(), e.get(), n.get(), ctx.get()) != 1) {
                throw std::runtime_error("Failed to reduce ECDSA message hash");
            }

            EcPointPtr r_point(EC_POINT_new(group), EC_POINT_free);
            if (!r_point ||
                EC_POINT_mul(group, r_point.get(), k.get(), nullptr, nullptr, ctx.get()) != 1 ||
                EC_POINT_is_at_infinity(group, r_point.get()) == 1) {
                throw std::runtime_error("Failed to compute ECDSA nonce point");
            }

            BnPtr x = make_bn();
            BnPtr y = make_bn();
            if (!x || !y ||
                EC_POINT_get_affine_coordinates(group, r_point.get(), x.get(), y.get(), ctx.get()) != 1) {
                throw std::runtime_error("Failed to read ECDSA nonce point");
            }

            BnPtr r = make_bn();
            if (!r || BN_nnmod(r.get(), x.get(), n.get(), ctx.get()) != 1 ||
                !valid_scalar(r.get(), n.get())) {
                throw std::runtime_error("Invalid ECDSA r value");
            }

            BnPtr kinv(BN_mod_inverse(nullptr, k.get(), n.get(), ctx.get()), BN_free);
            BnPtr rd = make_bn();
            BnPtr sum = make_bn();
            BnPtr s = make_bn();
            if (!kinv || !rd || !sum || !s ||
                BN_mod_mul(rd.get(), r.get(), d.get(), n.get(), ctx.get()) != 1 ||
                BN_mod_add(sum.get(), e.get(), rd.get(), n.get(), ctx.get()) != 1 ||
                BN_mod_mul(s.get(), kinv.get(), sum.get(), n.get(), ctx.get()) != 1 ||
                !valid_scalar(s.get(), n.get())) {
                throw std::runtime_error("Failed to compute ECDSA s value");
            }

            BnPtr half_n(BN_dup(n.get()), BN_free);
            if (!half_n || BN_rshift1(half_n.get(), half_n.get()) != 1) {
                throw std::runtime_error("Failed to compute ECDSA low-S bound");
            }
            if (BN_cmp(s.get(), half_n.get()) > 0 &&
                BN_sub(s.get(), n.get(), s.get()) != 1) {
                throw std::runtime_error("Failed to normalize ECDSA low-S value");
            }

            EcdsaSigner::Signature signature{};
            bn_to_bytes(r.get(), signature.data(), 32);
            bn_to_bytes(s.get(), signature.data() + 32, 32);
            return signature;
        }

        std::vector<uint8_t> raw_signature_to_der(const EcdsaSigner::Signature& signature) {
            BnPtr r = make_bn_from_bytes(signature.data(), 32);
            BnPtr s = make_bn_from_bytes(signature.data() + 32, 32);
            EcdsaSigPtr sig(ECDSA_SIG_new(), ECDSA_SIG_free);
            if (!r || !s || !sig) {
                throw std::runtime_error("Failed to build DER ECDSA signature");
            }

            BIGNUM* raw_r = r.release();
            BIGNUM* raw_s = s.release();
            if (ECDSA_SIG_set0(sig.get(), raw_r, raw_s) != 1) {
                BN_free(raw_r);
                BN_free(raw_s);
                throw std::runtime_error("Failed to build DER ECDSA signature");
            }

            const int der_len = i2d_ECDSA_SIG(sig.get(), nullptr);
            if (der_len <= 0) {
                throw std::runtime_error("Failed to size DER ECDSA signature");
            }

            std::vector<uint8_t> der(static_cast<size_t>(der_len));
            uint8_t* out = der.data();
            if (i2d_ECDSA_SIG(sig.get(), &out) != der_len) {
                throw std::runtime_error("Failed to encode DER ECDSA signature");
            }
            return der;
        }

        bool verify_hash_manual(
            const std::array<uint8_t, 32>& hash,
            const EcdsaSigner::Signature& signature,
            const EcdsaSigner::PublicKey& pub_key) {

            const EC_GROUP* group = get_secp256k1_group();
            if (!group || pub_key.size() != EcdsaSigner::PUBLIC_KEY_SIZE) {
                return false;
            }

            BnCtxPtr ctx(BN_CTX_new(), BN_CTX_free);
            if (!ctx) {
                return false;
            }

            BnPtr n(nullptr, BN_free);
            try {
                n = curve_order(group, ctx.get());
            } catch (...) {
                return false;
            }

            BnPtr r = make_bn_from_bytes(signature.data(), 32);
            BnPtr s = make_bn_from_bytes(signature.data() + 32, 32);
            BnPtr e = make_bn_from_bytes(hash.data(), hash.size());
            if (!r || !s || !e ||
                !valid_scalar(r.get(), n.get()) ||
                !valid_scalar(s.get(), n.get()) ||
                BN_mod(e.get(), e.get(), n.get(), ctx.get()) != 1) {
                return false;
            }

            BnPtr x = make_bn_from_bytes(pub_key.data() + 1, 32);
            const int y_parity = (pub_key[0] == 0x03) ? 1 : 0;
            if (!x || (pub_key[0] != 0x02 && pub_key[0] != 0x03)) {
                return false;
            }

            EcPointPtr q(EC_POINT_new(group), EC_POINT_free);
            if (!q ||
                EC_POINT_set_compressed_coordinates(group, q.get(), x.get(), y_parity, ctx.get()) != 1 ||
                EC_POINT_is_on_curve(group, q.get(), ctx.get()) != 1) {
                return false;
            }

            BnPtr w(BN_mod_inverse(nullptr, s.get(), n.get(), ctx.get()), BN_free);
            BnPtr u1 = make_bn();
            BnPtr u2 = make_bn();
            if (!w || !u1 || !u2 ||
                BN_mod_mul(u1.get(), e.get(), w.get(), n.get(), ctx.get()) != 1 ||
                BN_mod_mul(u2.get(), r.get(), w.get(), n.get(), ctx.get()) != 1) {
                return false;
            }

            EcPointPtr computed(EC_POINT_new(group), EC_POINT_free);
            if (!computed ||
                EC_POINT_mul(group, computed.get(), u1.get(), q.get(), u2.get(), ctx.get()) != 1 ||
                EC_POINT_is_at_infinity(group, computed.get()) == 1) {
                return false;
            }

            BnPtr rx = make_bn();
            BnPtr ry = make_bn();
            BnPtr v = make_bn();
            if (!rx || !ry || !v ||
                EC_POINT_get_affine_coordinates(group, computed.get(), rx.get(), ry.get(), ctx.get()) != 1 ||
                BN_nnmod(v.get(), rx.get(), n.get(), ctx.get()) != 1) {
                return false;
            }

            return BN_cmp(v.get(), r.get()) == 0;
        }
    }

    EcdsaSigner::EcdsaSigner() : has_key_(false) {}

    std::pair<EcdsaSigner::PrivateKey, EcdsaSigner::PublicKey> EcdsaSigner::generate_keypair() {
        const EC_GROUP* group = get_secp256k1_group();
        if (!group) {
            throw std::runtime_error("secp256k1 group unavailable");
        }
        BnCtxPtr ctx(BN_CTX_new(), BN_CTX_free);
        if (!ctx) {
            throw std::runtime_error("BN_CTX allocation failed");
        }
        BnPtr n = curve_order(group, ctx.get());

        for (int attempt = 0; attempt < 128; ++attempt) {
            PrivateKey private_key{};
            if (RAND_bytes(private_key.data(), static_cast<int>(private_key.size())) != 1) {
                throw std::runtime_error("Failed to generate ECDSA private key bytes");
            }
            BnPtr candidate = make_bn_from_bytes(private_key.data(), private_key.size());
            if (!valid_scalar(candidate.get(), n.get())) {
                continue;
            }
            auto public_key = derive_public_key_impl(private_key);
            return {private_key, public_key};
        }

        throw std::runtime_error("Failed to generate valid ECDSA private key scalar");
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
        return raw_signature_to_der(sign_raw(message));
    }

    EcdsaSigner::Signature EcdsaSigner::sign_raw(const std::vector<uint8_t>& message) const {
        return sign_deterministic(message);
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
        if (!has_key_) {
            throw std::runtime_error("ECDSA signer has no private key");
        }
        auto hash = sha256_hash(message);
        auto nonce = nonce_rfc6979_impl(message, private_key_, hash);
        return sign_hash_manual(hash, private_key_, nonce);
    }

    /**
     * Verify a raw signature (r||s format)
     * 
     * Properly reconstructs the public key from compressed format and
     * verifies with explicit secp256k1 group arithmetic.
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
        
        auto hash = sha256_hash(message);
        return verify_hash_manual(hash, signature, pub_key);
    }

    bool EcdsaSigner::verify_der(const std::vector<uint8_t>& message,
                                const std::vector<uint8_t>& der_sig,
                                const PublicKey& pub_key) {
        const uint8_t* ptr = der_sig.data();
        EcdsaSigPtr sig(d2i_ECDSA_SIG(nullptr, &ptr, static_cast<long>(der_sig.size())),
                         ECDSA_SIG_free);
        if (!sig) return false;
        if (ptr != der_sig.data() + der_sig.size()) return false;

        const BIGNUM* r = nullptr;
        const BIGNUM* s = nullptr;
        ECDSA_SIG_get0(sig.get(), &r, &s);
        if (!r || !s) return false;
        if (BN_num_bytes(r) > 32 || BN_num_bytes(s) > 32) return false;

        Signature raw{};
        bn_to_bytes(r, raw.data(), 32);
        bn_to_bytes(s, raw.data() + 32, 32);
        return verify_raw(message, raw, pub_key);
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
