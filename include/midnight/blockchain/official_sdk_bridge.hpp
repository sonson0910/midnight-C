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

    struct OfficialWalletStateResult
    {
        bool success = false;
        std::string network;
        std::string source_address;
        bool synced = false;
        std::string sync_error;
        std::string unshielded_balance;
        std::string pending_unshielded_balance;
        std::string dust_balance;
        std::string dust_balance_error;
        std::string dust_pending_balance;
        uint64_t dust_available_coins = 0;
        uint64_t dust_pending_coins = 0;
        uint64_t available_utxo_count = 0;
        uint64_t registered_utxo_count = 0;
        uint64_t unregistered_utxo_count = 0;
        uint64_t pending_utxo_count = 0;
        bool likely_dust_designation_lock = false;
        bool likely_pending_utxo_lock = false;
        std::string error;
    };

    struct OfficialIndexerSyncResult
    {
        bool success = false;
        std::string network;
        std::string source_address;
        std::string sync_error;
        std::string indexer_url;
        std::string indexer_ws_url;
        bool facade_is_synced = false;
        bool shielded_connected = false;
        bool unshielded_connected = false;
        bool dust_connected = false;
        bool shielded_complete_within_50 = false;
        bool unshielded_complete_within_50 = false;
        bool dust_complete_within_50 = false;
        bool all_connected = false;
        std::string shielded_apply_lag;
        std::string unshielded_apply_lag;
        std::string dust_apply_lag;
        std::string error;
    };

    struct OfficialContractDeployResult
    {
        bool success = false;
        std::string network;
        std::string contract;
        std::string contract_export;
        std::string contract_module;
        std::string seed_hex;
        std::string source_address;
        std::string fee_bps;
        std::string constructor_nonce_hex;
        std::string contract_address;
        std::string txid;
        std::string unshielded_balance;
        std::string dust_balance;
        bool dust_registration_attempted = false;
        bool dust_registration_success = false;
        uint64_t dust_registration_registered_utxos = 0;
        std::string dust_registration_message;
        std::string endpoints_node_url;
        std::string endpoints_indexer_url;
        std::string endpoints_indexer_ws_url;
        std::string endpoints_proof_server;
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

    bool register_wallet_seed_hex(
        const std::string &wallet_alias,
        const std::string &seed_hex,
        const std::string &network = "",
        const std::string &wallet_store_dir = "",
        std::string *error = nullptr);

    OfficialTransferResult transfer_official_night_with_wallet(
        const std::string &wallet_alias,
        const std::string &to_address,
        const std::string &amount,
        const std::string &network = "preprod",
        const std::string &proof_server = "",
        uint32_t sync_timeout_seconds = 120,
        const std::string &wallet_store_dir = "",
        const std::string &bridge_script = "tools/official-transfer-night.mjs");

    OfficialWalletStateResult query_official_wallet_state(
        const std::string &seed_hex,
        const std::string &network = "preprod",
        const std::string &proof_server = "",
        const std::string &node_url = "",
        const std::string &indexer_url = "",
        const std::string &indexer_ws_url = "",
        uint32_t sync_timeout_seconds = 120,
        const std::string &bridge_script = "tools/official-transfer-night.mjs");

    OfficialWalletStateResult query_official_wallet_state_with_wallet(
        const std::string &wallet_alias,
        const std::string &network = "preprod",
        const std::string &proof_server = "",
        const std::string &node_url = "",
        const std::string &indexer_url = "",
        const std::string &indexer_ws_url = "",
        uint32_t sync_timeout_seconds = 120,
        const std::string &wallet_store_dir = "",
        const std::string &bridge_script = "tools/official-transfer-night.mjs");

    OfficialIndexerSyncResult query_official_indexer_sync_status(
        const std::string &seed_hex,
        const std::string &network = "preprod",
        const std::string &proof_server = "",
        const std::string &node_url = "",
        const std::string &indexer_url = "",
        const std::string &indexer_ws_url = "",
        uint32_t sync_timeout_seconds = 120,
        const std::string &bridge_script = "tools/official-transfer-night.mjs");

    OfficialIndexerSyncResult query_official_indexer_sync_status_with_wallet(
        const std::string &wallet_alias,
        const std::string &network = "preprod",
        const std::string &proof_server = "",
        const std::string &node_url = "",
        const std::string &indexer_url = "",
        const std::string &indexer_ws_url = "",
        uint32_t sync_timeout_seconds = 120,
        const std::string &wallet_store_dir = "",
        const std::string &bridge_script = "tools/official-transfer-night.mjs");

    OfficialContractDeployResult deploy_official_compact_contract(
        const std::string &contract = "FaucetAMM",
        const std::string &network = "undeployed",
        const std::string &seed_hex = "",
        uint64_t fee_bps = 10,
        const std::string &initial_nonce_hex = "",
        const std::string &proof_server = "",
        const std::string &node_url = "",
        const std::string &indexer_url = "",
        const std::string &indexer_ws_url = "",
        uint32_t sync_timeout_seconds = 120,
        uint32_t dust_wait_seconds = 120,
        uint32_t deploy_timeout_seconds = 300,
        uint32_t dust_utxo_limit = 1,
        const std::string &contract_module = "",
        const std::string &contract_export = "",
        const std::string &private_state_store = "",
        const std::string &private_state_id = "",
        const std::string &constructor_args_json = "",
        const std::string &constructor_args_file = "",
        const std::string &zk_config_path = "",
        const std::string &bridge_script = "tools/official-deploy-contract.mjs");

    OfficialContractDeployResult deploy_official_compact_contract_with_wallet(
        const std::string &wallet_alias,
        const std::string &contract = "FaucetAMM",
        const std::string &network = "undeployed",
        uint64_t fee_bps = 10,
        const std::string &initial_nonce_hex = "",
        const std::string &proof_server = "",
        const std::string &node_url = "",
        const std::string &indexer_url = "",
        const std::string &indexer_ws_url = "",
        uint32_t sync_timeout_seconds = 120,
        uint32_t dust_wait_seconds = 120,
        uint32_t deploy_timeout_seconds = 300,
        uint32_t dust_utxo_limit = 1,
        const std::string &contract_module = "",
        const std::string &contract_export = "",
        const std::string &private_state_store = "",
        const std::string &private_state_id = "",
        const std::string &constructor_args_json = "",
        const std::string &constructor_args_file = "",
        const std::string &zk_config_path = "",
        const std::string &wallet_store_dir = "",
        const std::string &bridge_script = "tools/official-deploy-contract.mjs");
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

    int midnight_query_official_indexer_sync_status(
        const char *network,
        const char *seed_hex,
        const char *proof_server,
        const char *node_url,
        const char *indexer_url,
        const char *indexer_ws_url,
        uint32_t sync_timeout_seconds,
        int *out_facade_synced,
        int *out_all_connected,
        char *out_source_address,
        size_t out_source_address_capacity,
        char *out_sync_error,
        size_t out_sync_error_capacity,
        char *out_diagnostics_json,
        size_t out_diagnostics_json_capacity,
        char *out_error,
        size_t out_error_capacity);

    int midnight_query_official_wallet_state(
        const char *network,
        const char *seed_hex,
        const char *proof_server,
        const char *node_url,
        const char *indexer_url,
        const char *indexer_ws_url,
        uint32_t sync_timeout_seconds,
        int *out_synced,
        uint64_t *out_available_utxo_count,
        uint64_t *out_registered_utxo_count,
        char *out_source_address,
        size_t out_source_address_capacity,
        char *out_state_json,
        size_t out_state_json_capacity,
        char *out_error,
        size_t out_error_capacity);

    int midnight_deploy_official_compact_contract(
        const char *contract,
        const char *network,
        const char *seed_hex,
        uint64_t fee_bps,
        const char *proof_server,
        const char *node_url,
        const char *indexer_url,
        const char *indexer_ws_url,
        uint32_t sync_timeout_seconds,
        uint32_t dust_wait_seconds,
        uint32_t deploy_timeout_seconds,
        char *out_contract_address,
        size_t out_contract_address_capacity,
        char *out_txid,
        size_t out_txid_capacity,
        char *out_deploy_json,
        size_t out_deploy_json_capacity,
        char *out_error,
        size_t out_error_capacity);
}
