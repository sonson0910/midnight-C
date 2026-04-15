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

    midnight::blockchain::ProtocolParams MidnightNodeRPC::get_protocol_params()
    {
        midnight::g_logger->info("Fetching protocol parameters");

        json response = rpc_call("getProtocolParams");
        return json_to_protocol_params(response);
    }

    std::pair<uint64_t, std::string> MidnightNodeRPC::get_chain_tip()
    {
        midnight::g_logger->info("Querying chain tip");

        json response = rpc_call("getChainTip");

        uint64_t height = response.value("height", 0UL);
        std::string hash = response.value("hash", "");

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

        json params = json::object({{"transaction", tx_hex}});

        json response = rpc_call("submitTransaction", params);
        std::string tx_id = response.value("txId", "");

        msg.str("");
        msg << "Transaction submitted: " << tx_id;
        midnight::g_logger->info(msg.str());

        return tx_id;
    }

    json MidnightNodeRPC::get_transaction(const std::string &tx_id)
    {
        std::ostringstream msg;
        msg << "Fetching transaction: " << tx_id;
        midnight::g_logger->debug(msg.str());

        json params = json::object({{"txId", tx_id}});

        json response = rpc_call("getTransaction", params);
        return response;
    }

    bool MidnightNodeRPC::wait_for_confirmation(
        const std::string &tx_id,
        uint32_t max_blocks)
    {
        std::ostringstream msg;
        msg << "Waiting for confirmation: " << tx_id << " (max " << max_blocks << " blocks)";
        midnight::g_logger->info(msg.str());

        for (uint32_t i = 0; i < max_blocks; ++i)
        {
            try
            {
                json tx = get_transaction(tx_id);
                std::string status = tx.value("status", "");

                if (status == "confirmed" || status == "finalized")
                {
                    midnight::g_logger->info("Transaction confirmed");
                    return true;
                }

                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            catch (const std::exception &e)
            {
                midnight::g_logger->debug("Confirmation check error: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }

        midnight::g_logger->warn("Transaction confirmation timeout");
        return false;
    }

    uint64_t MidnightNodeRPC::get_balance(const std::string &address)
    {
        std::ostringstream msg;
        msg << "Querying balance for: " << address;
        midnight::g_logger->debug(msg.str());

        json params = json::object({{"address", address}});

        json response = rpc_call("getBalance", params);
        uint64_t balance = response.value("balance", 0UL);

        msg.str("");
        msg << "Balance: " << balance;
        midnight::g_logger->debug(msg.str());

        return balance;
    }

    json MidnightNodeRPC::evaluate_script(
        const std::string &script,
        const std::string &redeemer)
    {
        midnight::g_logger->debug("Evaluating script");

        json params = json::object({{"script", script},
                                    {"redeemer", redeemer}});

        json response = rpc_call("evaluateScript", params);
        return response;
    }

    json MidnightNodeRPC::get_node_info()
    {
        midnight::g_logger->info("Querying node info");

        json response = rpc_call("getNodeInfo");
        return response;
    }

    bool MidnightNodeRPC::is_ready()
    {
        try
        {
            json info = get_node_info();
            bool ready = info.value("ready", false);

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
        params.max_value_size = js.value("maxValueSize", 5000UL);
        params.price_memory = js.value("priceMemory", 0.0577);
        params.price_steps = js.value("priceSteps", 0.0000721);

        return params;
    }

} // namespace midnight::network
