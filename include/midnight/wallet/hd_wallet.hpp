#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace midnight::wallet
{

    /**
     * @brief Key derivation roles matching official Midnight Wallet SDK.
     *
     * Values from @midnight-ntwrk/wallet-sdk-hd HDWallet.ts:
     *   NightExternal: 0, NightInternal: 1, Dust: 2, Zswap: 3, Metadata: 4
     */
    enum class Role : uint32_t
    {
        NightExternal = 0, ///< Unshielded NIGHT transfers (external)
        NightInternal = 1, ///< Unshielded NIGHT transfers (internal/change)
        Dust = 2,          ///< Fee (DUST) token management
        Zswap = 3,        ///< Shielded (ZK-private) transactions
        Metadata = 4       ///< Wallet metadata key
    };

    /**
     * @brief Ed25519 key pair with derived Midnight address
     *
     * KeyPair uses libsodium Ed25519 (via midnight::crypto::Ed25519Signer):
     * - secret_key: 64 bytes = Ed25519 expanded private key
     * - public_key: 32 bytes = Ed25519 public key
     * - address: Bech32m-encoded Midnight address (mn_addr prefix)
     *
     * Signing/verification uses crypto_sign_detached/crypto_sign_verify_detached.
     */
    struct KeyPair
    {
        /// 64-byte Ed25519 expanded secret key (seed || SHA512(seed)[32:]).
        /// Use this directly with crypto_sign_detached for signing.
        /// The corresponding public key must be derived via crypto_sign_seed_keypair
        /// to ensure sign/verify roundtrip compatibility.
        std::vector<uint8_t> secret_key;
        std::vector<uint8_t> public_key; ///< 32-byte Ed25519 public key
        std::string address;             ///< Bech32m-encoded Midnight address

        /// Sign a message with Ed25519 (libsodium), returns 64-byte signature
        std::vector<uint8_t> sign(const std::vector<uint8_t> &message) const;

        /// Verify an Ed25519 signature against this key pair's public key
        bool verify(const std::vector<uint8_t> &message,
                    const std::vector<uint8_t> &signature) const;
    };

    /**
     * @brief BIP39 mnemonic utilities
     */
    namespace bip39
    {
        /// Generate a BIP39 mnemonic (12, 15, 18, 21, or 24 words)
        std::string generate_mnemonic(int word_count = 24);

        /// Validate a BIP39 mnemonic
        bool validate_mnemonic(const std::string &mnemonic);

        /// Convert mnemonic to 64-byte seed (PBKDF2-HMAC-SHA512, 2048 rounds)
        std::vector<uint8_t> mnemonic_to_seed(const std::string &mnemonic,
                                              const std::string &passphrase = "");
    }

    /**
     * @brief Hierarchical Deterministic wallet for Midnight
     *
     * Key derivation path: m/44'/2400'/account'/role/index
     *   - 2400 = Midnight coin type (official SLIP-44 registration)
     *   - role  = NightExternal(0) | NightInternal(1) | Dust(2) | Zswap(3) | Metadata(4)
     *
     * Uses SLIP-10 for Ed25519 (hardened-only derivation at levels 1-3).
     * Level 4 (role) is NON-hardened per SLIP-10 spec — the public key at
     * that level is derived and exposed as the role-specific public key.
     * Level 5 (index) is NON-hardened for address discovery (BIP-44 chain).
     *
     * Values verified against @midnight-ntwrk/wallet-sdk-hd v4.0.
     *
     * Example:
     * ```cpp
     * auto wallet = HDWallet::from_mnemonic("abandon abandon ... about");
     * auto night_key = wallet.derive_night(0, 0);
     * auto dust_key  = wallet.derive_dust(0, 0);
     * ```
     */
    class HDWallet
    {
    public:
        /// Create HD wallet from BIP39 mnemonic
        static HDWallet from_mnemonic(const std::string &mnemonic,
                                      const std::string &passphrase = "");

        /// Create HD wallet from raw 64-byte seed
        static HDWallet from_seed(const std::vector<uint8_t> &seed_64);

        /// Derive key for specific account/role/index
        KeyPair derive(uint32_t account, Role role, uint32_t index) const;

        /// Convenience: derive NightExternal (unshielded) key — role 0
        KeyPair derive_night(uint32_t account = 0, uint32_t index = 0) const;

        /// Convenience: derive NightInternal (change) key — role 1
        KeyPair derive_night_internal(uint32_t account = 0, uint32_t index = 0) const;

        /// Convenience: derive Dust (fee) key — role 2
        KeyPair derive_dust(uint32_t account = 0, uint32_t index = 0) const;

        /// Convenience: derive ZSwap (shielded) key — role 3
        KeyPair derive_zswap(uint32_t account = 0, uint32_t index = 0) const;

        /// Convenience: derive Metadata key — role 4
        KeyPair derive_metadata(uint32_t account = 0, uint32_t index = 0) const;

        /// Derive the shielded encryption public key from the Zswap keypair.
        ///
        /// The SDK derives the encryption public key via Curve25519 scalar multiplication
        /// of the zswap secret key with the Ristretto255 base point. This keeps the
        /// encryption key cryptographically linked to the zswap key while remaining
        /// computationally independent (different curve). This means the shielded address
        /// coin_pk = zswap_pk and enc_pk = curve25519(scalar_from_zswap_sk).
        ///
        /// @param zswap_secret_key 64-byte Ed25519 secret key from derive_zswap()
        /// @return 32-byte encryption public key (Curve25519 point)
        std::vector<uint8_t> derive_shielded_encryption_key(
            const std::vector<uint8_t> &zswap_secret_key) const;

        /// Get the raw 64-byte master seed
        const std::vector<uint8_t> &master_seed() const { return seed_; }

    private:
        std::vector<uint8_t> seed_;         // 64-byte BIP39 seed
        std::array<uint8_t, 32> master_key_;   // BIP32 master private key
        std::array<uint8_t, 32> master_chain_; // BIP32 master chain code

        struct ChainNode
        {
            std::array<uint8_t, 32> key;
            std::array<uint8_t, 32> chain_code;
        };

        /// Derive full path: m/44'/2400'/account'/role/index
        ChainNode derive_path(uint32_t account, Role role, uint32_t index) const;
    };

} // namespace midnight::wallet
