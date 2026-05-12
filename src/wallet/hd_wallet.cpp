#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/crypto/ed25519_signer.hpp"
#include <openssl/hmac.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sodium.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>

// BIP39 wordlist must be included before midnight::wallet namespace
#include "midnight/wallet/bip39_wordlist.hpp"

namespace midnight::wallet
{
    // BIP32/secp256k1 constants
    static const unsigned char BITCOIN_SEED[] = "Bitcoin seed";
    static const uint32_t HARDENED = 0x80000000UL;

    // secp256k1 curve order (32 bytes, big-endian hex)
    // 0xFFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF BAAEDCE6 AF48A03B BFD25E8C D0364141
    static const uint8_t SECP256K1_ORDER[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
        0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
    };

    // ─── RAII wrapper for BN_CTX ───────────────────────────────────

    struct BnCtx
    {
        BN_CTX *ctx;
        BnCtx() : ctx(BN_CTX_new()) {
            if (!ctx) throw std::runtime_error("BN_CTX_new failed");
        }
        ~BnCtx() { if (ctx) BN_CTX_free(ctx); }
        BN_CTX *get() const { return ctx; }
        BnCtx(const BnCtx &) = delete;
        BnCtx &operator=(const BnCtx &) = delete;
    };

    // ─── RAII wrapper for BIGNUM ───────────────────────────────────

    struct BnNum
    {
        BIGNUM *bn;
        explicit BnNum(BIGNUM *ptr = nullptr) : bn(ptr) {}
        ~BnNum() { if (bn) BN_free(bn); }
        BIGNUM *get() const { return bn; }
        operator BIGNUM*() const { return bn; }
        BnNum(const BnNum &) = delete;
        BnNum &operator=(const BnNum &) = delete;
        // Allow move
        BnNum(BnNum &&other) noexcept : bn(other.bn) { other.bn = nullptr; }
        BnNum &operator=(BnNum &&other) noexcept {
            if (this != &other) {
                if (bn) BN_free(bn);
                bn = other.bn;
                other.bn = nullptr;
            }
            return *this;
        }
    };

    // ─── RAII wrapper for EC_POINT ──────────────────────────────────
    struct EcPoint {
        EC_POINT* pt;
        explicit EcPoint(EC_GROUP* g) : pt(EC_POINT_new(g)) {
            if (!pt) throw std::runtime_error("EC_POINT_new failed");
        }
        ~EcPoint() { if (pt) EC_POINT_free(pt); }
        EC_POINT* get() const { return pt; }
        EcPoint(const EcPoint&) = delete;
        EcPoint& operator=(const EcPoint&) = delete;
    };

    // ─── RAII wrapper for EC_GROUP ───────────────────────────────
    struct EcGroup {
        EC_GROUP* g;
        explicit EcGroup(int nid) : g(EC_GROUP_new_by_curve_name(nid)) {
            if (!g) throw std::runtime_error("EC_GROUP_new_by_curve_name failed");
        }
        ~EcGroup() { if (g) EC_GROUP_free(g); }
        EC_GROUP* get() const { return g; }
        EcGroup(const EcGroup&) = delete;
        EcGroup& operator=(const EcGroup&) = delete;
    };

    // ─── KeyPair ────────────────────────────────────────────

    std::vector<uint8_t> KeyPair::sign(const std::vector<uint8_t> &message) const
    {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium init failed");
        }

        if (secret_key.size() != 64) {
            throw std::runtime_error("Ed25519 secret key must be 64 bytes (expanded_sk)");
        }

        // secret_key is the 64-byte Ed25519 expanded secret key:
        // expanded_sk = seed || SHA512(seed)[32:]
        // crypto_sign_seed_keypair produces exactly this format.
        // crypto_sign_detached expects this format directly — no further expansion needed.
        std::array<uint8_t, 64> sig{};
        unsigned long long sig_len = 0;

        if (crypto_sign_detached(sig.data(), &sig_len,
                                 message.data(), message.size(),
                                 secret_key.data()) != 0) {
            throw std::runtime_error("Ed25519 signing failed");
        }
        return std::vector<uint8_t>(sig.begin(), sig.begin() + sig_len);
    }

    bool KeyPair::verify(const std::vector<uint8_t> &message,
                         const std::vector<uint8_t> &signature) const
    {
        if (public_key.size() != 32) return false;
        if (signature.size() != 64) return false;
        if (sodium_init() < 0) return false;

        return crypto_sign_verify_detached(signature.data(),
                                          message.data(), message.size(),
                                          public_key.data()) == 0;
    }

    // ─── BIP39 ─────────────────────────────────────────────

    namespace bip39 {
        static int word_index(const std::string& w) {
            int lo = 0, hi = 2047;
            while (lo <= hi) {
                int mid = (lo + hi) >> 1;
                int cmp = BIP39_WORDLIST[mid].compare(w);
                if (cmp == 0) return mid;
                if (cmp < 0) lo = mid + 1; else hi = mid - 1;
            }
            return -1;
        }

        std::string generate_mnemonic(int wc) {
            if (wc != 12 && wc != 15 && wc != 18 && wc != 21 && wc != 24)
                throw std::runtime_error("word count must be 12/15/18/21/24");

            int ebytes = wc * 4;
            std::vector<uint8_t> ent(ebytes);
            RAND_bytes(ent.data(), ebytes);

            uint8_t h[32];
            SHA256(ent.data(), ebytes, h);

            std::vector<bool> bits;
            bits.reserve(ebytes * 8 + ebytes / 4);
            for (auto b : ent)
                for (int i = 7; i >= 0; --i) bits.push_back((b >> i) & 1);
            for (int i = 0; i < ebytes / 4; ++i)
                bits.push_back(((h[i / 8] >> (7 - (i % 8))) & 1));

            std::string r;
            for (int i = 0; i < wc; ++i) {
                uint16_t idx = 0;
                for (int b = 0; b < 11; ++b) idx = (idx << 1) | (bits[i * 11 + b] ? 1 : 0);
                if (i) r += ' ';
                r += BIP39_WORDLIST[idx];
            }
            return r;
        }

        bool validate_mnemonic(const std::string& m) {
            std::istringstream iss(m);
            std::string w;
            std::vector<int> indices;
            int cnt = 0;
            
            while (iss >> w) {
                int idx = word_index(w);
                if (idx < 0) return false;
                indices.push_back(idx);
                ++cnt;
            }
            
            // Check valid word counts
            if (cnt != 12 && cnt != 15 && cnt != 18 && cnt != 21 && cnt != 24) {
                return false;
            }
            
            // BIP39 checksum validation
            // 1. Convert word indices to entropy bits (11 bits per word)
            // 2. Split into entropy + checksum bits
            // 3. Verify checksum
            
            // Calculate total bits and checksum bits
            int total_bits = cnt * 11;
            int checksum_bits = total_bits / 33;  // 4 bits for 12 words, 6 for 24, etc.
            int entropy_bits = total_bits - checksum_bits;
            int entropy_bytes = entropy_bits / 8;
            
            // Build entropy bytes from indices
            std::vector<uint8_t> entropy(entropy_bytes, 0);
            int bit_pos = 0;
            
            for (int idx : indices) {
                for (int b = 10; b >= 0; --b) {
                    if (bit_pos < entropy_bits) {
                        int byte_idx = bit_pos / 8;
                        int bit_idx = 7 - (bit_pos % 8);
                        entropy[byte_idx] |= ((idx >> b) & 1) << bit_idx;
                    }
                    ++bit_pos;
                }
            }
            
            // Calculate expected checksum (first checksum_bits of SHA256 of entropy)
            uint8_t hash[SHA256_DIGEST_LENGTH];
            SHA256(entropy.data(), static_cast<int>(entropy.size()), hash);
            
            // Compare checksum bits
            bit_pos = 0;
            for (int idx : indices) {
                if (bit_pos >= entropy_bits) {
                    // We're in checksum bits
                    int checksum_bit_pos = bit_pos - entropy_bits;
                    int byte_idx = checksum_bit_pos / 8;
                    int bit_idx = 7 - (checksum_bit_pos % 8);
                    
                    int expected_bit = (hash[byte_idx] >> bit_idx) & 1;
                    int actual_bit = (idx >> (10 - (checksum_bit_pos % 11))) & 1;
                    
                    if (expected_bit != actual_bit) {
                        return false;
                    }
                }
                ++bit_pos;
            }
            
            return true;
        }

        std::vector<uint8_t> mnemonic_to_seed(const std::string& m,
                                              const std::string& p) {
            std::string salt = "mnemonic" + p;
            std::vector<uint8_t> seed(64);
            PKCS5_PBKDF2_HMAC(m.c_str(), (int)m.size(),
                              (const unsigned char*)salt.c_str(), (int)salt.size(),
                              2048, EVP_sha512(), 64, seed.data());
            return seed;
        }
    }

    // ─── BIP32/secp256k1 ─────────────────────────────────

    // Big-endian bytes to BIGNUM
    static BIGNUM* bytes_to_bn(const std::array<uint8_t, 32>& bytes) {
        return BN_bin2bn(bytes.data(), 32, nullptr);
    }

    // BIGNUM to big-endian bytes (32 bytes, padded)
    static void bn_to_bytes32(BIGNUM* bn, uint8_t* out) {
        int len = BN_num_bytes(bn);
        if (len < 32) {
            memset(out, 0, 32 - len);
            BN_bn2bin(bn, out + (32 - len));
        } else {
            BN_bn2bin(bn, out);
        }
    }

    // BLS scalar field modulus (256-bit, little-endian for BN_mod)
    // 0x73eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001
    static const uint8_t BLS_FIELD[] = {
        0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFE,
        0xB4, 0xDA, 0x02, 0x53, 0x5B, 0xA0, 0x8D, 0x9E,
        0x80, 0x9D, 0xD8, 0x33, 0x7D, 0x48, 0x97, 0x29,
        0x53, 0xE7, 0xDE, 0x73, 0x00, 0x00, 0x00, 0x00
    };

    // Reduce a 32-byte scalar modulo the BLS field
    // Matches SDK: DustSecretKey.fromSeed(seed) computes seed % BLS_FIELD
    static std::array<uint8_t, 32> bls_reduce(const std::array<uint8_t, 32>& scalar) {
        BnCtx ctx;
        BnNum n(BN_bin2bn(BLS_FIELD, 32, nullptr));
        BnNum bn_s(BN_bin2bn(scalar.data(), 32, nullptr));
        BnNum r(BN_new());
        if (!n.get() || !bn_s.get() || !r.get())
            throw std::runtime_error("BIGNUM allocation failed in bls_reduce");
        if (BN_mod(r.get(), bn_s.get(), n.get(), ctx.get()) != 1)
            throw std::runtime_error("BN_mod failed in bls_reduce");
        std::array<uint8_t, 32> out{};
        bn_to_bytes32(r.get(), out.data());
        return out;
    }

    // (a + b) mod secp256k1_n using RAII BN_CTX and RAII BIGNUMs
    static std::array<uint8_t, 32> secp_add(const std::array<uint8_t, 32>& a,
                                              const std::array<uint8_t, 32>& b) {
        BnCtx ctx;
        BnNum n(BN_bin2bn(SECP256K1_ORDER, 32, nullptr));
        BnNum bn_a(BN_bin2bn(a.data(), 32, nullptr));
        BnNum bn_b(BN_bin2bn(b.data(), 32, nullptr));
        BnNum r(BN_new());

        if (!n.get() || !bn_a.get() || !bn_b.get() || !r.get())
            throw std::runtime_error("BIGNUM allocation failed");

        if (BN_mod_add(r.get(), bn_a.get(), bn_b.get(), n.get(), ctx.get()) != 1)
            throw std::runtime_error("BN_mod_add failed");

        std::array<uint8_t, 32> out{};
        bn_to_bytes32(r.get(), out.data());

        return out;
    }

    // BIP-32 child key derivation (matching @scure/bip32 exactly)
    // hardened: data = [0x00] || priv(32) || index(4, BE)
    // normal:   data = pubkey(33) || index(4, BE)
    static std::pair<std::array<uint8_t, 32>, std::array<uint8_t, 32>>
    bip32_child(const std::array<uint8_t, 32>& priv_key,
                const std::array<uint8_t, 32>& chain_code,
                uint32_t index,
                bool hardened,
                const std::array<uint8_t, 33>* pub33_opt)
    {
        std::vector<uint8_t> data;
        if (hardened) {
            data.reserve(1 + 32 + 4);
            data.push_back(0x00);
            data.insert(data.end(), priv_key.begin(), priv_key.end());
        } else {
            if (!pub33_opt) throw std::runtime_error("pubkey required for normal derivation");
            data.reserve(33 + 4);
            data.insert(data.end(), pub33_opt->begin(), pub33_opt->end());
        }

        uint8_t idx_bytes[4] = {
            static_cast<uint8_t>((index >> 24) & 0xFF),
            static_cast<uint8_t>((index >> 16) & 0xFF),
            static_cast<uint8_t>((index >> 8) & 0xFF),
            static_cast<uint8_t>(index & 0xFF)
        };
        data.insert(data.end(), idx_bytes, idx_bytes + 4);

        unsigned char out[64];
        HMAC(EVP_sha512(), chain_code.data(), 32, data.data(), data.size(), out, nullptr);

        std::array<uint8_t, 32> tweak{};
        std::array<uint8_t, 32> child_chain{};
        std::memcpy(tweak.data(), out, 32);
        std::memcpy(child_chain.data(), out + 32, 32);

        std::array<uint8_t, 32> child_key = secp_add(priv_key, tweak);

        return {child_key, child_chain};
    }

    // secp256k1 compressed public key (33 bytes) with RAII resource management
    static std::array<uint8_t, 33> secp_pubkey(const std::array<uint8_t, 32>& priv) {
        BnCtx ctx;
        EcGroup g(NID_secp256k1);  // RAII - no manual free needed
        EcPoint pt(g.get());           // RAII - no manual free needed

        BnNum bn_p(BN_bin2bn(priv.data(), 32, nullptr));
        if (!bn_p.get()) throw std::runtime_error("BN_bin2bn failed for private key");

        if (EC_POINT_mul(g.get(), pt.get(), bn_p.get(), nullptr, nullptr, ctx.get()) != 1)
            throw std::runtime_error("EC_POINT_mul failed");

        BnNum bn_x(BN_new());
        BnNum bn_y(BN_new());
        if (!bn_x.get() || !bn_y.get()) throw std::runtime_error("BIGNUM allocation failed");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        if (EC_POINT_get_affine_coordinates_GFp(g.get(), pt.get(), bn_x, bn_y, ctx.get()) != 1)
            throw std::runtime_error("EC_POINT_get_affine_coordinates_GFp failed");
#pragma GCC diagnostic pop

        std::array<uint8_t, 33> pk{};
        pk[0] = BN_is_odd(bn_y.get()) ? 0x03 : 0x02;
        bn_to_bytes32(bn_x.get(), pk.data() + 1);

        // RAII handles cleanup automatically - no manual EC_POINT_free/EC_GROUP_free needed
        return pk;
    }

    // BIP-340 x-only public key (32 bytes)
    static std::array<uint8_t, 32> bip340_pubkey(const std::array<uint8_t, 32>& priv) {
        auto pk = secp_pubkey(priv);
        std::array<uint8_t, 32> xonly{};
        std::memcpy(xonly.data(), pk.data() + 1, 32);
        return xonly;
    }

    // ─── HDWallet ─────────────────────────────────────────

    HDWallet HDWallet::from_mnemonic(const std::string& m, const std::string& p) {
        if (!bip39::validate_mnemonic(m))
            throw std::runtime_error("Invalid BIP39 mnemonic");
        return from_seed(bip39::mnemonic_to_seed(m, p));
    }

    HDWallet HDWallet::from_seed(const std::vector<uint8_t>& seed_64) {
        if (seed_64.size() != 64) throw std::runtime_error("Seed must be 64 bytes");

        HDWallet w;
        w.seed_ = seed_64;

        unsigned char out[64];
        HMAC(EVP_sha512(), BITCOIN_SEED, 12, seed_64.data(), seed_64.size(), out, nullptr);

        w.master_key_.fill(0);
        w.master_chain_.fill(0);
        std::memcpy(w.master_key_.data(), out, 32);
        std::memcpy(w.master_chain_.data(), out + 32, 32);

        return w;
    }

    HDWallet::ChainNode HDWallet::derive_path(uint32_t acc,
                                               Role role,
                                               uint32_t idx) const
    {
        uint32_t role_val = static_cast<uint32_t>(role);

        // Level 1: m/44' (hardened: index = 44 + 0x80000000 = 0x8000002C)
        auto r1 = bip32_child(master_key_, master_chain_, 44 + HARDENED, true, nullptr);

        // Level 2: m/44'/2400' (hardened)
        auto r2 = bip32_child(r1.first, r1.second, 2400 + HARDENED, true, nullptr);

        // Level 3: m/44'/2400'/acc' (hardened)
        auto r3 = bip32_child(r2.first, r2.second, acc + HARDENED, true, nullptr);

        // Level 4: m/44'/2400'/acc'/role (NON-hardened, SLIP-10 style)
        auto pub3 = secp_pubkey(r3.first);
        auto r4 = bip32_child(r3.first, r3.second, role_val, false, &pub3);

        // Level 5: m/44'/2400'/acc'/role/idx (NON-hardened, BIP-44 address chain)
        auto pub4 = secp_pubkey(r4.first);
        auto r5 = bip32_child(r4.first, r4.second, idx, false, &pub4);

        ChainNode node{};
        node.key = r5.first;
        node.chain_code = r5.second;
        return node;
    }

    KeyPair HDWallet::derive(uint32_t acc, Role role, uint32_t idx) const {
        auto node = derive_path(acc, role, idx);

        unsigned char pk[32];
        unsigned char sk[64];
        crypto_sign_seed_keypair(pk, sk, node.key.data());

        KeyPair kp;
        kp.secret_key.assign(sk, sk + 64);
        kp.public_key.assign(pk, pk + 32);
        return kp;
    }

    KeyPair HDWallet::derive_night(uint32_t acc, uint32_t idx) const {
        auto node = derive_path(acc, Role::NightExternal, idx);

        unsigned char pk[32];
        unsigned char sk[64];
        crypto_sign_seed_keypair(pk, sk, node.key.data());
        std::vector<uint8_t> pub_vec(pk, pk + 32);

        KeyPair kp;
        kp.secret_key.assign(sk, sk + 64);
        kp.public_key = pub_vec;

        kp.address = address::encode_unshielded(pub_vec, address::Network::Preview);
        return kp;
    }

    KeyPair HDWallet::derive_night_internal(uint32_t acc, uint32_t idx) const {
        auto node = derive_path(acc, Role::NightInternal, idx);

        unsigned char pk[32];
        unsigned char sk[64];
        crypto_sign_seed_keypair(pk, sk, node.key.data());
        std::vector<uint8_t> pub_vec(pk, pk + 32);

        KeyPair kp;
        kp.secret_key.assign(sk, sk + 64);
        kp.public_key = pub_vec;
        kp.address = address::encode_unshielded(pub_vec, address::Network::Preview);
        return kp;
    }

    KeyPair HDWallet::derive_dust(uint32_t acc, uint32_t idx) const {
        auto node = derive_path(acc, Role::Dust, idx);

        // For Dust keys, Midnight SDK computes: DustSecretKey = raw_scalar % BLS_FIELD
        // The DustPublicKey is NOT an Ed25519 or secp256k1 public key - it IS the scalar.
        // We store the raw 32-byte scalar, then reduce it to get the DustPublicKey (also 32 bytes).
        // This matches: ledger.DustSecretKey.fromSeed(seed) where DustPublicKey = seed % BLS_FIELD
        auto dust_scalar = bls_reduce(node.key);

        KeyPair kp;
        // secret_key = the SLIP-10 scalar (for signing with DustSecretKey)
        kp.secret_key.assign(node.key.begin(), node.key.end());
        // public_key = the BLS-reduced scalar (this IS the DustPublicKey)
        kp.public_key.assign(dust_scalar.begin(), dust_scalar.end());

        // Encode as Midnight dust Bech32m address
        // The SDK does: DustAddress.encodePublicKey(network, DustPublicKey)
        // which stores the scalar as SCALE BigInt and encodes with Bech32m prefix "mn_dust_preview1"
        std::vector<uint8_t> dust_scalar_vec(dust_scalar.begin(), dust_scalar.end());
        kp.address = address::encode_dust(dust_scalar_vec, address::Network::Preview);
        return kp;
    }

    KeyPair HDWallet::derive_zswap(uint32_t acc, uint32_t idx) const {
        auto node = derive_path(acc, Role::Zswap, idx);

        unsigned char pk[32];
        unsigned char sk[64];
        crypto_sign_seed_keypair(pk, sk, node.key.data());
        std::vector<uint8_t> pub_vec(pk, pk + 32);

        KeyPair kp;
        kp.secret_key.assign(sk, sk + 64);
        kp.public_key.assign(pk, pk + 32);
        kp.address = address::encode_unshielded(pub_vec, address::Network::Preview);
        return kp;
    }

    KeyPair HDWallet::derive_metadata(uint32_t acc, uint32_t idx) const {
        return derive(acc, Role::Metadata, idx);
    }

    std::vector<uint8_t> HDWallet::derive_shielded_encryption_key(
        const std::vector<uint8_t>& zswap_sk) const {
        if (zswap_sk.size() < 32) {
            throw std::runtime_error("zswap secret key too short");
        }

        unsigned char scalar[32];
        std::memcpy(scalar, zswap_sk.data(), 32);

        // Clamp scalar for Curve25519
        scalar[0]  &= 248;
        scalar[31] &= 127;
        scalar[31] |= 64;

        unsigned char base[32] = {0};
        base[0] = 9;

        unsigned char result[32];
        if (crypto_scalarmult(result, scalar, base) != 0) {
            throw std::runtime_error("x25519 scalar mult failed");
        }

        return std::vector<uint8_t>(result, result + 32);
    }
}
