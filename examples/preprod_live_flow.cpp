#include "midnight/production/midnight_client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace
{
    std::string env_value(const char *name, std::string fallback = {})
    {
        const char *value = std::getenv(name);
        return value && *value ? std::string(value) : std::move(fallback);
    }

    std::string require_env(const char *name)
    {
        auto value = env_value(name);
        if (value.empty())
        {
            throw std::runtime_error(std::string("Missing required env var: ") + name);
        }
        return value;
    }

    uint64_t env_u64(const char *name, uint64_t fallback)
    {
        auto value = env_value(name);
        return value.empty() ? fallback : std::stoull(value);
    }

    bool env_bool(const char *name, bool fallback = false)
    {
        auto value = env_value(name);
        if (value.empty())
        {
            return fallback;
        }
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }

    std::optional<uint64_t> env_optional_u64(const char *name)
    {
        auto value = env_value(name);
        if (value.empty())
        {
            return std::nullopt;
        }
        for (char c : value)
        {
            if (c != ',')
            {
                continue;
            }
            value.erase(std::remove(value.begin(), value.end(), ','), value.end());
            break;
        }
        return std::stoull(value);
    }

    midnight::production::ClientConfig client_config()
    {
        const auto network = env_value("MIDNIGHT_NETWORK", "preview");
        midnight::production::ClientConfig config;
        config.network_name = network;
        config.node_url = env_value(
            "MIDNIGHT_LIVE_NODE_URL",
            env_value("MIDNIGHT_NODE_URL", "https://rpc." + network + ".midnight.network"));
        config.indexer_url = env_value(
            "MIDNIGHT_LIVE_INDEXER_URL",
            env_value(
                "MIDNIGHT_INDEXER_URL",
                "https://indexer." + network + ".midnight.network/api/v4/graphql"));
        config.proof_server_url = env_value("MIDNIGHT_LIVE_PROOF_SERVER_URL", "http://127.0.0.1:6300");
        config.ledger_ffi_library = require_env("MIDNIGHT_LEDGER_FFI_LIBRARY");
        config.node_timeout_ms = static_cast<uint32_t>(env_u64("MIDNIGHT_LIVE_NODE_TIMEOUT_MS", 30000));
        config.proof_timeout_ms = static_cast<uint32_t>(env_u64("MIDNIGHT_LIVE_PROOF_TIMEOUT_MS", 300000));
        return config;
    }

    midnight::ledger::SourceConfig source_config()
    {
        const auto network = env_value("MIDNIGHT_NETWORK", "preview");
        midnight::ledger::SourceConfig source;
        source.src_url = env_value(
            "MIDNIGHT_LIVE_SOURCE_URL",
            env_value("MIDNIGHT_SOURCE_URL", "wss://rpc." + network + ".midnight.network"));
        source.source_mode = midnight::ledger::source_mode_from_string(
            env_value("MIDNIGHT_LIVE_SOURCE_MODE", "auto"));
        source.fetch_only_cached = env_bool("MIDNIGHT_LIVE_FETCH_ONLY_CACHED", false);
        source.require_ledger_state_cache =
            env_bool("MIDNIGHT_LIVE_REQUIRE_LEDGER_STATE_CACHE", true);
        source.ignore_block_context = env_bool("MIDNIGHT_LIVE_IGNORE_BLOCK_CONTEXT", false);
        source.dust_warp = env_bool("MIDNIGHT_LIVE_DUST_WARP", true);
        source.fetch_cache = env_value(
            "MIDNIGHT_LIVE_FETCH_CACHE",
            "redb:midnight_cache/live_submit_fetch_cache.db");
        source.ledger_state_db = env_value(
            "MIDNIGHT_LIVE_LEDGER_STATE_DB",
            "midnight_cache/live_submit_ledger_state_db");
        source.fetch_concurrency = static_cast<uint32_t>(
            env_u64("MIDNIGHT_LIVE_FETCH_CONCURRENCY", 4));
        const auto compute_concurrency = env_value("MIDNIGHT_LIVE_FETCH_COMPUTE_CONCURRENCY");
        if (!compute_concurrency.empty())
        {
            source.fetch_compute_concurrency =
                static_cast<uint32_t>(std::stoul(compute_concurrency));
        }
        source.fetch_retry_count = static_cast<uint32_t>(
            env_u64("MIDNIGHT_LIVE_FETCH_RETRY_COUNT", 5));
        source.fetch_retry_delay_ms = env_u64("MIDNIGHT_LIVE_FETCH_RETRY_DELAY_MS", 5000);
        if (auto from_block = env_optional_u64("MIDNIGHT_LIVE_SOURCE_FROM_BLOCK"))
        {
            source.from_block = *from_block;
        }
        if (auto to_block = env_optional_u64("MIDNIGHT_LIVE_SOURCE_TO_BLOCK"))
        {
            source.to_block = *to_block;
        }
        if (auto tx_hash = env_value("MIDNIGHT_LIVE_SOURCE_TX_HASH"); !tx_hash.empty())
        {
            source.transaction_hashes.push_back(tx_hash);
        }
        else if (auto funding_tx = env_value("MIDNIGHT_LIVE_FUNDING_TX_HASH"); !funding_tx.empty())
        {
            source.transaction_hashes.push_back(funding_tx);
        }
        return source;
    }

    bool is_bounded_source(const midnight::ledger::SourceConfig &source)
    {
        return source.from_block.has_value() || source.to_block.has_value();
    }

    void configure_build_source_for_safety(midnight::ledger::SourceConfig &source)
    {
        if (!env_bool("MIDNIGHT_LIVE_ALLOW_COLD_SYNC", false) && !is_bounded_source(source))
        {
            source.source_mode = midnight::ledger::SourceMode::LocalCache;
            source.fetch_only_cached = true;
            return;
        }
        if (source.source_mode == midnight::ledger::SourceMode::Auto && !is_bounded_source(source))
        {
            source.source_mode = midnight::ledger::SourceMode::ColdSync;
        }
    }

    void print_build_result(const midnight::ledger::BuildResult &result)
    {
        std::cout << "success=" << result.success << "\n";
        std::cout << "transaction_hash=" << result.transaction_hash << "\n";
        std::cout << "transaction_hex_size=" << result.transaction_hex.size() << "\n";
        std::cout << "command=" << result.command << "\n";
        std::cout << "log=" << result.log << "\n";
        if (!result.raw_output.empty())
        {
            std::cout << "raw_output=" << result.raw_output << "\n";
        }
        if (!result.error.empty())
        {
            std::cerr << "error=" << result.error << "\n";
        }
    }

    bool has_cache_files(const std::string &path)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec))
        {
            return false;
        }
        if (std::filesystem::is_regular_file(path, ec))
        {
            return std::filesystem::file_size(path, ec) > 0;
        }
        if (!std::filesystem::is_directory(path, ec))
        {
            return false;
        }
        for (const auto &entry : std::filesystem::recursive_directory_iterator(path, ec))
        {
            if (ec)
            {
                return false;
            }
            if (entry.is_regular_file(ec) && entry.file_size(ec) > 0)
            {
                return true;
            }
        }
        return false;
    }

    bool should_fail_for_unsynced_cache(const midnight::ledger::SourceConfig &source)
    {
        const bool local_cache_mode =
            source.source_mode == midnight::ledger::SourceMode::LocalCache ||
            source.fetch_only_cached;
        return source.require_ledger_state_cache && local_cache_mode &&
               !is_bounded_source(source) && !has_cache_files(source.ledger_state_db);
    }

    midnight::production::PipelineOptions pipeline_options(const std::string &wallet_id)
    {
        midnight::production::PipelineOptions options;
        options.artifacts.root_dir = env_value("MIDNIGHT_LIVE_ARTIFACT_DIR", "midnight-artifacts");
        options.artifacts.network = env_value("MIDNIGHT_NETWORK", "preview");
        options.artifacts.wallet_id = env_value("MIDNIGHT_LIVE_WALLET_ID", wallet_id);
        options.confirmation.timeout =
            std::chrono::milliseconds(env_u64("MIDNIGHT_LIVE_CONFIRMATION_TIMEOUT_MS", 300000));
        options.confirmation.poll_interval =
            std::chrono::milliseconds(env_u64("MIDNIGHT_LIVE_CONFIRMATION_POLL_MS", 5000));
        return options;
    }

    int print_result(const midnight::production::PipelineResult &result)
    {
        std::cout << "build.success=" << result.transaction.build.success << "\n";
        std::cout << "tx_hash=" << result.transaction.build.transaction_hash << "\n";
        std::cout << "extrinsic_hash=" << result.transaction.submit.extrinsic_hash << "\n";
        std::cout << "submit.success=" << result.transaction.submit.success << "\n";
        std::cout << "confirmation.success=" << result.confirmation.success << "\n";
        std::cout << "block_height=" << result.confirmation.block_height << "\n";
        std::cout << "block_hash=" << result.confirmation.block_hash << "\n";
        std::cout << "artifacts=" << result.artifacts.directory.string() << "\n";

        if (!result.success)
        {
            std::cerr << "error=" << result.error.message << "\n";
            if (!result.error.detail.empty())
            {
                std::cerr << "detail=" << result.error.detail << "\n";
            }
            if (!result.transaction.build.error.empty())
            {
                std::cerr << "build_error=" << result.transaction.build.error << "\n";
            }
            if (!result.transaction.submit.error.empty())
            {
                std::cerr << "submit_error=" << result.transaction.submit.error << "\n";
            }
            return 1;
        }
        return 0;
    }

    int register_dust()
    {
        auto config = client_config();
        midnight::production::MidnightClient client(config);

        midnight::ledger::RegisterDustParams params;
        params.source = source_config();
        configure_build_source_for_safety(params.source);
        if (should_fail_for_unsynced_cache(params.source))
        {
            std::cerr << "error=Ledger state cache is not synced. Run `tools/midnight-preprod-live.sh sync-ledger-state` first, or set MIDNIGHT_LIVE_ALLOW_COLD_SYNC=1 to opt into a long cold sync inside register-dust.\n";
            return 1;
        }
        params.wallet_seed = require_env("MIDNIGHT_LIVE_SOURCE_SEED");
        const auto funding_seed = env_value("MIDNIGHT_LIVE_FUNDING_SEED");
        if (!funding_seed.empty())
        {
            params.funding_seed = funding_seed;
        }
        const auto dust_address = env_value("MIDNIGHT_LIVE_DUST_ADDRESS");
        if (!dust_address.empty())
        {
            params.destination_dust_address = dust_address;
        }

        return print_result(client.register_dust(
            pipeline_options(env_value("MIDNIGHT_NETWORK", "preview") + "-register-dust"),
            params));
    }

    int sync_ledger_state()
    {
        auto config = client_config();
        midnight::production::MidnightClient client(config);

        midnight::ledger::SyncLedgerStateParams params;
        params.source = source_config();
        params.wallet_seeds.push_back(require_env("MIDNIGHT_LIVE_SOURCE_SEED"));
        const auto funding_seed = env_value("MIDNIGHT_LIVE_FUNDING_SEED");
        if (!funding_seed.empty() && funding_seed != params.wallet_seeds.front())
        {
            params.wallet_seeds.push_back(funding_seed);
        }
        params.timeout_ms = env_u64("MIDNIGHT_LIVE_SYNC_TIMEOUT_MS", 0);

        const auto result = client.sync_ledger_state(params);
        print_build_result(result);
        return result.success ? 0 : 1;
    }

    int transfer_night()
    {
        auto config = client_config();
        midnight::production::MidnightClient client(config);

        midnight::ledger::TransferNightParams params;
        params.source = source_config();
        configure_build_source_for_safety(params.source);
        if (should_fail_for_unsynced_cache(params.source))
        {
            std::cerr << "error=Ledger state cache is not synced. Run `tools/midnight-preprod-live.sh sync-ledger-state` first, or set MIDNIGHT_LIVE_ALLOW_COLD_SYNC=1 to opt into a long cold sync inside transfer-night.\n";
            return 1;
        }
        params.source_seed = require_env("MIDNIGHT_LIVE_SOURCE_SEED");
        const auto funding_seed = env_value("MIDNIGHT_LIVE_FUNDING_SEED");
        if (!funding_seed.empty())
        {
            params.funding_seed = funding_seed;
        }
        params.destination_addresses = {
            require_env("MIDNIGHT_LIVE_DESTINATION_ADDRESS")};
        params.amount = env_value("MIDNIGHT_LIVE_AMOUNT", "1000000");

        return print_result(client.transfer_night(
            pipeline_options(env_value("MIDNIGHT_NETWORK", "preview") + "-transfer-night"),
            params));
    }

    int balance()
    {
        auto config = client_config();
        midnight::production::MidnightClient client(config);
        const auto address = require_env("MIDNIGHT_LIVE_NIGHT_ADDRESS");
        const auto source_mode = env_value("MIDNIGHT_LIVE_BALANCE_SOURCE", "local-cache");

        midnight::network::WalletState state;
        midnight::network::IndexerClient indexer(config.indexer_url, config.node_timeout_ms);
        const auto funding_tx = env_value("MIDNIGHT_LIVE_FUNDING_TX_HASH");
        const auto funding_block = env_optional_u64("MIDNIGHT_LIVE_FUNDING_BLOCK");
        if (source_mode == "local-cache" || source_mode == "cache")
        {
            midnight::ledger::WalletSummaryParams params;
            params.source = source_config();
            configure_build_source_for_safety(params.source);
            if (should_fail_for_unsynced_cache(params.source))
            {
                std::cerr << "error=Ledger state cache is not synced. Run `tools/midnight-preprod-live.sh sync-ledger-state` first, or set MIDNIGHT_LIVE_BALANCE_SOURCE=funding-tx for the old faucet transaction debug path.\n";
                return 1;
            }
            params.wallet_seed = require_env("MIDNIGHT_LIVE_SOURCE_SEED");
            params.timeout_ms = env_u64("MIDNIGHT_LIVE_BALANCE_TIMEOUT_MS", 60000);

            const auto summary = client.query_cached_wallet_summary(params);
            const auto spendable_night = summary.value("spendable_night_balance", "0");
            const auto shielded_night = summary.value("shielded_night_balance", "0");
            const auto registered_night = summary.value("registered_night_balance", "0");
            const auto generated_dust = summary.value("generated_dust_balance", "0");
            const auto dust_capacity = summary.value("generated_dust_capacity", "0");

            std::cout << "address=" << address << "\n";
            std::cout << "night_balance=" << spendable_night << "\n";
            std::cout << "spendable_night_balance=" << spendable_night << "\n";
            std::cout << "registered_night_balance=" << registered_night << "\n";
            std::cout << "shielded_night_balance=" << shielded_night << "\n";
            std::cout << "dust_balance=" << generated_dust << "\n";
            std::cout << "dust_capacity=" << dust_capacity << "\n";
            std::cout << "utxo_count=" << summary.value("utxo_count", 0u) << "\n";
            std::cout << "dust_utxo_count=" << summary.value("dust_utxo_count", 0u) << "\n";
            std::cout << "shielded_coin_count=" << summary.value("shielded_coin_count", 0u) << "\n";
            std::cout << "ledger_cache_height=" << summary.value("latest_source_height", 0u) << "\n";
            std::cout << "balance_source=local-ledger-cache\n";
            const auto dust_error = summary.value("dust_error", "");
            if (!dust_error.empty())
            {
                std::cerr << "dust_warning=" << dust_error << "\n";
            }
            return 0;
        }

        if (source_mode == "funding-tx" && !funding_tx.empty())
        {
            state = indexer.query_wallet_state_from_transaction(address, funding_tx);
        }
        else if (source_mode == "funding-block" && funding_block)
        {
            state = indexer.query_wallet_state(
                address,
                static_cast<uint32_t>(*funding_block),
                static_cast<uint32_t>(*funding_block));
        }
        else if (source_mode == "funding-tx" || source_mode == "funding-block")
        {
            std::cerr << "error=MIDNIGHT_LIVE_BALANCE_SOURCE='" << source_mode
                      << "' requires the matching MIDNIGHT_LIVE_FUNDING_TX_HASH or MIDNIGHT_LIVE_FUNDING_BLOCK value.\n";
            return 1;
        }
        else if (source_mode != "wallet-scan" && source_mode != "indexer-scan")
        {
            std::cerr << "error=Unknown MIDNIGHT_LIVE_BALANCE_SOURCE='" << source_mode
                      << "'. Use local-cache, funding-tx, funding-block, or indexer-scan.\n";
            return 1;
        }
        else
        {
            state = client.query_wallet_state(address);
        }

        std::cout << "address=" << state.address << "\n";
        std::cout << "night_balance=" << state.unshielded_balance << "\n";
        std::cout << "spendable_night_balance=" << state.unshielded_balance << "\n";
        std::cout << "dust_balance=" << state.dust_balance << "\n";
        std::cout << "utxo_count=" << state.available_utxo_count << "\n";
        std::cout << "dust_registered_utxo_count=" << state.dust_registered_utxo_count << "\n";
        if (source_mode == "funding-tx" && !funding_tx.empty())
        {
            std::cout << "balance_source=transaction:" << funding_tx << "\n";
        }
        else if (source_mode == "funding-block" && funding_block)
        {
            std::cout << "balance_source=block:" << *funding_block << "\n";
        }
        else
        {
            std::cout << "balance_source=wallet-scan\n";
        }
        if (!state.error.empty())
        {
            std::cerr << "error=" << state.error << "\n";
            return 1;
        }
        return 0;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: preprod_live_flow <balance|sync-ledger-state|register-dust|transfer-night|both>\n";
        return 2;
    }

    try
    {
        const std::string action = argv[1];
        if (action == "balance")
        {
            return balance();
        }
        if (action == "sync-ledger-state")
        {
            return sync_ledger_state();
        }
        if (action == "register-dust")
        {
            return register_dust();
        }
        if (action == "transfer-night")
        {
            return transfer_night();
        }
        if (action == "both")
        {
            const int dust_code = register_dust();
            if (dust_code != 0)
            {
                return dust_code;
            }
            return transfer_night();
        }
        std::cerr << "Unknown action: " << action << "\n";
        return 2;
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal=" << e.what() << "\n";
        return 1;
    }
}
