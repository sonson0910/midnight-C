#include <iostream>
#include <string>
#include <cstdint>

#include "midnight/blockchain/official_sdk_bridge.hpp"

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "transfer")
    {
        if (argc < 6)
        {
            std::cerr << "Usage: official_sdk_bridge_example transfer <network> <seed_hex> <to_address> <amount> [proof_server] [sync_timeout]" << std::endl;
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
        return 1;
    }

    std::cout << "Network: " << network << std::endl;
    std::cout << "Unshielded address: " << address << std::endl;
    std::cout << "Seed hex: " << seed << std::endl;
    std::cout << "Hint: use transfer mode for onchain send via C++ bridge:" << std::endl;
    std::cout << "  official_sdk_bridge_example transfer preprod <seed_hex> <to_address> <amount> [proof_server] [sync_timeout]" << std::endl;
    return 0;
}
