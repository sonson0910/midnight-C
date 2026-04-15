#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>

namespace midnight::network
{

    MidnightNodeRPC::MidnightNodeRPC(const std::string &node_url, uint32_t timeout_ms)
        : client_(std::make_unique<NetworkClient>(node_url, timeout_ms))
    {
        std::ostringstream msg;
        msg << "MidnightNodeRPC initialized: " << node_url;
        midnight::g_logger->info(msg.str());
    }

    std::vector<midnight::blockchain::UTXO> MidnightNodeRPC::get_utxos(
        const std::string &address)
    {
        std::ostringstream msg;
        msg << "Querying UTXOs for address: " << address;
        midnight::g_logger->info(msg.str());

        json params = json::object({{"address", address}});

        try
        {
            json response = rpc_call("getUTXOs", params);

            std::vector<midnight::blockchain::UTXO> utxos;
            if (response.is_array())
            {
                for (const auto &utxo_json : response)
                {
                    utxos.push_back(json_to_utxo(utxo_json));
                }
            }

            msg.str("");
            msg << "Found " << utxos.size() << " UTXOs";
            midnight::g_logger->info(msg.str());

            return utxos;
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(
                std::string("UTXO query unsupported on this RPC endpoint. Use Midnight indexer GraphQL for UTXO/address queries. Root cause: ") +
                e.what());
        }
    }

    midnight::blockchain::ProtocolParams MidnightNodeRPC::get_protocol_params()
    {
        midnight::g_logger->info("Fetching protocol parameters");

        try
        {
            json response = rpc_call("getProtocolParams");
            return json_to_protocol_params(response);
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn(
                std::string("getProtocolParams unavailable on this endpoint, using SDK defaults: ") +
                e.what());
            return json_to_protocol_params(json::object());
        }
    }

    std::pair<uint64_t, std::string> MidnightNodeRPC::get_chain_tip()
    {
        midnight::g_logger->info("Querying chain tip");

        json response;
        try
        {
            response = rpc_call("getChainTip");
        }
        catch (const std::exception &legacy_error)
        {
            midnight::g_logger->debug(
                std::string("getChainTip unavailable, falling back to chain_getHeader: ") +
                legacy_error.what());
            response = rpc_call("chain_getHeader", json::array());
        }

        uint64_t height = response.value("height", 0UL);
        if (height == 0 && response.contains("number"))
        {
            try
            {
                if (response["number"].is_string())
                {
                    height = std::stoull(response["number"].get<std::string>(), nullptr, 0);
                }
                else if (response["number"].is_number_unsigned())
                {
                    height = response["number"].get<uint64_t>();
                }
            }
            catch (...)
            {
                height = 0;
            }
        }

        std::string hash = response.value("hash", "");
        if (hash.empty())
        {
            try
            {
                json finalized_head = rpc_call("chain_getFinalizedHead", json::array());
                if (finalized_head.is_string())
                {
                    hash = finalized_head.get<std::string>();
                }
            }
            catch (const std::exception &e)
            {
                midnight::g_logger->debug(
                    std::string("Could not resolve finalized head hash: ") + e.what());
            }
        }

        std::ostringstream msg;
        msg << "Chain tip: height=" << height << " hash=" << hash;
        midnight::g_logger->info(msg.str());

        return {height, hash};
    }

    std::string MidnightNodeRPC::submit_transaction(const std::string &tx_hex)
    {
        std::ostringstream msg;
        msg << "Submitting transaction (" << tx_hex.length() << " chars)";
        midnight::g_logger->info(msg.str());

        json response;
        try
        {
            json params = json::object({{"transaction", tx_hex}});
            response = rpc_call("submitTransaction", params);
        }
        catch (const std::exception &legacy_error)
        {
            midnight::g_logger->debug(
                std::string("submitTransaction unavailable, falling back to author_submitExtrinsic: ") +
                legacy_error.what());
            json substrate_params = json::array({tx_hex});
            response = rpc_call("author_submitExtrinsic", substrate_params);
        }

        std::string tx_id;
        if (response.is_string())
        {
            tx_id = response.get<std::string>();
        }
        else
        {
            tx_id = response.value("txId", "");
        }

        if (tx_id.empty())
        {
            throw std::runtime_error("Transaction submission response missing transaction identifier");
        }

        msg.str("");
        msg << "Transaction submitted: " << tx_id;
        midnight::g_logger->info(msg.str());

        return tx_id;
    }

    json MidnightNodeRPC::get_transaction(const std::string &tx_id)
    {
        (void)tx_id;
        throw std::runtime_error(
            "get_transaction is not available on raw Midnight node RPC. Use indexer GraphQL for transaction lookup.");
    }

    bool MidnightNodeRPC::wait_for_confirmation(
        const std::string &tx_id,
        uint32_t max_blocks)
    {
        if (tx_id.empty())
        {
            return false;
        }

        std::ostringstream msg;
        msg << "Waiting for confirmation: " << tx_id << " (max " << max_blocks << " blocks)";
        midnight::g_logger->info(msg.str());

        uint64_t start_height = 0;
        try
        {
            start_height = get_chain_tip().first;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->debug("Could not fetch start height: " + std::string(e.what()));
        }

        bool seen_in_pending_pool = false;
        uint32_t iteration = 0;

        while (max_blocks == 0 || iteration < max_blocks)
        {
            try
            {
                json pending = rpc_call("author_pendingExtrinsics", json::array());
                bool is_pending = false;

                if (pending.is_array())
                {
                    for (const auto &ext : pending)
                    {
                        if (!ext.is_string())
                        {
                            continue;
                        }

                        const std::string raw = ext.get<std::string>();
                        if (raw == tx_id ||
                            (tx_id.rfind("0x", 0) == 0 && tx_id.size() > 10 &&
                             raw.find(tx_id.substr(2)) != std::string::npos))
                        {
                            is_pending = true;
                            break;
                        }
                    }
                }

                if (is_pending)
                {
                    seen_in_pending_pool = true;
                    midnight::g_logger->debug("Transaction currently in pending extrinsics");
                }
                else if (seen_in_pending_pool)
                {
                    midnight::g_logger->info("Transaction left pending extrinsics; treating as included");
                    return true;
                }
            }
            catch (const std::exception &e)
            {
                midnight::g_logger->debug("Confirmation check error: " + std::string(e.what()));
            }

            if (max_blocks > 0)
            {
                try
                {
                    auto tip = get_chain_tip();
                    if (tip.first >= (start_height + max_blocks))
                    {
                        midnight::g_logger->warn("Transaction confirmation timeout");
                        return false;
                    }
                }
                catch (const std::exception &e)
                {
                    midnight::g_logger->debug("Chain tip check failed during confirmation wait: " + std::string(e.what()));
                }
            }

            ++iteration;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        midnight::g_logger->warn("Transaction confirmation timeout");
        return false;
    }

    uint64_t MidnightNodeRPC::get_balance(const std::string &address)
    {
        (void)address;
        throw std::runtime_error(
            "get_balance is not available on raw Midnight node RPC. Use indexer GraphQL for account balance queries.");
    }

    json MidnightNodeRPC::evaluate_script(
        const std::string &script,
        const std::string &redeemer)
    {
        (void)script;
        (void)redeemer;
        throw std::runtime_error(
            "evaluate_script is not exposed by current Midnight preprod RPC endpoint.");
    }

    json MidnightNodeRPC::get_node_info()
    {
        midnight::g_logger->info("Querying node info");

        try
        {
            json legacy_response = rpc_call("getNodeInfo");
            if (legacy_response.is_object())
            {
                return legacy_response;
            }
        }
        catch (const std::exception &legacy_error)
        {
            midnight::g_logger->debug(
                std::string("getNodeInfo unavailable, using substrate system_* methods: ") +
                legacy_error.what());
        }

        json info = json::object();
        info["chain"] = rpc_call("system_chain", json::array());
        info["name"] = rpc_call("system_name", json::array());
        info["version"] = rpc_call("system_version", json::array());
        json health = rpc_call("system_health", json::array());
        info["health"] = health;
        const bool is_syncing = health.value("isSyncing", true);
        info["ready"] = !is_syncing;
        info["peers"] = health.value("peers", 0);

        return info;
    }

    bool MidnightNodeRPC::is_ready()
    {
        try
        {
            json info = get_node_info();
            bool ready = false;

            if (info.contains("ready") && info["ready"].is_boolean())
            {
                ready = info["ready"].get<bool>();
            }
            else if (info.contains("health") && info["health"].is_object())
            {
                ready = !info["health"].value("isSyncing", true);
            }

            std::ostringstream msg;
            msg << "Node ready status: " << (ready ? "true" : "false");
            midnight::g_logger->debug(msg.str());

            return ready;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("Node health check failed: " + std::string(e.what()));
            return false;
        }
    }

    json MidnightNodeRPC::rpc_call(
        const std::string &method,
        const json &params)
    {
        json request = json::object({{"jsonrpc", "2.0"},
                                     {"method", method},
                                     {"params", params},
                                     {"id", get_next_request_id()}});

        std::ostringstream msg;
        msg << "RPC call: " << method;
        midnight::g_logger->debug(msg.str());

        try
        {
            std::vector<std::string> path_errors;

            for (const auto &path : rpc_paths_)
            {
                try
                {
                    json response = client_->post_json(path, request);

                    // Check for JSON-RPC error response
                    if (response.contains("error") && !response["error"].is_null())
                    {
                        json error_obj = response["error"];
                        std::string error_msg = error_obj.value("message", "Unknown error");
                        throw std::runtime_error("RPC error: " + error_msg);
                    }

                    if (!response.contains("result"))
                    {
                        throw std::runtime_error("Invalid RPC response - missing result field");
                    }

                    return response["result"];
                }
                catch (const std::exception &path_error)
                {
                    const std::string path_error_message = "path " + path + ": " + path_error.what();
                    path_errors.push_back(path_error_message);
                    midnight::g_logger->debug("RPC path attempt failed: " + path_error_message);
                }
            }

            std::string combined_errors;
            bool first_error = true;
            for (const auto &error : path_errors)
            {
                if (!first_error)
                {
                    combined_errors += " | ";
                }
                combined_errors += error;
                first_error = false;
            }

            throw std::runtime_error("All RPC path attempts failed: " + combined_errors);
        }
        catch (const std::exception &e)
        {
            msg.str("");
            msg << "RPC call failed: " << method << " - " << e.what();
            midnight::g_logger->error(msg.str());
            throw;
        }
    }

    midnight::blockchain::UTXO MidnightNodeRPC::json_to_utxo(const json &js)
    {
        midnight::blockchain::UTXO utxo;

        utxo.tx_hash = js.value("txHash", "");
        utxo.output_index = js.value("outputIndex", 0U);
        utxo.amount = js.value("amount", 0UL);

        // Parse multi-assets if present
        if (js.contains("assets") && js["assets"].is_object())
        {
            for (auto &[asset_id, amount] : js["assets"].items())
            {
                utxo.multi_assets[asset_id] = amount.get<uint64_t>();
            }
        }

        return utxo;
    }

    midnight::blockchain::ProtocolParams MidnightNodeRPC::json_to_protocol_params(
        const json &js)
    {
        midnight::blockchain::ProtocolParams params;

        params.min_fee_a = js.value("minFeeA", 44UL);
        params.min_fee_b = js.value("minFeeB", 155381UL);
        params.utxo_cost_per_byte = js.value("utxoCostPerByte", 4310UL);
        params.max_tx_size = js.value("maxTxSize", 16384UL);
        params.max_block_size = js.value("maxBlockSize", 65536UL);
        params.min_pool_cost = js.value("minPoolCost", 0UL);
        params.coins_per_utxo_word = js.value("coinsPerUtxoWord", 0U);
        constexpr double kFixedPointScale = 1'000'000.0;
        params.price_memory = static_cast<uint64_t>(js.value("priceMemory", 0.0577) * kFixedPointScale);
        params.price_steps = static_cast<uint64_t>(js.value("priceSteps", 0.0000721) * kFixedPointScale);

        return params;
    }

} // namespace midnight::network
