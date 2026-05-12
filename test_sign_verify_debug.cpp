/**
 * Debug: trace sign/verify roundtrip in signing_submission flow
 */
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <array>
#include <sstream>
#include <iomanip>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

static const uint8_t SECP256K1_N[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x7C
};

static void print_hex(const char* label, const uint8_t* data, size_t len) {
    printf("[%s] ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

static std::array<uint8_t, 32> sha256_arr(const uint8_t* data, size_t len) {
    std::array<uint8_t, 32> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data, len);
    unsigned int l = 32;
    EVP_DigestFinal_ex(ctx, out.data(), &l);
    EVP_MD_CTX_free(ctx);
    return out;
}

static std::array<uint8_t, 32> tagged_hash(const uint8_t* msg, size_t msg_len, const char* tag) {
    auto tag_h = sha256_arr((const uint8_t*)tag, strlen(tag));
    std::vector<uint8_t> buf;
    buf.insert(buf.end(), tag_h.begin(), tag_h.end());
    buf.insert(buf.end(), tag_h.begin(), tag_h.end());
    buf.insert(buf.end(), msg, msg + msg_len);
    return sha256_arr(buf.data(), buf.size());
}

static bool hex_to_bytes(const std::string& hex, uint8_t* out, size_t len) {
    std::string h = hex;
    if (h.size() >= 2 && h.substr(0, 2) == "0x") h = h.substr(2);
    if (h.size() != len * 2) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned int byte;
        std::istringstream(h.substr(i * 2, 2)) >> std::hex >> byte;
        out[i] = static_cast<uint8_t>(byte);
    }
    return true;
}

static std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; i++) oss << std::setw(2) << (int)data[i];
    return oss.str();
}

// Mimic generate_keypair from Bip340Signer
static bool generate_keypair(uint8_t* sk_out, uint8_t* pk_out) {
    // Generate random sk
    if (RAND_bytes(sk_out, 32) != 1) return false;
    sk_out[0] &= 0x7F;
    sk_out[31] &= 0xF8;
    bool is_zero = true;
    for (int i = 0; i < 32; i++) if (sk_out[i] != 0) { is_zero = false; break; }
    if (is_zero) sk_out[31] = 1;

    // Compute pk = sk * G, force odd y
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    BN_CTX* ctx = BN_CTX_new();
    const EC_POINT* gen = EC_GROUP_get0_generator(group);
    EC_POINT* G = EC_POINT_new(group);
    EC_POINT_copy(G, gen);
    
    BIGNUM* d = BN_bin2bn(sk_out, 32, nullptr);
    EC_POINT* P = EC_POINT_new(group);
    EC_POINT_mul(group, P, nullptr, G, d, ctx);
    
    BIGNUM* P_x = BN_new(), *P_y = BN_new();
    EC_POINT_get_affine_coordinates_GFp(group, P, P_x, P_y, ctx);
    
    // BIP-340: force odd y
    if (!BN_is_odd(P_y)) {
        EC_POINT_invert(group, P, ctx);
        EC_POINT_get_affine_coordinates_GFp(group, P, P_x, P_y, ctx);
    }
    
    BN_bn2binpad(P_x, pk_out, 32);
    
    printf("[KEYGEN] P_x: "); for (int i = 0; i < 32; i++) printf("%02x", pk_out[i]); printf("\n");
    
    BN_free(P_x); BN_free(P_y); BN_free(d);
    EC_POINT_free(P); EC_POINT_free(G);
    BN_CTX_free(ctx); EC_GROUP_free(group);
    return true;
}

// Mimic sign_message from Bip340Signer
static bool sign_message(const uint8_t* sk, const uint8_t* pk,
                        const std::vector<uint8_t>& msg,
                        uint8_t* sig_out /* 64 bytes */) {
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    BN_CTX* ctx = BN_CTX_new();
    
    const EC_POINT* gen = EC_GROUP_get0_generator(group);
    EC_POINT* G = EC_POINT_new(group);
    EC_POINT_copy(G, gen);
    
    // Compute P = d*G, verify matches pk
    BIGNUM* d = BN_bin2bn(sk, 32, nullptr);
    EC_POINT* P_check = EC_POINT_new(group);
    EC_POINT_mul(group, P_check, nullptr, G, d, ctx);
    BIGNUM* P_x_check = BN_new(), *P_y_check = BN_new();
    EC_POINT_get_affine_coordinates_GFp(group, P_check, P_x_check, P_y_check, ctx);
    uint8_t pk_check[32];
    BN_bn2binpad(P_x_check, pk_check, 32);
    printf("[SIGN] pk from sk:  "); for (int i = 0; i < 32; i++) printf("%02x", pk_check[i]); printf("\n");
    printf("[SIGN] expected pk: "); for (int i = 0; i < 32; i++) printf("%02x", pk[i]); printf("\n");
    if (memcmp(pk_check, pk, 32) != 0) {
        printf("[SIGN] ERROR: pk mismatch!\n");
        return false;
    }
    BN_free(P_x_check); BN_free(P_y_check); EC_POINT_free(P_check);
    
    // msg_hash
    auto msg_hash = tagged_hash(msg.data(), msg.size(), "BIP0340/message");
    print_hex("msg_hash", msg_hash.data(), 32);
    
    // nonce RFC6979 (simplified - first try, no loop)
    uint8_t V[32] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                     0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                     0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                     0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    uint8_t K[32] = {0};
    uint8_t buf1[97], buf2[97];
    memcpy(buf1, V, 32); buf1[32] = 0x00; memcpy(buf1+33, sk, 32); memcpy(buf1+65, pk, 32);
    memcpy(buf2, V, 32); buf2[32] = 0x01; memcpy(buf2+33, sk, 32); memcpy(buf2+65, pk, 32);
    HMAC(EVP_sha256(), K, 32, buf1, 97, K, nullptr);
    HMAC(EVP_sha256(), K, 32, V, 32, V, nullptr);
    HMAC(EVP_sha256(), K, 32, buf2, 97, K, nullptr);
    HMAC(EVP_sha256(), K, 32, V, 32, V, nullptr);
    V[0] &= 0x7F; V[31] &= 0xF8;
    uint8_t nonce[32];
    memcpy(nonce, V, 32);
    print_hex("nonce", nonce, 32);
    
    // R = nonce * G
    BIGNUM* k = BN_bin2bn(nonce, 32, nullptr);
    EC_POINT* R = EC_POINT_new(group);
    EC_POINT_mul(group, R, nullptr, G, k, ctx);
    BIGNUM* R_x = BN_new(), *R_y = BN_new();
    EC_POINT_get_affine_coordinates_GFp(group, R, R_x, R_y, ctx);
    uint8_t Rx_bytes[32];
    BN_bn2binpad(R_x, Rx_bytes, 32);
    int Ry_parity = BN_is_odd(R_y);
    print_hex("R_x (raw)", Rx_bytes, 32);
    printf("[SIGN] Ry_parity=%d\n", Ry_parity);
    
    // k_effective
    BIGNUM* k_eff = BN_dup(k);
    if (Ry_parity == 1) {
        BIGNUM* n = BN_bin2bn(SECP256K1_N, 32, nullptr);
        BIGNUM* k_neg = BN_new();
        BN_mod_sub(k_neg, n, k, n, ctx);
        BN_free(k_eff);
        k_eff = k_neg;
        BN_free(n);
    }
    
    // e = hash(Rx || Px || msg_hash)
    uint8_t e_in[96];
    memcpy(e_in, Rx_bytes, 32);
    memcpy(e_in+32, pk, 32);
    memcpy(e_in+64, msg_hash.data(), 32);
    auto e_hash = sha256_arr(e_in, 96);
    print_hex("e_hash", e_hash.data(), 32);
    
    // s = k_eff + e*d mod n
    BIGNUM* e = BN_bin2bn(e_hash.data(), 32, nullptr);
    BIGNUM* n = BN_bin2bn(SECP256K1_N, 32, nullptr);
    BN_mod(e, e, n, ctx);
    BIGNUM* e_d = BN_new();
    BN_mod_mul(e_d, e, d, n, ctx);
    BIGNUM* s = BN_new();
    BN_mod_add(s, k_eff, e_d, n, ctx);
    uint8_t s_bytes[32];
    BN_bn2binpad(s, s_bytes, 32);
    
    memcpy(sig_out, Rx_bytes, 32);
    memcpy(sig_out+32, s_bytes, 32);
    
    print_hex("SIG Rx", sig_out, 32);
    print_hex("SIG s ", sig_out+32, 32);
    
    BN_free(R_x); BN_free(R_y); BN_free(k); BN_free(k_eff);
    BN_free(e); BN_free(n); BN_free(e_d); BN_free(s); BN_free(d);
    EC_POINT_free(R); EC_POINT_free(G); EC_POINT_free(P_check);
    BN_CTX_free(ctx); EC_GROUP_free(group);
    return true;
}

// Mimic verify_signature from Bip340Signer
static bool verify_signature(const uint8_t* pk,
                             const std::vector<uint8_t>& msg,
                             const uint8_t* sig /* 64 bytes */) {
    uint8_t Rx_bytes[32], s_bytes[32];
    memcpy(Rx_bytes, sig, 32);
    memcpy(s_bytes, sig+32, 32);
    print_hex("VERIFY Rx", Rx_bytes, 32);
    print_hex("VERIFY Px", pk, 32);
    
    auto msg_hash = tagged_hash(msg.data(), msg.size(), "BIP0340/message");
    print_hex("VERIFY msg_hash", msg_hash.data(), 32);
    
    uint8_t e_in[96];
    memcpy(e_in, Rx_bytes, 32);
    memcpy(e_in+32, pk, 32);
    memcpy(e_in+64, msg_hash.data(), 32);
    auto e_hash = sha256_arr(e_in, 96);
    print_hex("VERIFY e_hash", e_hash.data(), 32);
    
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    BN_CTX* ctx = BN_CTX_new();
    
    const EC_POINT* gen = EC_GROUP_get0_generator(group);
    EC_POINT* G = EC_POINT_new(group);
    EC_POINT_copy(G, gen);
    
    BIGNUM* n = BN_bin2bn(SECP256K1_N, 32, nullptr);
    BIGNUM* e = BN_bin2bn(e_hash.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(s_bytes, 32, nullptr);
    BIGNUM* Rx = BN_bin2bn(Rx_bytes, 32, nullptr);
    BIGNUM* Px = BN_bin2bn(pk, 32, nullptr);
    BN_mod(e, e, n, ctx);
    
    bool result = false;
    for (int parity_P = 0; parity_P <= 1 && !result; parity_P++) {
        printf("\n[VERIFY] Trying P parity=%d\n", parity_P);
        EC_POINT* P = EC_POINT_new(group);
        if (!EC_POINT_set_compressed_coordinates(group, P, Px, parity_P, ctx)) {
            EC_POINT_free(P);
            continue;
        }
        
        // Verify P is on curve
        if (!EC_POINT_is_on_curve(group, P, ctx)) {
            printf("  P not on curve\n");
            EC_POINT_free(P);
            continue;
        }
        
        // Get full P coordinates
        BIGNUM* P_x_full = BN_new(), *P_y_full = BN_new();
        EC_POINT_get_affine_coordinates_GFp(group, P, P_x_full, P_y_full, ctx);
        uint8_t P_x_check[32], P_y_check[32];
        BN_bn2binpad(P_x_full, P_x_check, 32);
        BN_bn2binpad(P_y_full, P_y_check, 32);
        printf("  P.x: "); for (int i = 0; i < 32; i++) printf("%02x", P_x_check[i]); printf("\n");
        printf("  P.y: "); for (int i = 0; i < 32; i++) printf("%02x", P_y_check[i]); printf(" (parity=%d)\n", BN_is_odd(P_y_full));
        BN_free(P_x_full); BN_free(P_y_full);
        
        EC_POINT* sG = EC_POINT_new(group);
        EC_POINT* eP = EC_POINT_new(group);
        EC_POINT* R_prime = EC_POINT_new(group);
        
        EC_POINT_mul(group, sG, nullptr, G, s, ctx);
        EC_POINT_mul(group, eP, nullptr, P, e, ctx);
        EC_POINT_invert(group, eP, ctx);
        EC_POINT_add(group, R_prime, sG, eP, ctx);
        
        BIGNUM* R_x_cmp = BN_new(), *R_y_cmp = BN_new();
        EC_POINT_get_affine_coordinates_GFp(group, R_prime, R_x_cmp, R_y_cmp, ctx);
        uint8_t R_prime_x[32];
        BN_bn2binpad(R_x_cmp, R_prime_x, 32);
        printf("  R'.x: "); for (int i = 0; i < 32; i++) printf("%02x", R_prime_x[i]); printf("\n");
        printf("  Rx:   "); for (int i = 0; i < 32; i++) printf("%02x", Rx_bytes[i]); printf("\n");
        
        if (BN_cmp(R_x_cmp, Rx) == 0) {
            printf("  *** MATCH! ***\n");
            result = true;
        } else {
            printf("  No match\n");
        }
        
        BN_free(R_x_cmp); BN_free(R_y_cmp);
        EC_POINT_free(sG); EC_POINT_free(eP); EC_POINT_free(R_prime); EC_POINT_free(P);
    }
    
    BN_free(n); BN_free(e); BN_free(s); BN_free(Rx); BN_free(Px);
    EC_POINT_free(G); BN_CTX_free(ctx); EC_GROUP_free(group);
    
    return result;
}

int main() {
    printf("=== BIP-340 Sign/Verify Roundtrip Debug ===\n\n");
    
    // Generate keypair
    uint8_t sk[32], pk[32];
    if (!generate_keypair(sk, pk)) {
        printf("Key generation failed\n");
        return 1;
    }
    
    // Message: same as test
    std::vector<uint8_t> msg = {0x00, 0x01, 0x02, 0x03, 0x04};
    
    // Sign
    uint8_t sig[64];
    printf("\n=== SIGNING ===\n");
    if (!sign_message(sk, pk, msg, sig)) {
        printf("Signing failed\n");
        return 1;
    }
    
    // Verify
    printf("\n=== VERIFYING ===\n");
    bool ok = verify_signature(pk, msg, sig);
    printf("\n=== RESULT: %s ===\n", ok ? "PASS" : "FAIL");
    
    return ok ? 0 : 1;
}
