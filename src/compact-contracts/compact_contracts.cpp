/**
 * Phase 2: Compact Contracts Implementation
 */

#include "midnight/compact-contracts/compact_contracts.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/json_bridge_utils.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <regex>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>
#include <ctime>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace
{
    // Import shared utilities from common_utils.hpp
    using midnight::util::quote_shell_arg;
    using midnight::util::run_command_capture;
    using midnight::util::extract_json_payload;
    using midnight::util::to_lower_copy;

    std::string normalized_contract_key(std::string contract_name)
    {
        contract_name = to_lower_copy(contract_name);
        contract_name.erase(
            std::remove_if(
                contract_name.begin(),
                contract_name.end(),
                [](unsigned char ch)
                { return !std::isalnum(ch); }),
            contract_name.end());
        return contract_name;
    }

    // Import shared JSON bridge from json_bridge_utils.hpp
    using midnight::util::nlohmann_to_jsoncpp;

    bool is_hex_prefixed_string(const std::string &value, size_t min_hex_chars)
    {
        if (value.size() < (2 + min_hex_chars) || value.rfind("0x", 0) != 0)
        {
            return false;
        }

        for (size_t i = 2; i < value.size(); ++i)
        {
            if (!std::isxdigit(static_cast<unsigned char>(value[i])))
            {
                return false;
            }
        }

        return true;
    }

    std::string derive_indexer_url_from_rpc(const std::string &rpc_url)
    {
        if (rpc_url.find("preprod") != std::string::npos)
        {
            return "https://indexer.preprod.midnight.network/api/v4/graphql";
        }
        if (rpc_url.find("preview") != std::string::npos)
        {
            return "https://indexer.preview.midnight.network/api/v4/graphql";
        }
        if (rpc_url.find("127.0.0.1") != std::string::npos || rpc_url.find("localhost") != std::string::npos)
        {
            return "http://127.0.0.1:8088/api/v4/graphql";
        }

        return "";
    }

    std::string derive_network_from_rpc(const std::string &rpc_url)
    {
        const std::string normalized = to_lower_copy(rpc_url);
        if (normalized.find("preprod") != std::string::npos)
        {
            return "preprod";
        }
        if (normalized.find("preview") != std::string::npos)
        {
            return "preview";
        }
        if (normalized.find("mainnet") != std::string::npos)
        {
            return "mainnet";
        }
        if (normalized.find("127.0.0.1") != std::string::npos || normalized.find("localhost") != std::string::npos)
        {
            return "undeployed";
        }

        return "undeployed";
    }

    std::optional<Json::Value> extract_contract_action(const Json::Value &response)
    {
        if (!response.isMember("data") || !response["data"].isObject())
        {
            return {};
        }

        const Json::Value &action = response["data"]["contractAction"];
        if (action.isNull() || !action.isObject())
        {
            return {};
        }

        return action;
    }
}

namespace midnight::compact_contracts
{

    // ============================================================================
    // CompactLoader Implementation
    // ============================================================================

    std::optional<CompactAbi> CompactLoader::load_abi(const std::string &abi_json)
    {
        try
        {
            Json::CharReaderBuilder builder;
            Json::Value root;
            std::string errs;

            std::istringstream iss(abi_json);
            if (!Json::parseFromStream(builder, iss, &root, &errs))
            {
                midnight::g_logger->error(std::string("JSON parse error: ") + errs);
                return {};
            }

            CompactAbi abi;
            abi.contract_name = root.get("name", "unknown").asString();
            abi.version = root.get("version", "1.0.0").asString();

            // Parse public state
            if (root.isMember("publicState"))
            {
                auto pub_state = root["publicState"];
                for (const auto &key : pub_state.getMemberNames())
                {
                    abi.public_state[key] = pub_state[key].asString();
                }
            }

            // Parse private state
            if (root.isMember("privateState"))
            {
                auto pri_state = root["privateState"];
                for (const auto &key : pri_state.getMemberNames())
                {
                    abi.private_state[key] = pri_state[key].asString();
                }
            }

            // Parse functions
            if (root.isMember("functions"))
            {
                auto functions = root["functions"];
                for (const auto &func_json : functions)
                {
                    AbiFunction func;
                    func.name = func_json.get("name", "").asString();
                    func.return_type = func_json.get("returns", "void").asString();
                    func.estimated_weight = func_json.get("weight", 1000).asUInt64();
                    func.requires_private_witness = func_json.get("witness", false).asBool();
                    func.modifies_state = func_json.get("mutable", false).asBool();

                    // Parse parameters
                    if (func_json.isMember("parameters"))
                    {
                        auto params = func_json["parameters"];
                        for (const auto &param : params)
                        {
                            AbiParam p;
                            p.name = param.get("name", "").asString();
                            p.type = param.get("type", "").asString();
                            p.is_array = param.get("array", false).asBool();
                            if (p.is_array)
                            {
                                p.array_size = param.get("size", 0).asUInt();
                            }
                            func.parameters.push_back(p);
                        }
                    }

                    abi.functions.push_back(func);
                }
            }

            return abi;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error loading ABI: ") + e.what());
            return {};
        }
    }

    std::optional<CompiledContract> CompactLoader::load_compiled(const std::string &wasm_file,
                                                                 const std::string &abi_file)
    {
        try
        {
            // Load ABI
            std::ifstream abi_stream(abi_file);
            if (!abi_stream.is_open())
            {
                return {};
            }
            std::string abi_content((std::istreambuf_iterator<char>(abi_stream)),
                                    std::istreambuf_iterator<char>());

            auto abi_opt = load_abi(abi_content);
            if (!abi_opt)
            {
                return {};
            }

            // Load bytecode
            std::ifstream wasm_stream(wasm_file, std::ios::binary);
            if (!wasm_stream.is_open())
            {
                return {};
            }
            std::string bytecode((std::istreambuf_iterator<char>(wasm_stream)),
                                 std::istreambuf_iterator<char>());

            CompiledContract contract;
            contract.bytecode = bytecode;
            contract.abi = *abi_opt;
            contract.verification_key = "vk_" + std::to_string(std::time(nullptr));

            return contract;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error loading compiled contract: ") + e.what());
            return {};
        }
    }

    bool CompactLoader::validate_abi(const CompactAbi &abi)
    {
        if (abi.contract_name.empty())
            return false;
        if (abi.functions.empty())
            return false;

        // All functions must have names
        for (const auto &func : abi.functions)
        {
            if (func.name.empty())
                return false;
        }

        return true;
    }

    // ============================================================================
    // ContractQueryEngine Implementation
    // ============================================================================

    ContractQueryEngine::ContractQueryEngine(const std::string &indexer_graphql_url)
        : indexer_url_(indexer_graphql_url)
    {
    }

    std::optional<Json::Value> ContractQueryEngine::query_public_state(
        const std::string &contract_address, const std::string &state_key)
    {
        try
        {
            if (!is_hex_prefixed_string(contract_address, 40))
            {
                return {};
            }

            std::string query = R"(
            query {
                contractAction(address: ")" +
                                contract_address + R"(", offset: { blockOffset: { height: 0 } }) {
                    __typename
                    address
                    state
                    zswapState
                }
            }
        )";

            auto result = graphql_query(query);
            auto action = extract_contract_action(result);
            if (!action)
            {
                return {};
            }

            Json::Value value;
            value["key"] = state_key;
            value["value"] = action->get("state", Json::Value());
            value["zswapState"] = action->get("zswapState", Json::Value());
            value["actionType"] = action->get("__typename", "");
            return value;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error querying public state: ") + e.what());
            return {};
        }
    }

    std::optional<ContractState> ContractQueryEngine::query_all_public_state(
        const std::string &contract_address)
    {
        try
        {
            if (!is_hex_prefixed_string(contract_address, 40))
            {
                return {};
            }

            ContractState state;
            state.contract_address = contract_address;
            state.block_height = 0;

            std::string query = R"(
            query {
                contractAction(address: ")" +
                                contract_address + R"(", offset: { blockOffset: { height: 0 } }) {
                    __typename
                    address
                    state
                    zswapState
                    transaction {
                        hash
                        block {
                            height
                            hash
                            timestamp
                        }
                    }
                }
            }
        )";

            auto result = graphql_query(query);
            auto action = extract_contract_action(result);
            if (!action)
            {
                return {};
            }

            Json::Value public_data;
            public_data["actionType"] = action->get("__typename", "");
            public_data["address"] = action->get("address", "");
            public_data["state"] = action->get("state", Json::Value());
            public_data["zswapState"] = action->get("zswapState", Json::Value());

            if ((*action).isMember("transaction") && (*action)["transaction"].isObject())
            {
                const Json::Value &tx = (*action)["transaction"];
                public_data["transactionHash"] = tx.get("hash", "");

                if (tx.isMember("block") && tx["block"].isObject())
                {
                    const Json::Value &block = tx["block"];
                    state.block_height = block.get("height", 0).asUInt();
                    public_data["blockHash"] = block.get("hash", "");
                    public_data["timestamp"] = block.get("timestamp", 0).asUInt64();
                }
            }

            state.public_data = public_data;

            return state;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error querying all public state: ") + e.what());
            return {};
        }
    }

    std::optional<CompactAbi> ContractQueryEngine::query_contract_abi(const std::string &contract_address)
    {
        try
        {
            (void)contract_address;
            // The v3 preprod indexer does not expose Compact ABI blobs directly.
            return {};
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error querying contract ABI: ") + e.what());
            return {};
        }
    }

    std::optional<ContractQueryEngine::DeploymentInfo> ContractQueryEngine::query_deployment_info(
        const std::string &contract_address)
    {
        try
        {
            if (!is_hex_prefixed_string(contract_address, 40))
            {
                return {};
            }

            std::string query = R"(
            query {
                contractAction(address: ")" +
                                contract_address + R"(", offset: { blockOffset: { height: 0 } }) {
                    __typename
                    address
                    transaction {
                        hash
                        block {
                            height
                            timestamp
                        }
                    }
                }
            }
        )";

            auto result = graphql_query(query);
            auto action = extract_contract_action(result);
            if (!action)
            {
                return {};
            }

            const std::string action_type = action->get("__typename", "").asString();
            if (action_type != "ContractDeploy")
            {
                return {};
            }

            if (!(*action).isMember("transaction") || !(*action)["transaction"].isObject())
            {
                return {};
            }

            const Json::Value &tx = (*action)["transaction"];
            const Json::Value &block = tx.get("block", Json::Value());

            DeploymentInfo info;
            info.deployer_address = "";
            info.block_height = block.get("height", 0).asUInt();
            info.transaction_hash = tx.get("hash", "").asString();
            info.deployment_timestamp = block.get("timestamp", 0).asUInt64();
            if (info.transaction_hash.empty() || info.block_height == 0)
            {
                return {};
            }
            return info;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error querying deployment info: ") + e.what());
            return {};
        }
    }

    Json::Value ContractQueryEngine::graphql_query(const std::string &query_str)
    {
        midnight::network::NetworkClient client(indexer_url_, 10000);

        nlohmann::json payload = {
            {"query", query_str},
        };

        auto response = client.post_json("/", payload);
        Json::Value parsed = nlohmann_to_jsoncpp(response);

        if (parsed.isMember("errors") && parsed["errors"].isArray() && !parsed["errors"].empty())
        {
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            throw std::runtime_error("Indexer GraphQL returned errors: " + Json::writeString(writer, parsed["errors"]));
        }

        return parsed;
    }

    // ============================================================================
    // ContractCallBuilder Implementation
    // ============================================================================

    ContractCallBuilder::ContractCallBuilder(const CompactAbi &abi) : abi_(abi)
    {
    }

    std::string ContractCallBuilder::build_call(const std::string &function_name,
                                                const Json::Value &args)
    {
        auto func = get_function(function_name);
        if (!func)
        {
            return "";
        }

        // Build encoded call data
        std::string call_data = function_name + ":";

        for (size_t i = 0; i < func->parameters.size(); ++i)
        {
            if (i > 0)
                call_data += ",";

            if (args.isMember(func->parameters[i].name))
            {
                std::string encoded = encode_parameter(func->parameters[i].type,
                                                       args[func->parameters[i].name]);
                call_data += encoded;
            }
        }

        return call_data;
    }

    bool ContractCallBuilder::validate_call(const std::string &function_name,
                                            const Json::Value &args)
    {
        auto func = get_function(function_name);
        if (!func)
        {
            return false;
        }

        // Check all required parameters are provided
        for (const auto &param : func->parameters)
        {
            if (!args.isMember(param.name))
            {
                return false;
            }
        }

        return true;
    }

    std::optional<AbiFunction> ContractCallBuilder::get_function(const std::string &function_name)
    {
        for (const auto &func : abi_.functions)
        {
            if (func.name == function_name)
            {
                return func;
            }
        }
        return {};
    }

    uint64_t ContractCallBuilder::estimate_weight(const std::string &function_name)
    {
        auto func = get_function(function_name);
        if (!func)
        {
            return 0;
        }
        return func->estimated_weight;
    }

    std::string ContractCallBuilder::encode_parameter(const std::string &type,
                                                      const Json::Value &value)
    {
        // Simple encoding (in production: use proper ABI encoding)
        if (type.find("u32") != std::string::npos)
        {
            return "u32:" + std::to_string(value.asUInt());
        }
        else if (type.find("u64") != std::string::npos)
        {
            return "u64:" + std::to_string(value.asUInt64());
        }
        else if (type.find("bytes") != std::string::npos)
        {
            return "bytes:" + value.asString();
        }
        else
        {
            return type + ":" + value.asString();
        }
    }

    // ============================================================================
    // ContractDeployer Implementation
    // ============================================================================

    ContractDeployer::ContractDeployer(const std::string &node_rpc_url)
        : rpc_url_(node_rpc_url)
    {
    }

    std::optional<std::string> ContractDeployer::deploy_contract(
        const CompiledContract &compiled_contract,
        const Json::Value &constructor_args)
    {
        try
        {
            const auto read_string = [&](const std::initializer_list<const char *> &keys) -> std::string
            {
                if (!constructor_args.isObject())
                {
                    return "";
                }

                for (const auto *key : keys)
                {
                    if (!constructor_args.isMember(key))
                    {
                        continue;
                    }

                    const Json::Value &value = constructor_args[key];
                    if (value.isString())
                    {
                        return value.asString();
                    }
                    if (value.isBool())
                    {
                        return value.asBool() ? "true" : "false";
                    }
                    if (value.isUInt64())
                    {
                        return std::to_string(value.asUInt64());
                    }
                    if (value.isInt64())
                    {
                        return std::to_string(static_cast<long long>(value.asInt64()));
                    }
                    if (value.isUInt())
                    {
                        return std::to_string(value.asUInt());
                    }
                    if (value.isInt())
                    {
                        return std::to_string(value.asInt());
                    }
                }

                return "";
            };

            const auto read_uint = [&](const std::initializer_list<const char *> &keys, uint64_t fallback) -> uint64_t
            {
                if (!constructor_args.isObject())
                {
                    return fallback;
                }

                for (const auto *key : keys)
                {
                    if (!constructor_args.isMember(key))
                    {
                        continue;
                    }

                    const Json::Value &value = constructor_args[key];
                    if (value.isUInt64())
                    {
                        return value.asUInt64();
                    }
                    if (value.isUInt())
                    {
                        return value.asUInt();
                    }
                    if (value.isInt64() && value.asInt64() >= 0)
                    {
                        return static_cast<uint64_t>(value.asInt64());
                    }
                    if (value.isInt() && value.asInt() >= 0)
                    {
                        return static_cast<uint64_t>(value.asInt());
                    }
                    if (value.isString())
                    {
                        try
                        {
                            return std::stoull(value.asString());
                        }
                        catch (...)
                        {
                            continue;
                        }
                    }
                }

                return fallback;
            };

            bool bridge_enabled = false;
            if (constructor_args.isObject())
            {
                bridge_enabled = constructor_args.get("use_official_sdk_bridge", false).asBool();
            }

            const char *env_flag = std::getenv("MIDNIGHT_ENABLE_COMPACT_DEPLOY_BRIDGE");
            const std::string env_value = env_flag == nullptr ? "" : to_lower_copy(env_flag);
            const bool env_enabled = (env_value == "1" || env_value == "true" || env_value == "yes");

            if (!bridge_enabled && !env_enabled)
            {
                return {};
            }

            std::string contract_name = compiled_contract.abi.contract_name;
            if (contract_name.empty())
            {
                contract_name = read_string({"contract", "contract_name", "contractName"});
            }

            if (contract_name.empty())
            {
                contract_name = "FaucetAMM";
            }

            if (normalized_contract_key(contract_name).empty())
            {
                midnight::g_logger->error("Contract deploy bridge received an empty/invalid contract name");
                return {};
            }

            std::string network = derive_network_from_rpc(rpc_url_);
            if (constructor_args.isObject())
            {
                const std::string configured_network = read_string({"network", "network_id"});
                if (!configured_network.empty())
                {
                    network = configured_network;
                }
            }

            uint64_t fee_bps = 10;
            if (constructor_args.isArray() && !constructor_args.empty())
            {
                const Json::Value &first = constructor_args[0];
                if (first.isUInt64())
                {
                    fee_bps = first.asUInt64();
                }
                else if (first.isUInt())
                {
                    fee_bps = first.asUInt();
                }
                else if (first.isInt64() && first.asInt64() >= 0)
                {
                    fee_bps = static_cast<uint64_t>(first.asInt64());
                }
                else if (first.isInt() && first.asInt() >= 0)
                {
                    fee_bps = static_cast<uint64_t>(first.asInt());
                }
                else if (first.isString())
                {
                    try
                    {
                        fee_bps = std::stoull(first.asString());
                    }
                    catch (...)
                    {
                        // Keep default.
                    }
                }
            }

            fee_bps = read_uint({"fee_bps", "feeBps"}, fee_bps);

            const uint64_t sync_timeout = read_uint({"sync_timeout", "syncTimeout"}, 120);
            const uint64_t dust_wait = read_uint({"dust_wait", "dustWait"}, sync_timeout);
            const uint64_t deploy_timeout = read_uint({"deploy_timeout", "deployTimeout"}, 300);
            const uint64_t dust_utxo_limit = read_uint({"dust_utxo_limit", "dustUtxoLimit"}, 1);

            std::string bridge_script = read_string({"bridge_script"});
            if (bridge_script.empty())
            {
                bridge_script = "tools/official-deploy-contract.mjs";
            }

            const std::string seed_hex = read_string({"seed_hex", "seed", "wallet_seed_hex"});
            const std::string initial_nonce_hex = read_string({"initial_nonce_hex", "initial_nonce"});
            const std::string proof_server = read_string({"proof_server", "proofServer"});
            std::string node_url = read_string({"node_url", "nodeUrl"});
            const std::string indexer_url = read_string({"indexer_url", "indexerUrl"});
            const std::string indexer_ws_url = read_string({"indexer_ws_url", "indexerWsUrl"});
            const std::string contract_module = read_string({"contract_module", "contractModule"});
            const std::string contract_export = read_string({"contract_export", "contractExport"});
            const std::string private_state_store = read_string({"private_state_store", "privateStateStore"});
            const std::string private_state_id = read_string({"private_state_id", "privateStateId"});
            std::string constructor_args_json = read_string({"constructor_args_json", "constructorArgsJson"});
            const std::string constructor_args_file = read_string({"constructor_args_file", "constructorArgsFile"});
            const std::string zk_config_path = read_string({"zk_config_path", "zkConfigPath"});

            if (constructor_args_json.empty() && constructor_args.isObject())
            {
                const Json::Value *constructor_args_value = nullptr;
                if (constructor_args.isMember("constructor_args"))
                {
                    constructor_args_value = &constructor_args["constructor_args"];
                }
                else if (constructor_args.isMember("constructorArgs"))
                {
                    constructor_args_value = &constructor_args["constructorArgs"];
                }

                if (constructor_args_value != nullptr)
                {
                    Json::StreamWriterBuilder writer;
                    writer["indentation"] = "";
                    constructor_args_json = Json::writeString(writer, *constructor_args_value);
                }
            }

            if (node_url.empty())
            {
                node_url = rpc_url_;
            }

            std::ostringstream command;
            command << "node " << quote_shell_arg(bridge_script)
                    << " --contract " << quote_shell_arg(contract_name)
                    << " --network " << quote_shell_arg(network)
                    << " --fee-bps " << fee_bps
                    << " --sync-timeout " << sync_timeout
                    << " --dust-wait " << dust_wait
                    << " --deploy-timeout " << deploy_timeout
                    << " --dust-utxo-limit " << dust_utxo_limit;

            if (!seed_hex.empty())
            {
                command << " --seed " << quote_shell_arg(seed_hex);
            }
            if (!initial_nonce_hex.empty())
            {
                command << " --initial-nonce " << quote_shell_arg(initial_nonce_hex);
            }
            if (!proof_server.empty())
            {
                command << " --proof-server " << quote_shell_arg(proof_server);
            }
            if (!node_url.empty())
            {
                command << " --node-url " << quote_shell_arg(node_url);
            }
            if (!indexer_url.empty())
            {
                command << " --indexer-url " << quote_shell_arg(indexer_url);
            }
            if (!indexer_ws_url.empty())
            {
                command << " --indexer-ws-url " << quote_shell_arg(indexer_ws_url);
            }
            if (!contract_module.empty())
            {
                command << " --contract-module " << quote_shell_arg(contract_module);
            }
            if (!contract_export.empty())
            {
                command << " --contract-export " << quote_shell_arg(contract_export);
            }
            if (!private_state_store.empty())
            {
                command << " --private-state-store " << quote_shell_arg(private_state_store);
            }
            if (!private_state_id.empty())
            {
                command << " --private-state-id " << quote_shell_arg(private_state_id);
            }
            if (!constructor_args_json.empty())
            {
                command << " --constructor-args-json " << quote_shell_arg(constructor_args_json);
            }
            if (!constructor_args_file.empty())
            {
                command << " --constructor-args-file " << quote_shell_arg(constructor_args_file);
            }
            if (!zk_config_path.empty())
            {
                command << " --zk-config-path " << quote_shell_arg(zk_config_path);
            }

            command << " 2>&1";

            int exit_code = 0;
            const std::string raw_output = run_command_capture(command.str(), exit_code);
            if (raw_output.empty())
            {
                midnight::g_logger->error("Deploy bridge produced no output");
                return {};
            }

            Json::CharReaderBuilder reader_builder;
            Json::Value root;
            std::string parse_errors;
            std::istringstream payload_stream(extract_json_payload(raw_output));
            if (!Json::parseFromStream(reader_builder, payload_stream, &root, &parse_errors))
            {
                midnight::g_logger->error("Deploy bridge returned non-JSON output: " + parse_errors
                          + ". Raw output: " + raw_output);
                return {};
            }

            const bool success = root.get("success", false).asBool();
            if (!success)
            {
                std::string bridge_error = root.get("error", "Contract deploy bridge failed").asString();
                if (bridge_error.empty())
                {
                    bridge_error = "Contract deploy bridge failed";
                }
                if (exit_code != 0)
                {
                    bridge_error += " (exit code " + std::to_string(exit_code) + ")";
                }

                midnight::g_logger->error(bridge_error);
                return {};
            }

            std::string contract_address = root.get("contractAddress", "").asString();
            if (contract_address.empty() &&
                root.isMember("deployTxData") && root["deployTxData"].isObject() &&
                root["deployTxData"].isMember("public") && root["deployTxData"]["public"].isObject())
            {
                contract_address = root["deployTxData"]["public"].get("contractAddress", "").asString();
            }

            if (contract_address.empty())
            {
                midnight::g_logger->error("Deploy bridge succeeded but no contractAddress was returned");
                return {};
            }

            return contract_address;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error deploying contract: ") + e.what());
            return {};
        }
    }

    std::optional<ContractDeployer::DeploymentStatus> ContractDeployer::get_deployment_status(
        const std::string &tx_hash)
    {
        try
        {
            if (!is_hex_prefixed_string(tx_hash, 64))
            {
                return {};
            }

            const std::string indexer_url = derive_indexer_url_from_rpc(rpc_url_);
            if (indexer_url.empty())
            {
                return {};
            }

            midnight::network::NetworkClient client(indexer_url, 10000);
            const std::string query = R"(
            query {
                transactions(offset: { hash: ")" +
                                      tx_hash + R"(" }) {
                    hash
                    block {
                        height
                    }
                    contractActions {
                        __typename
                        address
                    }
                }
            }
        )";

            nlohmann::json payload = {
                {"query", query},
            };

            Json::Value response = nlohmann_to_jsoncpp(client.post_json("/", payload));
            if (!response.isMember("data") || !response["data"].isMember("transactions") || !response["data"]["transactions"].isArray())
            {
                return {};
            }

            const Json::Value &txs = response["data"]["transactions"];
            if (txs.empty())
            {
                return {};
            }

            const Json::Value &tx = txs[0];
            DeploymentStatus status;
            status.status = DeploymentStatus::Status::FINALIZED;
            status.transaction_hash = tx.get("hash", "").asString();
            if (tx.isMember("block") && tx["block"].isObject())
            {
                status.block_height = tx["block"].get("height", 0).asUInt();
            }

            if (tx.isMember("contractActions") && tx["contractActions"].isArray() && !tx["contractActions"].empty())
            {
                status.contract_address = tx["contractActions"][0].get("address", "").asString();
            }

            if (status.transaction_hash.empty())
            {
                return {};
            }

            return status;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error getting deployment status: ") + e.what());
            return {};
        }
    }

    // ============================================================================
    // WitnessFunctionProcessor Implementation
    // ============================================================================

    WitnessFunctionProcessor::WitnessFunctionProcessor(const CompactAbi &contract_abi)
        : contract_abi_(contract_abi)
    {
    }

    WitnessContext WitnessFunctionProcessor::build_witness(
        const std::string &function_name,
        const std::map<std::string, std::string> &private_data,
        const Json::Value &public_args)
    {

        WitnessContext context;
        context.contract_address = "";
        context.function_name = function_name;
        context.function_args = public_args;
        context.private_inputs = private_data;

        // Generate commitments for private data
        context.commitments = generate_commitments(private_data);

        return context;
    }

    bool WitnessFunctionProcessor::requires_witness(const std::string &function_name)
    {
        for (const auto &func : contract_abi_.functions)
        {
            if (func.name == function_name)
            {
                return func.requires_private_witness;
            }
        }
        return false;
    }

    std::vector<std::string> WitnessFunctionProcessor::generate_commitments(
        const std::map<std::string, std::string> &private_data)
    {

        std::vector<std::string> commitments;
        for (const auto &[key, value] : private_data)
        {
            // In production: Compute commitment (e.g., Pedersen, Poseidon)
            // For now: Hash the value
            std::string commitment = "0x" + std::string(64, 'c');
            commitments.push_back(commitment);
        }
        return commitments;
    }

} // namespace midnight::compact_contracts
