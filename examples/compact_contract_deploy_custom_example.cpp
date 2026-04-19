#include "midnight/compact-contracts/compact_contracts.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using midnight::phase2::CompiledContract;
using midnight::phase2::ContractDeployer;

namespace
{
    struct Options
    {
        std::string contract = "FaucetAMM";
        std::string network = "undeployed";
        std::string seed_hex;
        std::string proof_server;
        std::string node_url = "http://127.0.0.1:9944";
        std::string indexer_url;
        std::string indexer_ws_url;
        std::string contract_module;
        std::string contract_export;
        std::string private_state_store;
        std::string private_state_id;
        std::string constructor_args_json;
        std::string constructor_args_file;
        std::string zk_config_path;
        uint64_t fee_bps = 10;
        uint64_t sync_timeout = 120;
        uint64_t dust_wait = 120;
        uint64_t deploy_timeout = 300;
        uint64_t dust_utxo_limit = 1;
    };

    void print_usage(const char *program)
    {
        std::cout
            << "Usage:\n"
            << "  " << program << " [options]\n\n"
            << "Core options:\n"
            << "  --contract <name>                Contract name (default: FaucetAMM)\n"
            << "  --contract-module <specifier>    Contract JS module specifier/path\n"
            << "  --contract-export <name>         Export name in contract module\n"
            << "  --zk-config-path <path>          Managed ZK assets directory\n"
            << "  --private-state-id <id>          Private state id for deployContract\n"
            << "  --private-state-store <name>     Local private state store name\n"
            << "  --constructor-args-json <json>   Constructor args JSON (array/object)\n"
            << "  --constructor-args-file <path>   File containing constructor args JSON\n\n"
            << "Network/wallet options:\n"
            << "  --network <id>                   preprod|preview|mainnet|undeployed\n"
            << "  --node-url <url>                 Node RPC URL\n"
            << "  --indexer-url <url>              Indexer GraphQL URL\n"
            << "  --indexer-ws-url <url>           Indexer GraphQL WS URL\n"
            << "  --proof-server <url>             Proof server URL\n"
            << "  --seed-hex <hex>                 32-byte seed hex\n"
            << "  --fee-bps <n>                    FaucetAMM default fee_bps fallback\n"
            << "  --sync-timeout <sec>             Default: 120\n"
            << "  --dust-wait <sec>                Default: 120\n"
            << "  --deploy-timeout <sec>           Default: 300\n"
            << "  --dust-utxo-limit <n>            Default: 1\n"
            << "  --help                           Show this help\n\n"
            << "Example:\n"
            << "  " << program << " --contract FaucetAMM "
            << "--contract-module @midnight-ntwrk/counter-contract "
            << "--contract-export FaucetAMM "
            << "--zk-config-path midnight-research/node_modules/@midnight-ntwrk/counter-contract/dist/managed/FaucetAMM\n";
    }

    bool parse_u64(const std::string &flag, const std::string &value, uint64_t &out)
    {
        try
        {
            size_t consumed = 0;
            const unsigned long long parsed = std::stoull(value, &consumed, 10);
            if (consumed != value.size())
            {
                throw std::invalid_argument("trailing characters");
            }
            out = static_cast<uint64_t>(parsed);
            return true;
        }
        catch (const std::exception &)
        {
            std::cerr << "Invalid value for " << flag << ": " << value << std::endl;
            return false;
        }
    }

    bool load_file_text(const std::string &path, std::string &out)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
        {
            std::cerr << "Cannot open file: " << path << std::endl;
            return false;
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        out = buffer.str();
        return true;
    }

    bool parse_options(int argc, char **argv, Options &opts)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];

            auto next_value = [&](const std::string &flag, std::string &target) -> bool
            {
                if (i + 1 >= argc)
                {
                    std::cerr << flag << " requires a value" << std::endl;
                    return false;
                }
                target = argv[++i];
                return true;
            };

            if (arg == "--help")
            {
                print_usage(argv[0]);
                return false;
            }
            if (arg == "--contract")
            {
                if (!next_value(arg, opts.contract))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--network")
            {
                if (!next_value(arg, opts.network))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--seed-hex")
            {
                if (!next_value(arg, opts.seed_hex))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--proof-server")
            {
                if (!next_value(arg, opts.proof_server))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--node-url")
            {
                if (!next_value(arg, opts.node_url))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--indexer-url")
            {
                if (!next_value(arg, opts.indexer_url))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--indexer-ws-url")
            {
                if (!next_value(arg, opts.indexer_ws_url))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--contract-module")
            {
                if (!next_value(arg, opts.contract_module))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--contract-export")
            {
                if (!next_value(arg, opts.contract_export))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--private-state-store")
            {
                if (!next_value(arg, opts.private_state_store))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--private-state-id")
            {
                if (!next_value(arg, opts.private_state_id))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--constructor-args-json")
            {
                if (!next_value(arg, opts.constructor_args_json))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--constructor-args-file")
            {
                if (!next_value(arg, opts.constructor_args_file))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--zk-config-path")
            {
                if (!next_value(arg, opts.zk_config_path))
                {
                    return false;
                }
                continue;
            }

            std::string numeric;
            if (arg == "--fee-bps")
            {
                if (!next_value(arg, numeric) || !parse_u64(arg, numeric, opts.fee_bps))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--sync-timeout")
            {
                if (!next_value(arg, numeric) || !parse_u64(arg, numeric, opts.sync_timeout))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--dust-wait")
            {
                if (!next_value(arg, numeric) || !parse_u64(arg, numeric, opts.dust_wait))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--deploy-timeout")
            {
                if (!next_value(arg, numeric) || !parse_u64(arg, numeric, opts.deploy_timeout))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--dust-utxo-limit")
            {
                if (!next_value(arg, numeric) || !parse_u64(arg, numeric, opts.dust_utxo_limit))
                {
                    return false;
                }
                continue;
            }

            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }

        if (opts.constructor_args_json.empty() && !opts.constructor_args_file.empty())
        {
            if (!load_file_text(opts.constructor_args_file, opts.constructor_args_json))
            {
                return false;
            }
        }

        if (!opts.constructor_args_json.empty() && !opts.constructor_args_file.empty())
        {
            std::cerr << "Use either --constructor-args-json or --constructor-args-file" << std::endl;
            return false;
        }

        return true;
    }
}

int main(int argc, char **argv)
{
    Options opts;
    if (!parse_options(argc, argv, opts))
    {
        return (argc > 1 && std::string(argv[1]) == "--help") ? 0 : 1;
    }

    if (opts.seed_hex.empty())
    {
        const char *env_seed = std::getenv("MIDNIGHT_DEPLOY_SEED_HEX");
        if (env_seed != nullptr)
        {
            opts.seed_hex = env_seed;
        }
    }

    Json::Value deploy_args(Json::objectValue);
    deploy_args["use_official_sdk_bridge"] = true;
    deploy_args["network"] = opts.network;
    deploy_args["fee_bps"] = static_cast<Json::UInt64>(opts.fee_bps);
    deploy_args["sync_timeout"] = static_cast<Json::UInt64>(opts.sync_timeout);
    deploy_args["dust_wait"] = static_cast<Json::UInt64>(opts.dust_wait);
    deploy_args["deploy_timeout"] = static_cast<Json::UInt64>(opts.deploy_timeout);
    deploy_args["dust_utxo_limit"] = static_cast<Json::UInt64>(opts.dust_utxo_limit);

    if (!opts.seed_hex.empty())
    {
        deploy_args["seed_hex"] = opts.seed_hex;
    }
    if (!opts.proof_server.empty())
    {
        deploy_args["proof_server"] = opts.proof_server;
    }
    if (!opts.node_url.empty())
    {
        deploy_args["node_url"] = opts.node_url;
    }
    if (!opts.indexer_url.empty())
    {
        deploy_args["indexer_url"] = opts.indexer_url;
    }
    if (!opts.indexer_ws_url.empty())
    {
        deploy_args["indexer_ws_url"] = opts.indexer_ws_url;
    }
    if (!opts.contract_module.empty())
    {
        deploy_args["contract_module"] = opts.contract_module;
    }
    if (!opts.contract_export.empty())
    {
        deploy_args["contract_export"] = opts.contract_export;
    }
    if (!opts.private_state_store.empty())
    {
        deploy_args["private_state_store"] = opts.private_state_store;
    }
    if (!opts.private_state_id.empty())
    {
        deploy_args["private_state_id"] = opts.private_state_id;
    }
    if (!opts.zk_config_path.empty())
    {
        deploy_args["zk_config_path"] = opts.zk_config_path;
    }

    if (!opts.constructor_args_json.empty())
    {
        Json::Value parsed_constructor_args;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream input(opts.constructor_args_json);
        if (!Json::parseFromStream(builder, input, &parsed_constructor_args, &errors))
        {
            std::cerr << "Invalid constructor args JSON: " << errors << std::endl;
            return 1;
        }

        deploy_args["constructor_args_json"] = opts.constructor_args_json;
        deploy_args["constructor_args"] = parsed_constructor_args;
    }

    CompiledContract compiled;
    compiled.abi.contract_name = opts.contract;

    ContractDeployer deployer(opts.node_url);
    const auto contract_address = deployer.deploy_contract(compiled, deploy_args);

    if (!contract_address)
    {
        std::cerr << "Deployment failed." << std::endl;
        std::cerr << "Hint: verify module/export/zk-config, service endpoints, and wallet balances." << std::endl;
        return 1;
    }

    std::cout << "Deployment successful." << std::endl;
    std::cout << "Contract address: " << *contract_address << std::endl;
    return 0;
}
