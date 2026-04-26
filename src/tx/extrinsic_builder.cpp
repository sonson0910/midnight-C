#include "midnight/tx/extrinsic_builder.hpp"
#include <sodium.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace midnight::tx
{

    // ─── PalletCall ───────────────────────────────────────────

    PalletCall PalletCall::system_remark(const std::vector<uint8_t> &remark)
    {
        // system.remark = pallet 0, call 0, args = SCALE bytes(remark)
        codec::ScaleEncoder enc;
        enc.encode_bytes(remark);
        return PalletCall{0x00, 0x00, enc.data()};
    }

    PalletCall PalletCall::custom(uint8_t pallet, uint8_t call,
                                  const std::vector<uint8_t> &data)
    {
        return PalletCall{pallet, call, data};
    }

    // ─── ExtrinsicBuilder ─────────────────────────────────────

    ExtrinsicBuilder::ExtrinsicBuilder(const ExtrinsicParams &params)
        : params_(params)
    {
        if (params_.genesis_hash.size() != 32)
            throw std::runtime_error("Genesis hash must be 32 bytes");
        if (params_.block_hash.size() != 32)
            throw std::runtime_error("Block hash must be 32 bytes");
    }

    std::vector<uint8_t> ExtrinsicBuilder::encode_call(const PalletCall &call) const
    {
        std::vector<uint8_t> result;
        result.push_back(call.pallet_index);
        result.push_back(call.call_index);
        result.insert(result.end(), call.call_data.begin(), call.call_data.end());
        return result;
    }

    std::vector<uint8_t> ExtrinsicBuilder::encode_extra() const
    {
        codec::ScaleEncoder enc;

        // Era
        if (params_.mortal_era)
        {
            enc.encode_era_mortal(params_.era_block, params_.era_period);
        }
        else
        {
            enc.encode_era_immortal();
        }

        // Nonce (compact)
        enc.encode_compact(params_.nonce);

        // Tip (compact)
        enc.encode_compact(params_.tip);

        return {enc.data().begin(), enc.data().end()};
    }

    std::vector<uint8_t> ExtrinsicBuilder::encode_additional() const
    {
        codec::ScaleEncoder enc;

        // spec_version (u32)
        enc.encode_u32_le(params_.spec_version);

        // tx_version (u32)
        enc.encode_u32_le(params_.tx_version);

        // Genesis hash (32 raw bytes)
        enc.encode_raw(params_.genesis_hash);

        // Block hash / checkpoint (32 raw bytes)
        // For immortal era, use genesis hash
        if (!params_.mortal_era)
        {
            enc.encode_raw(params_.genesis_hash);
        }
        else
        {
            enc.encode_raw(params_.block_hash);
        }

        return {enc.data().begin(), enc.data().end()};
    }

    std::vector<uint8_t> ExtrinsicBuilder::build_signing_payload(const PalletCall &call)
    {
        auto call_data = encode_call(call);
        auto extra = encode_extra();
        auto additional = encode_additional();

        // payload = call || extra || additional
        std::vector<uint8_t> payload;
        payload.insert(payload.end(), call_data.begin(), call_data.end());
        payload.insert(payload.end(), extra.begin(), extra.end());
        payload.insert(payload.end(), additional.begin(), additional.end());

        // If payload > 256 bytes, hash it with blake2b-256
        if (payload.size() > 256)
        {
            std::vector<uint8_t> hash(32);
            crypto_generichash(hash.data(), 32, payload.data(), payload.size(),
                               nullptr, 0);
            return hash;
        }

        return payload;
    }

    std::vector<uint8_t> ExtrinsicBuilder::build_signed(
        const PalletCall &call,
        const std::vector<uint8_t> &secret_key_64,
        const std::vector<uint8_t> &public_key_32)
    {
        if (secret_key_64.size() != 64)
            throw std::runtime_error("Secret key must be 64 bytes");
        if (public_key_32.size() != 32)
            throw std::runtime_error("Public key must be 32 bytes");

        if (sodium_init() < 0)
            throw std::runtime_error("libsodium init failed");

        // 1. Build signing payload
        auto payload = build_signing_payload(call);

        // 2. Sign with Ed25519
        std::vector<uint8_t> signature(crypto_sign_BYTES); // 64 bytes
        unsigned long long sig_len;
        crypto_sign_detached(signature.data(), &sig_len,
                             payload.data(), payload.size(),
                             secret_key_64.data());

        // 3. Assemble the signed extrinsic (before length prefix)
        codec::ScaleEncoder body;

        // Version byte: 0x84 = signed + Ed25519
        //   bit 7 = 1 (signed), bits 0-6 = 4 (extrinsic version)
        body.encode_u8(0x84);

        // Signer: MultiAddress::Id(32 bytes)
        body.encode_multi_address_id(public_key_32);

        // Signature type: Ed25519 = 0x00
        body.encode_u8(0x00);

        // Signature: 64 raw bytes
        body.encode_raw(signature);

        // Extra: era + nonce + tip
        auto extra = encode_extra();
        body.encode_raw(extra);

        // Call data
        auto call_data = encode_call(call);
        body.encode_raw(call_data);

        // 4. Prefix with compact length
        return body.finalize_with_length();
    }

    std::vector<uint8_t> ExtrinsicBuilder::build_unsigned(const PalletCall &call)
    {
        codec::ScaleEncoder body;

        // Version byte: 0x04 = unsigned, extrinsic version 4
        body.encode_u8(0x04);

        // Call data
        auto call_data = encode_call(call);
        body.encode_raw(call_data);

        return body.finalize_with_length();
    }

    std::string ExtrinsicBuilder::to_hex(const std::vector<uint8_t> &data)
    {
        return codec::util::bytes_to_hex(data);
    }

} // namespace midnight::tx
