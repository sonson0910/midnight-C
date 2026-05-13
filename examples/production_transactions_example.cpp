#include "midnight/production/midnight_client.hpp"
#include <iostream>

int main()
{
    using midnight::ledger::SourceConfig;
    using midnight::ledger::ToolkitConfig;
    using midnight::ledger::TransferNightParams;
    using midnight::production::ClientConfig;
    using midnight::production::MidnightClient;

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

    // This builds the tagged midnight-ledger Transaction through the official
    // toolkit, wraps it as Midnight::send_mn_transaction, and submits it via
    // author_submitExtrinsic.
    auto result = client.transfer_night(toolkit, transfer);
    if (!result.success)
    {
        std::cerr << result.error << "\n";
        return 1;
    }

    std::cout << result.submit.extrinsic_hash << "\n";
    return 0;
}
