#include "midnight/production/midnight_client.hpp"
#include <iostream>

int main()
{
    using midnight::ledger::SourceConfig;
    using midnight::ledger::ToolkitConfig;
    using midnight::ledger::TransferNightParams;
    using midnight::production::ClientConfig;
    using midnight::production::MidnightClient;
    using midnight::production::PipelineOptions;

    ClientConfig client_cfg;
    client_cfg.node_url = "https://rpc.preprod.midnight.network";
    client_cfg.indexer_url = "https://indexer.preprod.midnight.network/api/v4/graphql";
    client_cfg.proof_server_url = "http://127.0.0.1:6300";

    ToolkitConfig toolkit;
    toolkit.toolkit_binary = "midnight-node-toolkit";
    toolkit.proof_server_url = client_cfg.proof_server_url;

    SourceConfig source;
    source.src_url = "wss://rpc.preprod.midnight.network";

    TransferNightParams transfer;
    transfer.source = source;
    transfer.source_seed = "<wallet-seed-hex-or-toolkit-seed>";
    transfer.destination_addresses = {"mn_addr_preprod1..."};
    transfer.amount = "1000000000";

    MidnightClient client(client_cfg);

    PipelineOptions options;
    options.toolkit = toolkit;
    options.artifacts.root_dir = "midnight-artifacts";
    options.artifacts.network = "preprod";
    options.artifacts.wallet_id = "example-wallet";
    options.wait_for_confirmation = true;

    // This builds the tagged midnight-ledger Transaction through the official
    // toolkit, wraps it as Midnight::send_mn_transaction, submits it via
    // author_submitExtrinsic, saves artifacts, and waits for the indexer.
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
