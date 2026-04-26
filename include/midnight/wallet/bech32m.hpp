#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace midnight::wallet {

/**
 * @brief Bech32m encoder/decoder for Midnight network addresses.
 *
 * Ported from BIP-350 reference implementation (Bitcoin Core).
 * HRP prefixes from @midnight-ntwrk/wallet-sdk-address-format:
 *
 *   UnshieldedAddress: mn_addr           (mainnet)
 *                      mn_addr_preview   (preview)
 *   ShieldedAddress:   mn_shield-addr    (mainnet)
 *                      mn_shield-addr_preview
 *   DustAddress:       mn_dust           (mainnet)
 *                      mn_dust_preview
 */
namespace bech32m {

    /// Encode raw bytes into a Bech32m string with the given HRP.
    /// The data is first converted to 5-bit groups (toWords), then
    /// a Bech32m checksum is appended.
    std::string encode(const std::string& hrp, const std::vector<uint8_t>& data);

    /// Decode a Bech32m string. Returns the HRP and the data bytes.
    /// Throws std::runtime_error on invalid input.
    struct DecodeResult {
        std::string hrp;
        std::vector<uint8_t> data;
    };
    DecodeResult decode(const std::string& bech32_str);

    /// Convert 8-bit byte array to 5-bit groups (used in Bech32 data part)
    std::vector<uint8_t> to_words(const std::vector<uint8_t>& data);

    /// Convert 5-bit groups back to 8-bit bytes
    std::vector<uint8_t> from_words(const std::vector<uint8_t>& words);

} // namespace bech32m

/**
 * @brief Midnight-specific address encoding utilities.
 *
 * Follows the exact same format as the official TypeScript
 * @midnight-ntwrk/wallet-sdk-address-format package.
 *
 * Network IDs match SDK:
 *   MainNet, TestNet, DevNet, QaNet, Undeployed, Preview, PreProd
 */
namespace address {

    /// Network identifiers — all 7 values from @midnight-ntwrk/wallet-sdk-abstractions/NetworkId
    enum class Network {
        Mainnet,      ///< mn_addr                (mainnet)
        Preview,      ///< mn_addr_preview         (preview testnet)
        PreProd,      ///< mn_addr_preprod          (pre-production)
        Testnet,      ///< mn_addr_testnet          (testnet)
        DevNet,       ///< mn_addr_devnet           (devnet)
        QaNet,        ///< mn_addr_qanet            (QA network)
        Undeployed    ///< mn_addr_undeployed       (local/undeployed)
    };

    /// Convert Network enum to SDK string (e.g., "preview", "mainnet")
    std::string network_to_string(Network network);

    /// Parse SDK string to Network enum
    Network network_from_string(const std::string& name);

    /// Encode a 32-byte public key into a Midnight unshielded address
    /// e.g. "mn_addr_preview1qpz..."
    std::string encode_unshielded(const std::vector<uint8_t>& pubkey_32,
                                   Network network = Network::Preview);

    /// Decode a Midnight unshielded address back to a 32-byte public key
    std::vector<uint8_t> decode_unshielded(const std::string& address);

    /// Encode a shielded address (coinPubKey + encryptionPubKey = 64 bytes)
    /// SDK: ShieldedAddress.codec type="shield-addr", data = coinPubKey || encPubKey
    std::string encode_shielded(const std::vector<uint8_t>& coin_pk_32,
                                 const std::vector<uint8_t>& enc_pk_32,
                                 Network network = Network::Preview);

    /// Decode a shielded address back to coinPubKey (32) + encPubKey (32)
    struct ShieldedAddressData {
        std::vector<uint8_t> coin_public_key;        // 32 bytes
        std::vector<uint8_t> encryption_public_key;   // 32 bytes
    };
    ShieldedAddressData decode_shielded(const std::string& address);

    /// Encode a dust address from BLS scalar (SCALE-encoded BigInt)
    /// SDK: DustAddress uses bigint → SCALE BigInt encoding → Bech32m
    /// @param dust_scalar_le Little-endian bytes of the BLS scalar (DustPublicKey)
    std::string encode_dust(const std::vector<uint8_t>& dust_scalar_le,
                             Network network = Network::Preview);

    /// Encode a dust address from raw 32-byte public key
    /// Convenience: converts Ed25519 pubkey to BLS scalar format first
    std::string encode_dust_from_pubkey(const std::vector<uint8_t>& pubkey_32,
                                         Network network = Network::Preview);

    /// Decode a dust address back to BLS scalar bytes
    std::vector<uint8_t> decode_dust(const std::string& address);

    /// Get the HRP string for a given type and network
    std::string get_hrp(const std::string& type, Network network);

} // namespace address

} // namespace midnight::wallet
