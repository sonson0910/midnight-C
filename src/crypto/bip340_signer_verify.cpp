    // ============================================================
    // BIP-340 VERIFY - CLEAN IMPLEMENTATION
    // ============================================================
    bool Bip340Signer::verify_signature(const std::vector<uint8_t>& message,
                                       const std::string& signature_hex,
                                       const std::string& public_key_hex) {
        // Parse signature: Rx (32 bytes) + s (32 bytes)
        std::string sig_hex = signature_hex;
        if (sig_hex.size() >= 2 && sig_hex.substr(0, 2) == "0x") {
            sig_hex = sig_hex.substr(2);
        }
        if (sig_hex.size() != 128) return false;

        std::array<uint8_t, 32> Rx_bytes{};
        std::array<uint8_t, 32> s_bytes{};
        if (!hex_to_bytes(sig_hex.substr(0, 64), Rx_bytes.data(), 32)) return false;
        if (!hex_to_bytes(sig_hex.substr(64, 64), s_bytes.data(), 32)) return false;

        // Parse public key (x-only, 32 bytes)
        std::string pk_hex = public_key_hex;
        if (pk_hex.size() >= 2 && pk_hex.substr(0, 2) == "0x") {
            pk_hex = pk_hex.substr(2);
        }
        if (pk_hex.size() != 64) return false;
        std::array<uint8_t, 32> Px_bytes{};
        if (!hex_to_bytes(pk_hex, Px_bytes.data(), 32)) return false;

        // Message hash
        auto msg_hash = tagged_hash(message.data(), message.size(), "BIP0340/message");

        // e = hash(Rx || Px || msg_hash)
        uint8_t e_input[96];
        memcpy(e_input, Rx_bytes.data(), 32);
        memcpy(e_input + 32, Px_bytes.data(), 32);
        memcpy(e_input + 64, msg_hash.data(), 32);
        auto e_hash = sha256(e_input, 96);

        // Get EC group
        EC_GROUP* group = get_secp256k1_group();
        if (!group) return false;
        
        BN_CTX* ctx = BN_CTX_new();
        if (!ctx) return false;

        // Create G from hardcoded constants
        BIGNUM* Gx = BN_bin2bn(SECP256K1_GX, 32, nullptr);
        BIGNUM* Gy = BN_bin2bn(SECP256K1_GY, 32, nullptr);
        if (!Gx || !Gy) {
            BN_free(Gx); BN_free(Gy);
            BN_CTX_free(ctx);
            return false;
        }
        EC_POINT* G = EC_POINT_new(group);
        if (!G || !EC_POINT_set_affine_coordinates_GFp(group, G, Gx, Gy, ctx)) {
            BN_free(Gx); BN_free(Gy);
            if (G) EC_POINT_free(G);
            BN_CTX_free(ctx);
            return false;
        }
        BN_free(Gx); BN_free(Gy);

        // Parse scalars
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

        // Try both parities for P
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
                EC_POINT_get_affine_coordinates_GFp(group, R, R_x, R_y, ctx);
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
