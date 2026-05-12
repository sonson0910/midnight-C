#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/wallet/bech32m.hpp"
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <sodium.h>
#include <iomanip>

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

            msg.clear();
            msg.str("");
            msg << "Found " << utxos.size() << " UTXOs";
            midnight::g_logger->info(msg.str());

            return utxos;
        }
        catch (const std::exception &)
        {
            // Midnight node RPC does not support UTXO queries.
            // Use IndexerClient (GraphQL) instead:
            //
            //   midnight::network::IndexerClient indexer(
            //       "https://indexer.preprod.midnight.network/api/v4/graphql");
            //   auto utxos = indexer.query_utxos(address);
            //
            // For real-time UTXO updates, use RealtimeIndexerClient or
            // GraphQLSubscriptionClient which subscribe via WebSocket.
            midnight::g_logger->debug(
                "UTXO query via node RPC failed. "
                "Use IndexerClient (GraphQL) or RealtimeIndexerClient for UTXO queries.");

            throw std::runtime_error(
                "UTXO query not supported by node RPC. "
                "Use midnight::network::IndexerClient with GraphQL endpoint instead.");
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

        msg.clear();
        msg.str("");
        msg << "Transaction submitted: " << tx_id;
        midnight::g_logger->info(msg.str());

        return tx_id;
    }

    json MidnightNodeRPC::get_transaction(const std::string &tx_id)
    {
        if (tx_id.empty())
        {
            throw std::runtime_error("Transaction ID cannot be empty");
        }

        midnight::g_logger->info("Transaction query attempted: " + tx_id);

        // MidnightNodeRPC is designed for transaction SUBMISSION (author_submitExtrinsic),
        // not for historical transaction QUERY. The node RPC does not provide a
        // direct method to retrieve arbitrary transactions by hash.
        //
        // To query transaction details, use:
        //
        //   midnight::network::IndexerClient indexer(
        //       "https://indexer.preprod.midnight.network/api/v4/graphql");
        //   auto tx = indexer.get_transaction(tx_id);
        //
        // The Indexer v4 GraphQL API provides:
        //   - Full transaction details with input/output UTXOs
        //   - Transaction status and block confirmation
        //   - Event history and script execution results
        //
        // For real-time transaction monitoring, use:
        //   midnight::network::RealtimeIndexerClient or
        //   midnight::network::GraphQLSubscriptionClient

        midnight::g_logger->debug(
            "Transaction query requires IndexerClient (GraphQL) - "
            "use indexer.get_transaction(tx_id) instead");

        throw std::runtime_error(
            "get_transaction is not available on MidnightNodeRPC. "
            "Use midnight::network::IndexerClient.get_transaction(tx_id) "
            "with the GraphQL endpoint for transaction lookup.");
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
        if (address.empty())
        {
            throw std::runtime_error("Address cannot be empty");
        }

        midnight::g_logger->info("Querying balance for address: " + address);

        // IMPORTANT: This method uses get_contract_state which queries smart contracts
        // via midnight_contractState RPC. This is NOT the correct method for user wallet
        // balances. User wallet (unshielded NIGHT) balances live in the Zswap UTXO set,
        // which is NOT accessible via Midnight Node RPC.
        //
        // CORRECT WAY to query user wallet NIGHT balance:
        //   midnight::network::IndexerClient indexer(
        //       "https://indexer.preprod.midnight.network/api/v4/graphql");
        //   auto state = indexer.query_wallet_state("mn_addr_preprod1...");
        //   // state.unshielded_balance contains NIGHT balance
        //
        // Midnight Node RPC (this class) should ONLY be used for:
        //   - Transaction submission (submit_transaction)
        //   - Chain tip (get_chain_tip)
        //   - Node health (is_ready, get_node_info)
        //   - Protocol params (get_protocol_params)
        //
        // For ALL state queries (balances, UTXOs, transactions, contract state),
        // use IndexerClient with GraphQL instead.

        midnight::g_logger->warn(
            "MidnightNodeRPC::get_balance is deprecated for user wallets. "
            "Use IndexerClient::query_wallet_state() with the GraphQL endpoint instead. "
            "This method only works for smart contract state queries via RPC.");

        try
        {
            json state = get_contract_state(address);

            uint64_t total_balance = 0;

            if (state.is_object() && state.contains("message"))
            {
                std::string msg = state["message"].get<std::string>();
                if (msg.find("No UTXOs") != std::string::npos)
                {
                    midnight::g_logger->info("No UTXOs found for address - balance is 0");
                    return 0;
                }
            }

            if (state.is_object() && state.contains("utxos"))
            {
                const auto& utxos = state["utxos"];
                if (utxos.is_array())
                {
                    for (const auto& utxo : utxos)
                    {
                        if (utxo.is_object() && utxo.contains("value"))
                        {
                            total_balance += utxo["value"].get<uint64_t>();
                        }
                    }
                    midnight::g_logger->info("Balance from UTXOs: " + std::to_string(total_balance));
                    return total_balance;
                }
            }

            if (state.is_object() && state.contains("total_balance"))
            {
                total_balance = state["total_balance"].get<uint64_t>();
                midnight::g_logger->info("Balance from total_balance: " + std::to_string(total_balance));
                return total_balance;
            }

            midnight::g_logger->debug("Unexpected contract state response format: " + state.dump());
            return 0;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->debug("get_balance failed: " + std::string(e.what()));
            throw;
        }
    }

    json MidnightNodeRPC::get_contract_state(const std::string &contract_address_input)
    {
        if (contract_address_input.empty())
        {
            throw std::runtime_error("Contract address cannot be empty");
        }

        midnight::g_logger->info("Querying contract state for: " + contract_address_input);

        // IMPORTANT: This method queries SMART CONTRACT state via midnight_contractState RPC.
        // It is NOT designed for user wallet (unshielded NIGHT) balance queries.
        //
        // For USER WALLET queries (NIGHT unshielded address balance):
        //   midnight::network::IndexerClient indexer(
        //       "https://indexer.preprod.midnight.network/api/v4/graphql");
        //   auto state = indexer.query_wallet_state("mn_addr_preprod1...");
        //
        // midnight_contractState RPC expects:
        //   - For contract addresses: raw 32-byte hex WITHOUT 0x prefix
        //   - User wallet addresses (Bech32m) will NOT work correctly with this RPC

        std::string address_hex;
        bool needs_decode = false;
        std::string lower_input = midnight::util::to_lower_copy(contract_address_input);

        if (lower_input.rfind("mn_", 0) == 0 || lower_input.rfind("stake", 0) == 0)
        {
            needs_decode = true;
        }

        if (needs_decode)
        {
            // Decode Bech32m address to get 32 raw bytes
            try
            {
                midnight::wallet::bech32m::DecodeResult decoded =
                    midnight::wallet::bech32m::decode(contract_address_input);
                
                // Convert raw bytes to hex string (without 0x prefix)
                std::ostringstream oss;
                for (uint8_t b : decoded.data)
                {
                    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
                }
                address_hex = oss.str();
                
                midnight::g_logger->debug("Decoded Bech32m address to hex: " + address_hex);
            }
            catch (const std::exception &e)
            {
                midnight::g_logger->error("Failed to decode Bech32m address: " + std::string(e.what()));
                throw std::runtime_error("Invalid Bech32m address: " + contract_address_input);
            }
        }
        else
        {
            // Assume it's already a hex string - remove 0x prefix if present
            address_hex = contract_address_input;
            if (address_hex.substr(0, 2) == "0x")
            {
                address_hex = address_hex.substr(2);
            }
        }

        try
        {
            // Call midnight_contractState RPC with the raw 32-byte hex (no 0x prefix)
            json params;
            params["contract_address"] = address_hex;

            json response = rpc_call("midnight_contractState", params);
            
            // Empty response ("") typically means no UTXOs found for this address
            if (response.is_string() && response.get<std::string>().empty())
            {
                midnight::g_logger->info("Contract state query returned empty - no UTXOs for this address");
                // Return a JSON indicating empty state rather than throwing
                json empty_result = json::object();
                empty_result["utxos"] = json::array();
                empty_result["total_balance"] = 0;
                empty_result["message"] = "No UTXOs found for address";
                return empty_result;
            }
            
            midnight::g_logger->info("Contract state query successful");
            return response;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("get_contract_state failed: " + std::string(e.what()));
            throw;
        }
    }

    json MidnightNodeRPC::evaluate_script(
        const std::string &script,
        const std::string &redeemer)
    {
        if (script.empty())
        {
            throw std::runtime_error("Script cannot be empty");
        }

        midnight::g_logger->info("Evaluating smart contract script");

        // Midnight uses Substrate's contracts_call RPC for off-chain contract execution.
        // This simulates the contract call without submitting a transaction.
        //
        // contracts_call parameters:
        //   call: { origin: AccountId, dest: AccountId, value: u128, gasLimit: u64, storageDepositLimit: Option<u128>, inputData: Vec<u8> }
        //
        // The node will execute the contract code with the provided input data
        // and return the execution result (return value, gas used, etc.)

        try
        {
            // Try the contracts_call RPC first (Substrate contracts pallet)
            // Build call structure as JSON
            json call_struct = json::object();
            call_struct["origin"] = "0x0000000000000000000000000000000000000000000000000000000000000000";
            call_struct["dest"] = "0x0000000000000000000000000000000000000000000000000000000000000000";
            call_struct["value"] = "0x00000000000000000000000000000000";
            call_struct["gasLimit"] = "0x0000000001A25C00";  // 30000000 ref_time units
            call_struct["storageDepositLimit"] = nullptr;  // null
            call_struct["inputData"] = script.empty() ? "0x" : script;

            json params = json::array({call_struct});

            // Try new API first (with relayHash)
            try
            {
                json req = json::object();
                req["call"] = call_struct;
                req["relayHash"] = nullptr;
                json response = rpc_call("contracts_call", json::array({req}));
                midnight::g_logger->info("Script evaluation successful via contracts_call");
                return json::object({
                    {"success", true},
                    {"result", response},
                    {"method", "contracts_call"}
                });
            }
            catch (const std::exception &)
            {
                // Fall back to legacy API without relayHash
                json response = rpc_call("contracts_call", params);
                midnight::g_logger->info("Script evaluation successful via contracts_call (legacy)");
                return json::object({
                    {"success", true},
                    {"result", response},
                    {"method", "contracts_call_legacy"}
                });
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->debug("contracts_call failed: " + std::string(e.what()));

            // Fallback: Try rpc_callWithProof if available (for ZK-enabled evaluation)
            try
            {
                json proof_params = {
                    {"code", script},
                    {"inputData", redeemer}
                };
                json response = rpc_call("rpc_callWithProof", proof_params);
                return json::object({
                    {"success", true},
                    {"result", response},
                    {"method", "rpc_callWithProof"}
                });
            }
            catch (const std::exception &)
            {
                // contracts_call/rpc_callWithProof not available on this node
                // This is expected for nodes without the contracts pallet
            }
        }

        // Midnight smart contract execution happens during transaction validation.
        // The recommended approach is to submit the transaction and let the node
        // validate the script. Invalid scripts cause submission to fail.
        midnight::g_logger->warn(
            "evaluate_script: node does not support off-chain script evaluation. "
            "Smart contract scripts are validated on-chain during transaction submission. "
            "Use submit_transaction() — invalid scripts will cause submission to fail "
            "with a validation error.");

        return json::object({
            {"success", false},
            {"error", "Off-chain script evaluation not available on this node"},
            {"recommendation", "Submit transaction to validate scripts on-chain"}
        });
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
                        // Truncate internal error details to prevent information leakage
                        if (error_msg.length() > 256) {
                            error_msg = error_msg.substr(0, 256) + "...";
                        }
                        midnight::g_logger->error("RPC error [" + method + "]: " + error_msg);
                        throw std::runtime_error("RPC call failed: " + method);
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

            std::ostringstream err_stream;
            bool first_error = true;
            for (const auto &error : path_errors)
            {
                if (!first_error)
                {
                    err_stream << " | ";
                }
                err_stream << error;
                first_error = false;
            }

            throw std::runtime_error("All RPC path attempts failed: " + err_stream.str());
        }
        catch (const std::exception &e)
        {
            std::ostringstream err_log;
            err_log << "RPC call failed: " << method << " - " << e.what();
            midnight::g_logger->error(err_log.str());
            throw;
        }
    }

    midnight::blockchain::UTXO MidnightNodeRPC::json_to_utxo(const json &js)
    {
        midnight::blockchain::UTXO utxo;

        utxo.tx_hash = js.value("txHash", "");
        utxo.output_index = js.value("outputIndex", 0U);
        utxo.amount_commitment = js.value("amountCommitment", "");

        // Decrypted value (only available if viewing key is available)
        utxo.value = js.value("value", 0ULL);

        // Parse multi-assets if present
        if (js.contains("assets") && js["assets"].is_object())
        {
            for (auto &[asset_id, commitment] : js["assets"].items())
            {
                utxo.multi_assets[asset_id] = commitment.get<std::string>();
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
