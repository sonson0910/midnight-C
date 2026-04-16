#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace midnight::blockchain
{
    struct OfficialAddressResult
    {
        bool success = false;
        std::string unshielded_address;
        std::string shielded_address;
        std::string dust_address;
        std::string seed_hex;
        bool dust_registration_requested = false;
        bool dust_registration_attempted = false;
        bool dust_registration_success = false;
        std::string dust_registration_message;
        std::string dust_registration_txid;
        std::string error;
    };

    struct OfficialTransferResult
    {
        bool success = false;
        std::string network;
        std::string source_address;
        std::string destination_address;
        std::string amount;
        bool synced = false;
        std::string sync_error;
        std::string txid;
        std::string unshielded_balance_before;
        std::string dust_balance_before;
        std::string error;
    };

    OfficialAddressResult generate_official_wallet_addresses(
        const std::string &network = "preprod",
        const std::string &seed_hex = "",
        uint32_t account = 0,
        uint32_t address_index = 0,
        bool register_dust = false,
        const std::string &proof_server = "",
        uint32_t sync_timeout_seconds = 120,
        const std::string &bridge_script = "tools/official-wallet-address.mjs");

    OfficialAddressResult generate_official_unshielded_address(
        const std::string &network = "preprod",
        const std::string &seed_hex = "",
        uint32_t account = 0,
        uint32_t address_index = 0,
        const std::string &bridge_script = "tools/official-wallet-address.mjs");

    OfficialTransferResult transfer_official_night(
        const std::string &seed_hex,
        const std::string &to_address,
        const std::string &amount,
        const std::string &network = "preprod",
        const std::string &proof_server = "",
        uint32_t sync_timeout_seconds = 120,
        const std::string &bridge_script = "tools/official-transfer-night.mjs");
}

extern "C"
{
    int midnight_generate_official_unshielded_address(
        const char *network,
        const char *seed_hex,
        char *out_address,
        size_t out_address_capacity,
        char *out_seed_hex,
        size_t out_seed_hex_capacity,
        char *out_error,
        size_t out_error_capacity);

    int midnight_transfer_official_night(
        const char *network,
        const char *seed_hex,
        const char *to_address,
        const char *amount,
        const char *proof_server,
        uint32_t sync_timeout_seconds,
        char *out_txid,
        size_t out_txid_capacity,
        char *out_error,
        size_t out_error_capacity);
}
