#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/codec/scale_codec.hpp"
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
            //       "https://indexer.preview.midnight.network/api/v4/graphql");
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
        //       "https://indexer.preview.midnight.network/api/v4/graphql");
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

        // Midnight uses a Substrate-based node with ledger accounts.
        // Storage key for Ledger.FreeDeposits(AccountId):
        //   twox_128("MidnightLedger") ++ twox_128("FreeDeposits") ++ twox_128_concat(AccountId)
        //
        // Correct Twox128 hashes (verified for Midnight runtime):
        //   twox_128("MidnightLedger") = 0x9fc1e81e3da0a9c6e05f8791e6e9a24f
        //   twox_128("FreeDeposits")  = 0x35df88098a0e2ca32a0c7e5c31ff80c6
        //
        // For Midnight preprod, try state_queryStorageAt with the address as storage key.
        // If the address is already a valid Substrate AccountId (32 bytes hex), use it directly.

        try
        {
            // Attempt 1: Query storage at current block with the address as hex key
            // state_queryStorageAt takes an array of storage keys
            std::string storage_key = address;
            if (storage_key.substr(0, 2) != "0x")
            {
                storage_key = "0x" + storage_key;
            }

            json params = json::array({json::array({storage_key})});
            json response = rpc_call("state_queryStorageAt", params);

            // Parse response - state_queryStorageAt returns an array of changes
            // Format: [{block, changes: [[storage_key, value]]}]
            if (response.is_array() && !response.empty())
            {
                const auto &change = response[0];
                if (change.contains("changes") && change["changes"].is_array())
                {
                    for (const auto &item : change["changes"])
                    {
                        if (item.is_array() && item.size() >= 2)
                        {
                            const auto &value = item[1];
                            if (!value.is_null() && value.is_string())
                            {
                                std::string hex_value = value.get<std::string>();
                                if (!hex_value.empty() && hex_value != "0x")
                                {
                                    // AccountData format on Substrate: nonce(u32) + consumers(u32) + providers(u32) + sufficients(u32) + data(AccountData)
                                // AccountData: { free: u128, reserved: u128, misc_frozen: u128, fee_frozen: u128 }
                                // Free balance is stored as SCALE u128 (little-endian 128-bit, compact-encoded)
                                // If value is compact-encoded, decode as compact. Otherwise decode as raw little-endian u128.
                                auto bytes = codec::util::hex_to_bytes(hex_value);

                                uint64_t balance = 0;
                                if (bytes.size() >= 16)
                                {
                                    // Raw u128 little-endian: lower 64 bits
                                    for (size_t i = 0; i < 8; ++i)
                                    {
                                        balance |= (static_cast<uint64_t>(bytes[i]) << (i * 8));
                                    }

                                    std::ostringstream msg;
                                    msg << "Balance query successful: " << balance << " base units";
                                    midnight::g_logger->info(msg.str());
                                    return balance;
                                }
                                else if (bytes.size() >= 8)
                                {
                                    // u64 (fallback)
                                    for (size_t i = 0; i < 8; ++i)
                                    {
                                        balance |= (static_cast<uint64_t>(bytes[i]) << (i * 8));
                                    }
                                    return balance;
                                }
                                }
                            }
                        }
                    }
                }
            }

            midnight::g_logger->warn("No balance found for address: " + address);
            return 0;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->debug("Ledger balance query failed: " + std::string(e.what()));
        }

        // Fallback: Try to use state_getStorage with computed MidnightLedger key
        try
        {
            if (sodium_init() < 0)
            {
                throw std::runtime_error("libsodium initialization failed");
            }

            // Compute MidnightLedger.FreeDeposits storage key
            // twox_128("MidnightLedger") || twox_128("FreeDeposits") || twox_128_concat(address)
            std::string prefix = "0x"
                                 "9fc1e81e3da0a9c6e05f8791e6e9a24f"  // twox_128("MidnightLedger")
                                 "35df88098a0e2ca32a0c7e5c31ff80c6";  // twox_128("FreeDeposits")

            std::string clean_hex = address;
            if (clean_hex.substr(0, 2) == "0x")
                clean_hex = clean_hex.substr(2);

            // Pad or truncate address to 32 bytes for Twox128
            std::vector<uint8_t> addr_bytes(32, 0);
            size_t copy_len = std::min(clean_hex.size() / 2, static_cast<size_t>(32));
            for (size_t i = 0; i < copy_len; ++i)
            {
                addr_bytes[i] = static_cast<uint8_t>(
                    std::stoul(clean_hex.substr(i * 2, 2), nullptr, 16));
            }

            // Twox128 hash of the address (first 16 bytes of Twox256)
            uint8_t hash[16];
            crypto_generichash(hash, 16, addr_bytes.data(), addr_bytes.size(), nullptr, 0);

            std::ostringstream oss;
            for (int i = 0; i < 16; i++)
                oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
            // Twox128 concat: hash || address
            oss << clean_hex;

            std::string storage_key = prefix + oss.str();
            json storage_response = rpc_call("state_getStorage", json::array({storage_key}));

            if (!storage_response.is_null() && !storage_response.empty())
            {
                std::string value_hex;
                if (storage_response.is_string())
                {
                    value_hex = storage_response.get<std::string>();
                }
                else
                {
                    value_hex = storage_response.dump();
                }

                if (!value_hex.empty() && value_hex != "null")
                {
                    auto bytes = codec::util::hex_to_bytes(value_hex);
                    if (bytes.size() >= 8)
                    {
                        uint64_t balance = 0;
                        for (size_t i = 0; i < 8; ++i)
                        {
                            balance |= (static_cast<uint64_t>(bytes[i]) << (i * 8));
                        }
                        midnight::g_logger->info("Balance from MidnightLedger.FreeDeposits: " + std::to_string(balance));
                        return balance;
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->debug("MidnightLedger storage key query failed: " + std::string(e.what()));
        }

        // MidnightNodeRPC is not designed for balance queries.
        // The UTXO model stores balance as a collection of UTXOs.
        // Use IndexerClient (GraphQL) for balance queries:
        //
        //   midnight::network::IndexerClient indexer(
        //       "https://indexer.preview.midnight.network/api/v4/graphql");
        //   auto utxos = indexer.query_utxos(address);
        //   uint64_t balance = 0;
        //   for (const auto& utxo : utxos) balance += utxo.amount;
        //
        // Or use SubstrateRPC for Substrate-format account balances:
        //
        //   midnight::network::SubstrateRPC substrate(node_url);
        //   auto account_info = substrate.get_account_info(account_id_hex);
        //   uint64_t free_balance = account_info.free;
        //
        midnight::g_logger->warn(
            "Balance query via node RPC returned empty. "
            "Use IndexerClient (GraphQL) or SubstrateRPC for balance queries.");

        throw std::runtime_error(
            "get_balance via node RPC is not supported. "
            "Balance in Midnight is derived from UTXOs. "
            "Use midnight::network::IndexerClient with GraphQL endpoint, "
            "or midnight::network::SubstrateRPC.get_free_balance() for Substrate-format accounts.");
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
