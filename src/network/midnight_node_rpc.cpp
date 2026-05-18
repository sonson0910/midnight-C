#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/tx/extrinsic_builder.hpp"
#include "midnight/wallet/bech32m.hpp"
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <sodium.h>
#include <iomanip>
#include <cctype>
#include <optional>
#include <string_view>
#include <algorithm>
#include <httplib.h>

namespace midnight::network
{
    namespace
    {
        class RpcApplicationError : public std::runtime_error
        {
        public:
            explicit RpcApplicationError(const std::string &message)
                : std::runtime_error(message) {}
        };

        bool is_wallet_address(const std::string &value)
        {
            const auto lower = midnight::util::to_lower_copy(value);
            return lower.rfind("mn_", 0) == 0 || lower.rfind("stake", 0) == 0;
        }

        bool is_64_hex_chars(const std::string &value)
        {
            if (value.size() != 64)
                return false;
            for (unsigned char ch : value)
            {
                if (!std::isxdigit(ch))
                    return false;
            }
            return true;
        }

        bool starts_with(const std::string &value, std::string_view prefix)
        {
            return value.size() >= prefix.size() &&
                   std::string_view(value.data(), prefix.size()) == prefix;
        }

        std::string websocket_url_from_http_url(const std::string &url)
        {
            auto ensure_path = [](std::string value) {
                const auto scheme_pos = value.find("://");
                const auto host_start = scheme_pos == std::string::npos ? 0 : scheme_pos + 3;
                if (value.find('/', host_start) == std::string::npos)
                {
                    value.push_back('/');
                }
                return value;
            };

            if (starts_with(url, "https://"))
            {
                return ensure_path("wss://" + url.substr(8));
            }
            if (starts_with(url, "http://"))
            {
                return ensure_path("ws://" + url.substr(7));
            }
            if (starts_with(url, "wss://") || starts_with(url, "ws://"))
            {
                return ensure_path(url);
            }
            return ensure_path("wss://" + url);
        }

        bool should_try_websocket_submit_fallback(const std::string &error)
        {
            return error.find("HTTP 403") != std::string::npos ||
                   error.find("HTTP 413") != std::string::npos ||
                   error.find("Payload Too Large") != std::string::npos ||
                   error.find("Request Entity Too Large") != std::string::npos;
        }

        std::string truncate_rpc_error_detail(std::string value, std::size_t max_length)
        {
            if (value.size() <= max_length)
            {
                return value;
            }
            return value.substr(0, max_length) + "...";
        }

        std::optional<uint32_t> parse_midnight_custom_error_code(const std::string &value)
        {
            constexpr std::string_view prefix = "Custom error:";
            const auto pos = value.find(prefix);
            if (pos == std::string::npos)
            {
                return std::nullopt;
            }

            std::size_t idx = pos + prefix.size();
            while (idx < value.size() && std::isspace(static_cast<unsigned char>(value[idx])))
            {
                ++idx;
            }

            uint32_t code = 0;
            bool saw_digit = false;
            while (idx < value.size() && std::isdigit(static_cast<unsigned char>(value[idx])))
            {
                saw_digit = true;
                code = code * 10 + static_cast<uint32_t>(value[idx] - '0');
                ++idx;
            }
            if (!saw_digit)
            {
                return std::nullopt;
            }
            return code;
        }

        const char *midnight_custom_error_name(uint32_t code)
        {
            switch (code)
            {
            case 115:
                return "Malformed.InvalidProof";
            case 126:
                return "Malformed.Unbalanced";
            case 138:
                return "Malformed.BalanceCheckOverspend";
            case 166:
                return "Malformed.InvalidNetworkId";
            case 169:
                return "Malformed.InvalidDustRegistrationSignature";
            case 170:
                return "Malformed.InvalidDustSpendProof";
            case 171:
                return "Malformed.OutOfDustValidityWindow";
            case 172:
                return "Malformed.MultipleDustRegistrationsForKey";
            case 173:
                return "Malformed.InsufficientDustForRegistrationFee";
            case 175:
                return "Malformed.IntentSignatureVerificationFailure";
            case 179:
                return "Malformed.UnsupportedProofVersion";
            case 195:
                return "Invalid.InputNotInUtxos";
            case 196:
                return "Invalid.DustDoubleSpend";
            case 228:
                return "Malformed.TransactionApplication.IntentTtlExpired";
            case 229:
                return "Malformed.TransactionApplication.IntentTtlTooFarInFuture";
            case 230:
                return "Malformed.TransactionApplication.IntentAlreadyExists";
            case 235:
                return "Malformed.Zswap.InvalidProof";
            case 241:
                return "Invalid.Zswap.UnknownMerkleRoot";
            case 242:
                return "Invalid.ReplayProtectionViolation.IntentTtlExpired";
            case 243:
                return "Invalid.ReplayProtectionViolation.IntentTtlTooFarInFuture";
            case 244:
                return "Invalid.ReplayProtectionViolation.IntentAlreadyExists";
            default:
                return nullptr;
            }
        }

        std::string rpc_error_detail(const json &error_obj)
        {
            std::ostringstream detail;
            std::optional<uint32_t> custom_code;

            if (error_obj.contains("code"))
            {
                detail << "code=" << error_obj["code"].dump() << " ";
            }

            detail << "message="
                   << error_obj.value("message", "Unknown RPC error");

            if (error_obj.contains("data") && !error_obj["data"].is_null())
            {
                detail << " data=";
                if (error_obj["data"].is_string())
                {
                    const auto data = error_obj["data"].get<std::string>();
                    detail << data;
                    custom_code = parse_midnight_custom_error_code(data);
                }
                else
                {
                    detail << error_obj["data"].dump();
                }
            }

            if (custom_code)
            {
                detail << " midnight_error_code=" << *custom_code;
                if (const char *name = midnight_custom_error_name(*custom_code))
                {
                    detail << " midnight_error=" << name;
                }
                if (*custom_code == 171)
                {
                    detail
                        << " hint=refresh the local ledger-state/source cache tail and rebuild; "
                           "the DUST spend validity window was built from stale block time";
                }
            }

            return truncate_rpc_error_detail(detail.str(), 4096);
        }
    } // namespace

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

    json MidnightNodeRPC::get_block_header(const std::string &block_hash_input)
    {
        auto block_hash = midnight::util::strip_hex_prefix(block_hash_input);
        if (!is_64_hex_chars(block_hash))
        {
            throw std::runtime_error("Block hash must be a 32-byte hex string");
        }
        return rpc_call(
            "chain_getHeader",
            json::array({midnight::util::ensure_hex_prefix(block_hash)}));
    }

    bool MidnightNodeRPC::block_hash_exists(const std::string &block_hash)
    {
        const auto header = get_block_header(block_hash);
        return header.is_object() && !header.empty();
    }

    std::string MidnightNodeRPC::submit_transaction(const std::string &tx_hex)
    {
        const std::string clean_tx_hex = midnight::util::strip_hex_prefix(tx_hex);
        if (clean_tx_hex.empty())
        {
            throw std::runtime_error("Midnight transaction bytes cannot be empty");
        }
        if (!midnight::util::is_hex_string(clean_tx_hex))
        {
            throw std::runtime_error("Midnight transaction must be an even-length hex string");
        }

        const auto midnight_tx = midnight::util::hex_to_bytes(clean_tx_hex);
        const auto call = midnight::tx::PalletCall::midnight_send_mn_transaction(midnight_tx);

        midnight::tx::ExtrinsicParams params{};
        params.genesis_hash.assign(32, 0);
        params.block_hash.assign(32, 0);
        const midnight::tx::ExtrinsicBuilder builder(params);
        const auto extrinsic_bytes = builder.build_unsigned(call);
        const auto extrinsic_hex = midnight::util::ensure_hex_prefix(
            midnight::util::bytes_to_hex(extrinsic_bytes));

        midnight::g_logger->info(
            "Submitting Midnight ledger transaction via Midnight.send_mn_transaction unsigned extrinsic");
        return submit_extrinsic(extrinsic_hex);
    }

    std::string MidnightNodeRPC::submit_extrinsic(const std::string &extrinsic_hex)
    {
        const std::string clean_extrinsic_hex = midnight::util::strip_hex_prefix(extrinsic_hex);
        if (clean_extrinsic_hex.empty())
        {
            throw std::runtime_error("Extrinsic bytes cannot be empty");
        }
        if (!midnight::util::is_hex_string(clean_extrinsic_hex))
        {
            throw std::runtime_error("Extrinsic must be an even-length hex string");
        }

        const auto payload = midnight::util::ensure_hex_prefix(clean_extrinsic_hex);
        std::ostringstream msg;
        msg << "Submitting extrinsic (" << payload.length() << " chars)";
        midnight::g_logger->info(msg.str());

        json response;
        try
        {
            response = rpc_call("author_submitExtrinsic", json::array({payload}));
        }
        catch (const std::exception &e)
        {
            const std::string http_error = e.what();
            if (!should_try_websocket_submit_fallback(http_error))
            {
                throw std::runtime_error(std::string("author_submitExtrinsic failed: ") + http_error);
            }

            midnight::g_logger->warn(
                "HTTP author_submitExtrinsic was rejected by the RPC frontend; retrying over WebSocket");
            try
            {
                response = rpc_call_websocket("author_submitExtrinsic", json::array({payload}));
            }
            catch (const std::exception &ws_error)
            {
                throw std::runtime_error(
                    std::string("author_submitExtrinsic failed over HTTP and WebSocket fallback: http=") +
                    http_error + " websocket=" + ws_error.what());
            }
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

        midnight::g_logger->warn(
            "MidnightNodeRPC::get_balance is not supported for Midnight user wallets. "
            "Use IndexerClient::query_wallet_state() with the GraphQL indexer endpoint.");
        throw std::runtime_error(
            "Wallet NIGHT balance is not available through node RPC. "
            "Use midnight::network::IndexerClient::query_wallet_state().");
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
        // midnight_contractState expects a raw 32-byte contract address as hex
        // without a 0x prefix. User wallet Bech32m addresses are not contract
        // addresses and must be queried through the indexer instead.
        if (is_wallet_address(contract_address_input))
        {
            throw std::runtime_error(
                "midnight_contractState accepts contract hex addresses only; "
                "use IndexerClient for mn_addr/mn_shield-addr/mn_dust wallet addresses.");
        }

        std::string address_hex = midnight::util::strip_hex_prefix(contract_address_input);
        if (!is_64_hex_chars(address_hex))
        {
            throw std::runtime_error(
                "Invalid Midnight contract address: expected 32-byte hex string without Bech32m encoding");
        }

        try
        {
            json params = json::array({address_hex});
            json response = rpc_call("midnight_contractState", params);
            midnight::g_logger->info("Contract state query successful");
            return response;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("get_contract_state failed: " + std::string(e.what()));
            throw;
        }
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
                        const auto detail = rpc_error_detail(error_obj);
                        midnight::g_logger->error("RPC error [" + method + "]: " + detail);
                        throw RpcApplicationError("RPC " + method + " error: " + detail);
                    }

                    if (!response.contains("result"))
                    {
                        throw std::runtime_error("Invalid RPC response - missing result field");
                    }

                    return response["result"];
                }
                catch (const RpcApplicationError &)
                {
                    throw;
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

    json MidnightNodeRPC::rpc_call_websocket(
        const std::string &method,
        const json &params)
    {
        json request = json::object({{"jsonrpc", "2.0"},
                                     {"method", method},
                                     {"params", params},
                                     {"id", get_next_request_id()}});

        const auto websocket_url = websocket_url_from_http_url(client_->get_base_url());
        const int timeout_ms = static_cast<int>(client_->get_timeout_ms());
        midnight::g_logger->debug("WebSocket RPC call: " + method + " -> " + websocket_url);

        try
        {
            httplib::ws::WebSocketClient ws(websocket_url);
            ws.set_connection_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
            ws.set_read_timeout(std::max(1, timeout_ms / 1000), (timeout_ms % 1000) * 1000);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            ws.enable_server_certificate_verification(false);
#endif

            if (!ws.connect())
            {
                throw std::runtime_error("failed to open WebSocket connection");
            }

            if (!ws.send(request.dump()))
            {
                ws.close();
                throw std::runtime_error("failed to send WebSocket JSON-RPC request");
            }

            std::string response_body;
            const auto read_result = ws.read(response_body);
            ws.close();

            if (read_result != httplib::ws::ReadResult::Text)
            {
                throw std::runtime_error("WebSocket RPC did not return a text JSON-RPC response");
            }

            json response;
            try
            {
                response = json::parse(response_body);
            }
            catch (const std::exception &parse_error)
            {
                throw std::runtime_error(
                    std::string("Invalid WebSocket JSON-RPC response: ") + parse_error.what());
            }

            if (response.contains("error") && !response["error"].is_null())
            {
                const auto detail = rpc_error_detail(response["error"]);
                midnight::g_logger->error("WebSocket RPC error [" + method + "]: " + detail);
                throw RpcApplicationError("RPC " + method + " error: " + detail);
            }

            if (!response.contains("result"))
            {
                throw std::runtime_error("Invalid WebSocket RPC response - missing result field");
            }

            return response["result"];
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(
                "WebSocket RPC call failed: " + method + " - " + e.what());
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
