#pragma once

#include "midnight/codec/scale_codec.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace midnight::tx
{

    /**
     * @brief Parameters for building a Substrate-compatible signed extrinsic
     */
    struct ExtrinsicParams
    {
        uint32_t spec_version;                 ///< Runtime spec version (e.g., 22000)
        uint32_t tx_version;                   ///< Transaction version (e.g., 2)
        std::vector<uint8_t> genesis_hash;     ///< 32-byte genesis block hash
        std::vector<uint8_t> block_hash;       ///< 32-byte checkpoint block hash
        uint64_t nonce;                        ///< Account nonce
        uint64_t tip = 0;                      ///< Tip amount (0 for no tip)
        bool mortal_era = false;               ///< Use mortal era?
        uint64_t era_block = 0;                ///< Current block for mortal era
        uint64_t era_period = 64;              ///< Era period (power of 2)
    };

    /**
     * @brief A pallet call (module index + call index + encoded arguments)
     */
    struct PalletCall
    {
        uint8_t pallet_index;                  ///< Pallet module index
        uint8_t call_index;                    ///< Call index within pallet
        std::vector<uint8_t> call_data;        ///< SCALE-encoded call arguments

        /// system.remark(data) — pallet 0, call 0
        static PalletCall system_remark(const std::vector<uint8_t> &remark);

        /// Custom call with raw data
        static PalletCall custom(uint8_t pallet, uint8_t call,
                                 const std::vector<uint8_t> &data = {});
    };

    /**
     * @brief Substrate-compatible extrinsic builder
     *
     * Constructs properly SCALE-encoded signed extrinsics following:
     *   1. SCALE compact length prefix
     *   2. Version byte (0x84 = signed, Ed25519)
     *   3. MultiAddress sender
     *   4. Ed25519 signature (64 bytes)
     *   5. Signed extensions (era, nonce, tip)
     *   6. Call data (pallet_index + call_index + args)
     *
     * The signing payload follows Substrate convention:
     *   payload = call || extra || additional_signed
     *   If payload > 256 bytes, sign blake2b(payload) instead.
     *
     * Example:
     * ```cpp
     * ExtrinsicParams params{.spec_version = 22000, .tx_version = 2, ...};
     * ExtrinsicBuilder builder(params);
     * auto extrinsic = builder.build_signed(
     *     PalletCall::system_remark({0x48, 0x65, 0x6C, 0x6C, 0x6F}),
     *     secret_key_64, public_key_32);
     * // Submit via author_submitExtrinsic
     * ```
     */
    class ExtrinsicBuilder
    {
    public:
        explicit ExtrinsicBuilder(const ExtrinsicParams &params);

        /// Build a signed extrinsic (SCALE-encoded, ready for RPC submission)
        std::vector<uint8_t> build_signed(
            const PalletCall &call,
            const std::vector<uint8_t> &secret_key_64,
            const std::vector<uint8_t> &public_key_32);

        /// Build an unsigned extrinsic
        std::vector<uint8_t> build_unsigned(const PalletCall &call);

        /// Build the raw signing payload (for external/hardware signers)
        std::vector<uint8_t> build_signing_payload(const PalletCall &call);

        /// Convert extrinsic bytes to hex string (for RPC)
        static std::string to_hex(const std::vector<uint8_t> &data);

    private:
        ExtrinsicParams params_;

        /// Encode call data: pallet_index + call_index + args
        std::vector<uint8_t> encode_call(const PalletCall &call) const;

        /// Encode "extra" signed extensions: era + nonce + tip
        std::vector<uint8_t> encode_extra() const;

        /// Encode "additional signed" data: spec_ver + tx_ver + genesis + block_hash
        std::vector<uint8_t> encode_additional() const;
    };

} // namespace midnight::tx
