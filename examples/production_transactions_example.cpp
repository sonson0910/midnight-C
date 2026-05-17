#include "midnight/production/midnight_client.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
std::string env_or(const char *name, std::string fallback)
{
    if (const char *value = std::getenv(name))
    {
        if (*value)
        {
            return value;
        }
    }
    return fallback;
}
}

int main()
{
    using midnight::ledger::SourceConfig;
    using midnight::ledger::TransferNightParams;
    using midnight::production::ClientConfig;
    using midnight::production::MidnightClient;
    using midnight::production::PipelineOptions;

    const std::string network = env_or("MIDNIGHT_NETWORK", "preview");

    auto client_cfg = ClientConfig::from_environment();

    SourceConfig source;
    source.src_url = env_or(
        "MIDNIGHT_SOURCE_URL",
        "wss://rpc." + network + ".midnight.network");
    source.fetch_cache = env_or("MIDNIGHT_FETCH_CACHE", "redb:midnight_cache/fetch_cache.db");
    source.ledger_state_db = env_or("MIDNIGHT_LEDGER_STATE_DB", "midnight_cache/ledger_cache_db");
    source.source_mode = midnight::ledger::source_mode_from_string(
        env_or("MIDNIGHT_SOURCE_MODE", "local-cache"));
    source.fetch_only_cached = env_or("MIDNIGHT_FETCH_ONLY_CACHED", "1") == "1";

    TransferNightParams transfer;
    transfer.source = source;
    transfer.source_seed = env_or("MIDNIGHT_SOURCE_SEED", "");
    transfer.destination_addresses = {env_or("MIDNIGHT_DESTINATION_ADDRESS", "")};
    transfer.amount = env_or("MIDNIGHT_AMOUNT", "1000000");

    if (transfer.source_seed.empty() || transfer.destination_addresses.front().empty())
    {
        std::cerr << "Set MIDNIGHT_SOURCE_SEED and MIDNIGHT_DESTINATION_ADDRESS before running this example.\n";
        return 2;
    }

    MidnightClient client(client_cfg);

    const auto state = client.state_status(source);
    if (!state.ready && source.source_mode == midnight::ledger::SourceMode::LocalCache)
    {
        std::cerr
            << "Local ledger state cache is not ready: "
            << state.ledger_state_path
            << "\nRun sync_ledger_state, import a state snapshot, or set MIDNIGHT_SOURCE_MODE=cold-sync explicitly.\n";
        return 3;
    }

    PipelineOptions options;
    options.artifacts.root_dir = "midnight-artifacts";
    options.artifacts.network = network;
    options.artifacts.wallet_id = env_or("MIDNIGHT_WALLET_ID", "example-wallet");
    options.wait_for_confirmation = true;

    // This builds via the configured native ledger FFI backend, wraps the
    // serialized Midnight transaction as Midnight::send_mn_transaction,
    // submits it through author_submitExtrinsic, saves artifacts, and waits
    // for the indexer.
    auto result = client.transfer_night(options, transfer);
    if (!result.success)
    {
        std::cerr << result.error.message << " " << result.error.detail << "\n";
        return 1;
    }

    std::cout << result.transaction.submit.extrinsic_hash << "\n";
    std::cout << result.confirmation.block_height << "\n";
    return 0;
}
