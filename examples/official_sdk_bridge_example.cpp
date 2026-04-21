#include <iostream>
#include <string>
#include <cstdint>

#include "midnight/blockchain/official_sdk_bridge.hpp"

namespace
{
    void print_usage()
    {
        std::cout << "Usage:\n";
        std::cout << "  official_sdk_bridge_example [network] [seed_hex]\n";
        std::cout << "  official_sdk_bridge_example wallet-add <wallet_alias> <seed_hex> [network] [wallet_store_dir]\n";
        std::cout << "  official_sdk_bridge_example transfer-wallet <network> <wallet_alias> <to_address> <amount> [proof_server] [sync_timeout] [wallet_store_dir]\n";
        std::cout << "  official_sdk_bridge_example state-wallet <network> <wallet_alias> [proof_server] [sync_timeout] [wallet_store_dir]\n";
        std::cout << "  official_sdk_bridge_example indexer-wallet <network> <wallet_alias> [proof_server] [sync_timeout] [wallet_store_dir]\n";
        std::cout << "  official_sdk_bridge_example deploy-wallet <network> <wallet_alias> <contract> [proof_server] [wallet_store_dir]\n";
        std::cout << "  official_sdk_bridge_example transfer <network> <seed_hex> <to_address> <amount> [proof_server] [sync_timeout]\n";
        std::cout << "  official_sdk_bridge_example state <network> <seed_hex> [proof_server] [sync_timeout]\n";
        std::cout << "  official_sdk_bridge_example indexer <network> <seed_hex> [proof_server] [sync_timeout]\n";
        std::cout << "  official_sdk_bridge_example deploy <network> <contract> [seed_hex] [proof_server]\n";

        std::cout << "\nEnvironment:\n";
        std::cout << "  MIDNIGHT_WALLET_STORE_PASSPHRASE=<passphrase >= 16 chars>\n";
        std::cout << "  MIDNIGHT_WALLET_STORE_DIR=<optional default wallet store dir>\n";

        std::cout << "\nQuick examples:\n";
        std::cout << "  official_sdk_bridge_example wallet-add mywallet <seed_hex> preprod\n";
        std::cout << "  official_sdk_bridge_example transfer-wallet preprod mywallet <to_address> <amount>\n";
        std::cout << "  official_sdk_bridge_example state-wallet preprod mywallet\n";
        std::cout << "  official_sdk_bridge_example deploy-wallet undeployed mywallet FaucetAMM\n";
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        const std::string mode = argv[1];
        if (mode == "--help" || mode == "-h" || mode == "help")
        {
            print_usage();
            return 0;
        }
    }

    if (argc > 1 && std::string(argv[1]) == "wallet-add")
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        const std::string wallet_alias = argv[2];
        const std::string seed_hex = argv[3];
        const std::string network = (argc > 4) ? argv[4] : "";
        const std::string wallet_store_dir = (argc > 5) ? argv[5] : "";

        std::string error;
        const bool ok = midnight::blockchain::register_wallet_seed_hex(
            wallet_alias,
            seed_hex,
            network,
            wallet_store_dir,
            &error);

        if (!ok)
        {
            std::cerr << "Wallet registration failed: " << error << std::endl;
            std::cerr << "Hint: set MIDNIGHT_WALLET_STORE_PASSPHRASE (>=16 chars) before wallet-add." << std::endl;
            return 1;
        }

        std::cout << "Wallet alias registered: " << wallet_alias << std::endl;
        if (!network.empty())
        {
            std::cout << "Bound network: " << network << std::endl;
        }
        if (!wallet_store_dir.empty())
        {
            std::cout << "Wallet store: " << wallet_store_dir << std::endl;
        }
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "state-wallet")
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string wallet_alias = argv[3];
        const std::string proof_server = (argc > 4) ? argv[4] : "";
        const uint32_t sync_timeout = (argc > 5) ? static_cast<uint32_t>(std::stoul(argv[5])) : 120U;
        const std::string wallet_store_dir = (argc > 6) ? argv[6] : "";

        const auto state = midnight::blockchain::query_official_wallet_state_with_wallet(
            wallet_alias,
            network,
            proof_server,
            "",
            "",
            "",
            sync_timeout,
            wallet_store_dir,
            "tools/official-transfer-night.mjs");

        if (!state.success)
        {
            std::cerr << "Official wallet state query failed: " << state.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << state.network << std::endl;
        std::cout << "Source address: " << state.source_address << std::endl;
        std::cout << "Synced: " << (state.synced ? "true" : "false") << std::endl;
        std::cout << "Unshielded balance: " << state.unshielded_balance << std::endl;
        std::cout << "Pending unshielded balance: " << state.pending_unshielded_balance << std::endl;
        std::cout << "Dust balance: " << state.dust_balance << std::endl;
        std::cout << "Available UTXOs: " << state.available_utxo_count << std::endl;
        std::cout << "Pending UTXOs: " << state.pending_utxo_count << std::endl;
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "indexer-wallet")
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string wallet_alias = argv[3];
        const std::string proof_server = (argc > 4) ? argv[4] : "";
        const uint32_t sync_timeout = (argc > 5) ? static_cast<uint32_t>(std::stoul(argv[5])) : 120U;
        const std::string wallet_store_dir = (argc > 6) ? argv[6] : "";

        const auto indexer = midnight::blockchain::query_official_indexer_sync_status_with_wallet(
            wallet_alias,
            network,
            proof_server,
            "",
            "",
            "",
            sync_timeout,
            wallet_store_dir,
            "tools/official-transfer-night.mjs");

        if (!indexer.success)
        {
            std::cerr << "Official indexer diagnostics failed: " << indexer.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << indexer.network << std::endl;
        std::cout << "Source address: " << indexer.source_address << std::endl;
        std::cout << "Facade synced: " << (indexer.facade_is_synced ? "true" : "false") << std::endl;
        std::cout << "Shielded connected: " << (indexer.shielded_connected ? "true" : "false") << std::endl;
        std::cout << "Unshielded connected: " << (indexer.unshielded_connected ? "true" : "false") << std::endl;
        std::cout << "Dust connected: " << (indexer.dust_connected ? "true" : "false") << std::endl;
        std::cout << "All connected: " << (indexer.all_connected ? "true" : "false") << std::endl;
        std::cout << "Shielded apply lag: " << indexer.shielded_apply_lag << std::endl;
        std::cout << "Unshielded apply lag: " << indexer.unshielded_apply_lag << std::endl;
        std::cout << "Dust apply lag: " << indexer.dust_apply_lag << std::endl;
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "transfer-wallet")
    {
        if (argc < 6)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string wallet_alias = argv[3];
        const std::string to_address = argv[4];
        const std::string amount = argv[5];
        const std::string proof_server = (argc > 6) ? argv[6] : "";
        const uint32_t sync_timeout = (argc > 7) ? static_cast<uint32_t>(std::stoul(argv[7])) : 120U;
        const std::string wallet_store_dir = (argc > 8) ? argv[8] : "";

        const auto transfer = midnight::blockchain::transfer_official_night_with_wallet(
            wallet_alias,
            to_address,
            amount,
            network,
            proof_server,
            sync_timeout,
            wallet_store_dir,
            "tools/official-transfer-night.mjs");

        if (!transfer.success)
        {
            std::cerr << "Official SDK transfer failed: " << transfer.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << transfer.network << std::endl;
        std::cout << "Source address: " << transfer.source_address << std::endl;
        std::cout << "Destination address: " << transfer.destination_address << std::endl;
        std::cout << "Amount: " << transfer.amount << std::endl;
        std::cout << "TxId: " << transfer.txid << std::endl;
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "state")
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string seed_hex = argv[3];
        const std::string proof_server = (argc > 4) ? argv[4] : "";
        const uint32_t sync_timeout = (argc > 5) ? static_cast<uint32_t>(std::stoul(argv[5])) : 120U;

        const auto state = midnight::blockchain::query_official_wallet_state(
            seed_hex,
            network,
            proof_server,
            "",
            "",
            "",
            sync_timeout,
            "tools/official-transfer-night.mjs");

        if (!state.success)
        {
            std::cerr << "Official wallet state query failed: " << state.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << state.network << std::endl;
        std::cout << "Source address: " << state.source_address << std::endl;
        std::cout << "Synced: " << (state.synced ? "true" : "false") << std::endl;
        std::cout << "Unshielded balance: " << state.unshielded_balance << std::endl;
        std::cout << "Pending unshielded balance: " << state.pending_unshielded_balance << std::endl;
        std::cout << "Dust balance: " << state.dust_balance << std::endl;
        std::cout << "Available UTXOs: " << state.available_utxo_count << std::endl;
        std::cout << "Pending UTXOs: " << state.pending_utxo_count << std::endl;
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "deploy")
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string contract = argv[3];
        const std::string seed_hex = (argc > 4) ? argv[4] : "";
        const std::string proof_server = (argc > 5) ? argv[5] : "";

        const auto deploy = midnight::blockchain::deploy_official_compact_contract(
            contract,
            network,
            seed_hex,
            10,
            "",
            proof_server,
            "",
            "",
            "",
            120,
            120,
            300,
            1,
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            "tools/official-deploy-contract.mjs");

        if (!deploy.success)
        {
            std::cerr << "Official compact deploy failed: " << deploy.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << deploy.network << std::endl;
        std::cout << "Contract: " << deploy.contract << std::endl;
        std::cout << "Contract address: " << deploy.contract_address << std::endl;
        std::cout << "TxId: " << deploy.txid << std::endl;
        std::cout << "Source address: " << deploy.source_address << std::endl;
        std::cout << "Unshielded balance: " << deploy.unshielded_balance << std::endl;
        std::cout << "Dust balance: " << deploy.dust_balance << std::endl;
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "deploy-wallet")
    {
        if (argc < 5)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string wallet_alias = argv[3];
        const std::string contract = argv[4];
        const std::string proof_server = (argc > 5) ? argv[5] : "";
        const std::string wallet_store_dir = (argc > 6) ? argv[6] : "";

        const auto deploy = midnight::blockchain::deploy_official_compact_contract_with_wallet(
            wallet_alias,
            contract,
            network,
            10,
            "",
            proof_server,
            "",
            "",
            "",
            120,
            120,
            300,
            1,
            "",
            "",
            "",
            "",
            "",
            "",
            "",
            wallet_store_dir,
            "tools/official-deploy-contract.mjs");

        if (!deploy.success)
        {
            std::cerr << "Official compact deploy failed: " << deploy.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << deploy.network << std::endl;
        std::cout << "Contract: " << deploy.contract << std::endl;
        std::cout << "Contract address: " << deploy.contract_address << std::endl;
        std::cout << "TxId: " << deploy.txid << std::endl;
        std::cout << "Source address: " << deploy.source_address << std::endl;
        std::cout << "Unshielded balance: " << deploy.unshielded_balance << std::endl;
        std::cout << "Dust balance: " << deploy.dust_balance << std::endl;
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "indexer")
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string seed_hex = argv[3];
        const std::string proof_server = (argc > 4) ? argv[4] : "";
        const uint32_t sync_timeout = (argc > 5) ? static_cast<uint32_t>(std::stoul(argv[5])) : 120U;

        const auto indexer = midnight::blockchain::query_official_indexer_sync_status(
            seed_hex,
            network,
            proof_server,
            "",
            "",
            "",
            sync_timeout,
            "tools/official-transfer-night.mjs");

        if (!indexer.success)
        {
            std::cerr << "Official indexer diagnostics failed: " << indexer.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << indexer.network << std::endl;
        std::cout << "Source address: " << indexer.source_address << std::endl;
        std::cout << "Facade synced: " << (indexer.facade_is_synced ? "true" : "false") << std::endl;
        std::cout << "Shielded connected: " << (indexer.shielded_connected ? "true" : "false") << std::endl;
        std::cout << "Unshielded connected: " << (indexer.unshielded_connected ? "true" : "false") << std::endl;
        std::cout << "Dust connected: " << (indexer.dust_connected ? "true" : "false") << std::endl;
        std::cout << "All connected: " << (indexer.all_connected ? "true" : "false") << std::endl;
        std::cout << "Shielded apply lag: " << indexer.shielded_apply_lag << std::endl;
        std::cout << "Unshielded apply lag: " << indexer.unshielded_apply_lag << std::endl;
        std::cout << "Dust apply lag: " << indexer.dust_apply_lag << std::endl;
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "transfer")
    {
        if (argc < 6)
        {
            print_usage();
            return 1;
        }

        const std::string network = argv[2];
        const std::string seed_hex = argv[3];
        const std::string to_address = argv[4];
        const std::string amount = argv[5];
        const std::string proof_server = (argc > 6) ? argv[6] : "";
        const uint32_t sync_timeout = (argc > 7) ? static_cast<uint32_t>(std::stoul(argv[7])) : 120U;

        const auto transfer = midnight::blockchain::transfer_official_night(
            seed_hex,
            to_address,
            amount,
            network,
            proof_server,
            sync_timeout,
            "tools/official-transfer-night.mjs");

        if (!transfer.success)
        {
            std::cerr << "Official SDK transfer failed: " << transfer.error << std::endl;
            return 1;
        }

        std::cout << "Network: " << transfer.network << std::endl;
        std::cout << "Source address: " << transfer.source_address << std::endl;
        std::cout << "Destination address: " << transfer.destination_address << std::endl;
        std::cout << "Amount: " << transfer.amount << std::endl;
        std::cout << "TxId: " << transfer.txid << std::endl;
        return 0;
    }

    const std::string network = (argc > 1) ? argv[1] : "preprod";
    const std::string seed_hex = (argc > 2) ? argv[2] : "";

    char address[256] = {0};
    char seed[128] = {0};
    char error[512] = {0};

    const int rc = midnight_generate_official_unshielded_address(
        network.c_str(),
        seed_hex.empty() ? nullptr : seed_hex.c_str(),
        address,
        sizeof(address),
        seed,
        sizeof(seed),
        error,
        sizeof(error));

    if (rc != 0)
    {
        std::cerr << "Official SDK address generation failed: " << error << std::endl;
        std::cerr << "Hint: run from repository root and ensure midnight-research dependencies are installed." << std::endl;
        print_usage();
        return 1;
    }

    std::cout << "Network: " << network << std::endl;
    std::cout << "Unshielded address: " << address << std::endl;
    std::cout << "Seed hex: " << seed << std::endl;
    std::cout << "Hint: use transfer mode for onchain send via C++ bridge:" << std::endl;
    std::cout << "  official_sdk_bridge_example transfer preprod <seed_hex> <to_address> <amount> [proof_server] [sync_timeout]" << std::endl;
    std::cout << "Hint: register wallet alias once, then transfer without passing seed on command line:" << std::endl;
    std::cout << "  export MIDNIGHT_WALLET_STORE_PASSPHRASE='your-strong-passphrase'" << std::endl;
    std::cout << "  official_sdk_bridge_example wallet-add <wallet_alias> <seed_hex> [network] [wallet_store_dir]" << std::endl;
    std::cout << "  official_sdk_bridge_example transfer-wallet preprod <wallet_alias> <to_address> <amount> [proof_server] [sync_timeout] [wallet_store_dir]" << std::endl;
    std::cout << "Hint: use state mode for snapshot via C++ bridge:" << std::endl;
    std::cout << "  official_sdk_bridge_example state preprod <seed_hex> [proof_server] [sync_timeout]" << std::endl;
    std::cout << "Hint: wallet alias state/indexer diagnostics:" << std::endl;
    std::cout << "  official_sdk_bridge_example state-wallet preprod <wallet_alias> [proof_server] [sync_timeout] [wallet_store_dir]" << std::endl;
    std::cout << "  official_sdk_bridge_example indexer-wallet preprod <wallet_alias> [proof_server] [sync_timeout] [wallet_store_dir]" << std::endl;
    std::cout << "Hint: use indexer mode for sync diagnostics via C++ bridge:" << std::endl;
    std::cout << "  official_sdk_bridge_example indexer preprod <seed_hex> [proof_server] [sync_timeout]" << std::endl;
    std::cout << "Hint: use deploy mode for compact contract deploy via C++ bridge:" << std::endl;
    std::cout << "  official_sdk_bridge_example deploy undeployed FaucetAMM [seed_hex] [proof_server]" << std::endl;
    std::cout << "Hint: deploy using wallet alias (no seed in process args):" << std::endl;
    std::cout << "  official_sdk_bridge_example deploy-wallet undeployed <wallet_alias> FaucetAMM [proof_server] [wallet_store_dir]" << std::endl;
    return 0;
}
