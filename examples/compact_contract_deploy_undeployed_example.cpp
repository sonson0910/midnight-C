#include "midnight/compact-contracts/compact_contracts.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using midnight::phase2::CompiledContract;
using midnight::phase2::ContractDeployer;

namespace
{
    void print_usage(const char *program)
    {
        std::cout << "Usage:\n"
                  << "  " << program << " [seed_hex] [fee_bps] [proof_server]\n\n"
                  << "Examples:\n"
                  << "  " << program << " 0123abcd... 10 http://127.0.0.1:6300\n"
                  << "  MIDNIGHT_DEPLOY_SEED_HEX=0123abcd... " << program << "\n\n"
                  << "Notes:\n"
                  << "  - This example uses the official JS deploy bridge for FaucetAMM on undeployed.\n"
                  << "  - Ensure midnight-research dependencies are installed and node is available.\n"
                  << "  - Wallet must have NIGHT and DUST for deployment fees.\n";
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--help")
    {
        print_usage(argv[0]);
        return 0;
    }

    std::string seed_hex;
    if (argc > 1)
    {
        seed_hex = argv[1];
    }

    uint64_t fee_bps = 10;
    if (argc > 2)
    {
        try
        {
            fee_bps = std::stoull(argv[2]);
        }
        catch (const std::exception &)
        {
            std::cerr << "Invalid fee_bps value: " << argv[2] << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    std::string proof_server;
    if (argc > 3)
    {
        proof_server = argv[3];
    }

    if (seed_hex.empty())
    {
        const char *env_seed = std::getenv("MIDNIGHT_DEPLOY_SEED_HEX");
        if (env_seed != nullptr)
        {
            seed_hex = env_seed;
        }
    }

    Json::Value deploy_args(Json::objectValue);
    deploy_args["use_official_sdk_bridge"] = true;
    deploy_args["network"] = "undeployed";
    deploy_args["fee_bps"] = static_cast<Json::UInt64>(fee_bps);
    deploy_args["sync_timeout"] = static_cast<Json::UInt64>(120);
    deploy_args["dust_wait"] = static_cast<Json::UInt64>(120);
    deploy_args["deploy_timeout"] = static_cast<Json::UInt64>(300);

    if (!seed_hex.empty())
    {
        deploy_args["seed_hex"] = seed_hex;
    }

    if (!proof_server.empty())
    {
        deploy_args["proof_server"] = proof_server;
    }

    CompiledContract compiled;
    compiled.abi.contract_name = "FaucetAMM";

    ContractDeployer deployer("http://127.0.0.1:9944");
    const auto contract_address = deployer.deploy_contract(compiled, deploy_args);

    if (!contract_address)
    {
        std::cerr << "Deployment failed.\n";
        std::cerr << "Hint: verify proof server/indexer/node are running, and wallet has deploy funds.\n";
        return 1;
    }

    std::cout << "Deployment successful.\n";
    std::cout << "Contract address: " << *contract_address << std::endl;
    return 0;
}
