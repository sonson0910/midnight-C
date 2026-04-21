#include "midnight/blockchain/official_sdk_bridge.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include <json/json.h>

using midnight::blockchain::deploy_official_compact_contract;

namespace
{
    struct Options
    {
        std::string contract = "Vesting";
        std::string contract_module;
        std::string contract_export = "Vesting";
        std::string network = "undeployed";

        std::string seed_hex;
        std::string initial_nonce_hex;
        std::string proof_server;
        std::string node_url = "http://127.0.0.1:9944";
        std::string indexer_url;
        std::string indexer_ws_url;

        std::string private_state_store;
        std::string private_state_id;
        std::string zk_config_path;

        std::string constructor_args_json;
        std::string constructor_args_file;

        std::string beneficiary;
        std::string start_ts;
        std::string cliff_ts;
        std::string duration_secs;
        std::string total_amount;

        uint64_t fee_bps = 10;
        uint32_t sync_timeout = 120;
        uint32_t dust_wait = 120;
        uint32_t deploy_timeout = 300;
        uint32_t dust_utxo_limit = 1;
    };

    void print_usage(const char *program)
    {
        std::cout
            << "Usage:\n"
            << "  " << program << " [options]\n\n"
            << "Core options:\n"
            << "  --network <id>                   preprod|preview|mainnet|undeployed (default: undeployed)\n"
            << "  --contract <name>                Contract identifier (default: Vesting)\n"
            << "  --contract-module <specifier>    Contract JS module specifier/path\n"
            << "  --contract-export <name>         Export name in contract module (default: Vesting)\n"
            << "  --seed-hex <hex>                 32-byte wallet seed hex (or MIDNIGHT_DEPLOY_SEED_HEX env)\n"
            << "  --initial-nonce <hex>            Optional constructor nonce (32-byte hex)\n"
            << "  --proof-server <url>             Proof server URL\n"
            << "  --node-url <url>                 Node RPC URL (default: http://127.0.0.1:9944)\n"
            << "  --indexer-url <url>              Indexer GraphQL URL\n"
            << "  --indexer-ws-url <url>           Indexer GraphQL WS URL\n"
            << "  --private-state-store <name>     Optional private state store\n"
            << "  --private-state-id <id>          Optional private state id\n"
            << "  --zk-config-path <path>          Managed ZK assets directory\n"
            << "  --fee-bps <n>                    Fee basis points fallback (default: 10)\n"
            << "  --sync-timeout <sec>             Wallet sync timeout (default: 120)\n"
            << "  --dust-wait <sec>                Dust wait timeout (default: 120)\n"
            << "  --deploy-timeout <sec>           Deploy timeout (default: 300)\n"
            << "  --dust-utxo-limit <n>            Max NIGHT UTXOs for DUST registration (default: 1)\n"
            << "  --help                           Show this help\n\n"
            << "Constructor args modes:\n"
            << "  1) Explicit JSON mode:\n"
            << "     --constructor-args-json <json> OR --constructor-args-file <path>\n"
            << "  2) Auto vesting mode (common signature):\n"
            << "     --beneficiary <address> --start-ts <unix> --cliff-ts <unix>\n"
            << "     --duration-secs <seconds> --total-amount <decimal>\n"
            << "     Auto JSON order: [beneficiary, start_ts, cliff_ts, duration_secs, total_amount]\n"
            << "     Numeric fields are encoded as typed bigint objects for JS safety.\n\n"
            << "Example:\n"
            << "  " << program << " --network undeployed "
            << "--contract Vesting "
            << "--contract-module @your-org/vesting-contract "
            << "--contract-export Vesting "
            << "--beneficiary mn_addr1... "
            << "--start-ts 1713500000 --cliff-ts 1716100000 "
            << "--duration-secs 31536000 --total-amount 1000000000000\n";
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

    bool parse_u32(const std::string &flag, const std::string &value, uint32_t &out)
    {
        uint64_t parsed = 0;
        if (!parse_u64(flag, value, parsed))
        {
            return false;
        }

        if (parsed > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        {
            std::cerr << "Value out of range for " << flag << ": " << value << std::endl;
            return false;
        }

        out = static_cast<uint32_t>(parsed);
        return true;
    }

    bool is_decimal(const std::string &value)
    {
        if (value.empty())
        {
            return false;
        }

        return std::all_of(value.begin(), value.end(), [](unsigned char ch)
                           { return std::isdigit(ch) != 0; });
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
            if (arg == "--network")
            {
                if (!next_value(arg, opts.network))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--contract")
            {
                if (!next_value(arg, opts.contract))
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
            if (arg == "--seed-hex")
            {
                if (!next_value(arg, opts.seed_hex))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--initial-nonce")
            {
                if (!next_value(arg, opts.initial_nonce_hex))
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
            if (arg == "--zk-config-path")
            {
                if (!next_value(arg, opts.zk_config_path))
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
            if (arg == "--beneficiary")
            {
                if (!next_value(arg, opts.beneficiary))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--start-ts")
            {
                if (!next_value(arg, opts.start_ts))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--cliff-ts")
            {
                if (!next_value(arg, opts.cliff_ts))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--duration-secs")
            {
                if (!next_value(arg, opts.duration_secs))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--total-amount")
            {
                if (!next_value(arg, opts.total_amount))
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
                if (!next_value(arg, numeric) || !parse_u32(arg, numeric, opts.sync_timeout))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--dust-wait")
            {
                if (!next_value(arg, numeric) || !parse_u32(arg, numeric, opts.dust_wait))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--deploy-timeout")
            {
                if (!next_value(arg, numeric) || !parse_u32(arg, numeric, opts.deploy_timeout))
                {
                    return false;
                }
                continue;
            }
            if (arg == "--dust-utxo-limit")
            {
                if (!next_value(arg, numeric) || !parse_u32(arg, numeric, opts.dust_utxo_limit))
                {
                    return false;
                }
                continue;
            }

            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }

        if (!opts.constructor_args_json.empty() && !opts.constructor_args_file.empty())
        {
            std::cerr << "Use either --constructor-args-json or --constructor-args-file, not both." << std::endl;
            return false;
        }

        return true;
    }

    Json::Value as_typed_bigint(const std::string &value)
    {
        Json::Value typed(Json::objectValue);
        typed["__type"] = "bigint";
        typed["value"] = value;
        return typed;
    }

    bool build_default_vesting_constructor_args(const Options &opts, std::string &out_json)
    {
        if (opts.beneficiary.empty() ||
            opts.start_ts.empty() ||
            opts.cliff_ts.empty() ||
            opts.duration_secs.empty() ||
            opts.total_amount.empty())
        {
            return false;
        }

        if (!is_decimal(opts.start_ts) ||
            !is_decimal(opts.cliff_ts) ||
            !is_decimal(opts.duration_secs) ||
            !is_decimal(opts.total_amount))
        {
            std::cerr << "Vesting numeric fields must be unsigned decimal strings." << std::endl;
            return false;
        }

        Json::Value args(Json::arrayValue);
        args.append(opts.beneficiary);
        args.append(as_typed_bigint(opts.start_ts));
        args.append(as_typed_bigint(opts.cliff_ts));
        args.append(as_typed_bigint(opts.duration_secs));
        args.append(as_typed_bigint(opts.total_amount));

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        out_json = Json::writeString(writer, args);
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

    std::string constructor_args_json = opts.constructor_args_json;
    if (constructor_args_json.empty() && !opts.constructor_args_file.empty())
    {
        if (!load_file_text(opts.constructor_args_file, constructor_args_json))
        {
            return 1;
        }
    }

    if (constructor_args_json.empty())
    {
        if (!build_default_vesting_constructor_args(opts, constructor_args_json))
        {
            std::cerr << "Missing constructor args." << std::endl;
            std::cerr << "Provide --constructor-args-json/--constructor-args-file, or vesting flags:" << std::endl;
            std::cerr << "  --beneficiary --start-ts --cliff-ts --duration-secs --total-amount" << std::endl;
            return 1;
        }
    }

    const auto result = deploy_official_compact_contract(
        opts.contract,
        opts.network,
        opts.seed_hex,
        opts.fee_bps,
        opts.initial_nonce_hex,
        opts.proof_server,
        opts.node_url,
        opts.indexer_url,
        opts.indexer_ws_url,
        opts.sync_timeout,
        opts.dust_wait,
        opts.deploy_timeout,
        opts.dust_utxo_limit,
        opts.contract_module,
        opts.contract_export,
        opts.private_state_store,
        opts.private_state_id,
        constructor_args_json,
        "",
        opts.zk_config_path,
        "tools/official-deploy-contract.mjs");

    if (!result.success)
    {
        std::cerr << "Vesting deploy failed: " << result.error << std::endl;
        std::cerr << "Hint: verify module/export/constructor args and ensure node+indexer+proof server are reachable." << std::endl;
        return 1;
    }

    std::cout << "Vesting deploy successful." << std::endl;
    std::cout << "Network: " << result.network << std::endl;
    std::cout << "Contract: " << result.contract << std::endl;
    std::cout << "Contract module: " << result.contract_module << std::endl;
    std::cout << "Contract export: " << result.contract_export << std::endl;
    std::cout << "Contract address: " << result.contract_address << std::endl;
    std::cout << "TxId: " << result.txid << std::endl;
    std::cout << "Source address: " << result.source_address << std::endl;
    std::cout << "Unshielded balance: " << result.unshielded_balance << std::endl;
    std::cout << "Dust balance: " << result.dust_balance << std::endl;

    return 0;
}
