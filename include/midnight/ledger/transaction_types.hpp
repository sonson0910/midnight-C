#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace midnight::ledger
{
    enum class SourceMode
    {
        Auto,       ///< SDK chooses the safest production mode for the operation.
        LocalCache, ///< Use an already-synced local ledger/wallet cache only.
        ColdSync,   ///< Allow the ledger backend to fetch source history from RPC.
        Bounded     ///< Use an explicit bounded block/transaction source.
    };

    inline std::string source_mode_to_string(SourceMode mode)
    {
        switch (mode)
        {
        case SourceMode::LocalCache:
            return "local-cache";
        case SourceMode::ColdSync:
            return "cold-sync";
        case SourceMode::Bounded:
            return "bounded";
        case SourceMode::Auto:
        default:
            return "auto";
        }
    }

    inline SourceMode source_mode_from_string(std::string value)
    {
        for (auto &c : value)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
            else if (c == '_')
            {
                c = '-';
            }
        }
        if (value == "local" || value == "local-cache" || value == "cache-only")
        {
            return SourceMode::LocalCache;
        }
        if (value == "cold" || value == "cold-sync" || value == "rpc")
        {
            return SourceMode::ColdSync;
        }
        if (value == "bounded" || value == "range")
        {
            return SourceMode::Bounded;
        }
        return SourceMode::Auto;
    }

    struct SourceConfig
    {
        SourceMode source_mode = SourceMode::Auto;
        std::string src_url;
        std::vector<std::string> src_files;
        bool fetch_only_cached = false;
        bool require_ledger_state_cache = true;
        bool ignore_block_context = false;
        bool dust_warp = false;
        std::string fetch_cache = "redb:midnight_cache/fetch_cache.db";
        std::string ledger_state_db = "midnight_cache/ledger_cache_db";
        uint32_t fetch_concurrency = 4;
        std::optional<uint32_t> fetch_compute_concurrency;
        uint32_t fetch_retry_count = 3;
        uint64_t fetch_retry_delay_ms = 5000;
        std::optional<uint64_t> from_block; ///< Optional bounded source start
        std::optional<uint64_t> to_block;   ///< Optional bounded source end
        std::vector<std::string> transaction_hashes; ///< Optional source tx filter
    };

    struct BuildResult
    {
        bool success = false;
        std::vector<uint8_t> transaction_bytes;
        std::string transaction_hex;
        std::string transaction_hash;
        std::string contract_address;
        std::string output_file;
        std::string intent_path;
        std::string private_state_path;
        std::string zswap_state_path;
        std::string onchain_state_path;
        std::string result_path;
        std::string raw_output;
        std::string command;
        std::string log;
        std::string error;
    };

    struct SyncLedgerStateParams
    {
        SourceConfig source;
        std::vector<std::string> wallet_seeds;
        uint64_t timeout_ms = 0; ///< 0 means no backend timeout
    };

    struct WalletSummaryParams
    {
        SourceConfig source;
        std::string wallet_seed;
        uint64_t timeout_ms = 0; ///< 0 means no backend timeout
    };

    struct IntentResult
    {
        bool success = false;
        std::string output_intent;
        std::string output_private_state;
        std::string output_zswap_state;
        std::optional<std::string> output_onchain_state;
        std::optional<std::string> output_result;
        std::string command;
        std::string log;
        std::string error;
    };

    struct TransferNightParams
    {
        SourceConfig source;
        std::optional<std::string> proof_server; ///< optional remote prover URL
        std::string source_seed;
        std::optional<std::string> funding_seed;
        std::vector<std::string> destination_addresses;
        std::string amount; ///< u128 decimal string in the ledger's smallest NIGHT unit
        std::string token_type =
            "0000000000000000000000000000000000000000000000000000000000000000";
        std::vector<std::string> input_utxos; ///< "<intent_hash>#<output_index>"
        std::optional<std::string> rng_seed;  ///< 32-byte hex
        std::string coin_selection = "largest-first";
    };

    struct RegisterDustParams
    {
        SourceConfig source;
        std::optional<std::string> proof_server; ///< optional remote prover URL
        std::string wallet_seed;
        std::optional<std::string> funding_seed;
        std::optional<std::string> destination_dust_address;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct DeregisterDustParams
    {
        SourceConfig source;
        std::optional<std::string> proof_server; ///< optional remote prover URL
        std::string wallet_seed;
        std::string funding_seed;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct SimpleContractDeployParams
    {
        SourceConfig source;
        std::optional<std::string> proof_server; ///< optional remote prover URL
        std::string funding_seed;
        std::vector<std::string> authority_seeds;
        std::optional<uint32_t> authority_threshold;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct SimpleContractCallParams
    {
        SourceConfig source;
        std::optional<std::string> proof_server; ///< optional remote prover URL
        std::string funding_seed;
        std::string contract_address; ///< 32-byte contract hex, no Bech32 wallet address
        std::string call_key = "store";
        std::string fee = "1300000"; ///< u128 decimal string
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct CustomContractIntentParams
    {
        SourceConfig source;
        std::optional<std::string> proof_server; ///< optional remote prover URL
        std::string funding_seed;
        std::vector<std::string> compiled_contract_dirs;
        std::vector<std::string> intent_files;
        std::vector<std::string> input_utxos;
        std::optional<std::string> zswap_state_file;
        std::vector<std::string> shielded_destinations;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

} // namespace midnight::ledger
