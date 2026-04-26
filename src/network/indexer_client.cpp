#include "midnight/network/indexer_client.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <regex>
#include <stdexcept>

namespace midnight::network
{

    // ────────────────────────────────────────────────
    // URL Parsing Helper
    // ────────────────────────────────────────────────

    void IndexerClient::parse_url(const std::string &url,
                                  std::string &scheme,
                                  std::string &host,
                                  uint16_t &port,
                                  std::string &path)
    {
        // Parse: scheme://host:port/path
        size_t scheme_end = url.find("://");
        if (scheme_end == std::string::npos)
        {
            scheme = "http";
            scheme_end = 0;
        }
        else
        {
            scheme = url.substr(0, scheme_end);
            scheme_end += 3;
        }

        size_t path_start = url.find('/', scheme_end);
        std::string host_port;
        if (path_start == std::string::npos)
        {
            host_port = url.substr(scheme_end);
            path = "/";
        }
        else
        {
            host_port = url.substr(scheme_end, path_start - scheme_end);
            path = url.substr(path_start);
        }

        size_t colon_pos = host_port.find(':');
        if (colon_pos == std::string::npos)
        {
            host = host_port;
            port = (scheme == "https") ? 443 : 80;
        }
        else
        {
            host = host_port.substr(0, colon_pos);
            port = static_cast<uint16_t>(std::stoi(host_port.substr(colon_pos + 1)));
        }
    }

    // ────────────────────────────────────────────────
    // Constructor
    // ────────────────────────────────────────────────

    IndexerClient::IndexerClient(const std::string &graphql_url, uint32_t timeout_ms)
        : graphql_url_(graphql_url), timeout_ms_(timeout_ms)
    {
        // Parse URL to create base URL for NetworkClient
        std::string scheme, host, path;
        uint16_t port;
        parse_url(graphql_url, scheme, host, port, path);

        std::string base_url = scheme + "://" + host + ":" + std::to_string(port);
        client_ = std::make_unique<NetworkClient>(base_url, timeout_ms);
        client_->set_header("Content-Type", "application/json");
        client_->set_header("Accept", "application/json");
    }

    // ────────────────────────────────────────────────
    // Core GraphQL Execution
    // ────────────────────────────────────────────────

    json IndexerClient::execute_graphql(const json &body)
    {
        std::string scheme, host, path;
        uint16_t port;
        parse_url(graphql_url_, scheme, host, port, path);

        try
        {
            auto response = client_->post_json(path, body);
            if (response.contains("errors") && response["errors"].is_array() && !response["errors"].empty())
            {
                std::string err_msg = "GraphQL error: ";
                for (const auto &err : response["errors"])
                {
                    if (err.contains("message"))
                    {
                        err_msg += err["message"].get<std::string>() + "; ";
                    }
                }
                midnight::g_logger->warn(err_msg);
            }
            return response;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("IndexerClient GraphQL request failed: " + std::string(e.what()));
            throw;
        }
    }

    json IndexerClient::graphql_query(const std::string &query, const json &variables)
    {
        json body = {{"query", query}};
        if (!variables.empty())
        {
            body["variables"] = variables;
        }

        auto response = execute_graphql(body);

        if (response.contains("data"))
        {
            return response["data"];
        }

        return json::object();
    }

    // ────────────────────────────────────────────────
    // Wallet Queries
    // ────────────────────────────────────────────────

    WalletState IndexerClient::query_wallet_state(const std::string &address)
    {
        WalletState state;
        state.address = address;

        try
        {
            // Indexer v4 API: The public indexer is governance-focused.
            // UTXO balance tracking is handled by the Wallet SDK locally.
            // We query what's available: block info and dust status.

            // 1. Get latest block to confirm sync
            const std::string block_query = R"(
                {
                    block {
                        hash
                        timestamp
                    }
                }
            )";

            auto block_data = graphql_query(block_query);
            if (block_data.contains("block") && !block_data["block"].is_null())
            {
                state.synced = true;
                auto &block = block_data["block"];
                if (block.contains("hash"))
                {
                    midnight::g_logger->info("Indexer synced, latest block: " +
                                             block["hash"].get<std::string>().substr(0, 20) + "...");
                }
            }

            // 2. Query dust generation status if Cardano reward address provided
            // Note: dustGenerationStatus requires cardanoRewardAddresses
            // For Midnight-native addresses, dust info comes from the Wallet SDK

            // Mark that balance must come from wallet SDK, not indexer
            state.unshielded_balance = "N/A (query via Wallet SDK)";
            state.dust_balance = "N/A (query via Wallet SDK)";
        }
        catch (const std::exception &e)
        {
            state.error = e.what();
            midnight::g_logger->error("Failed to query wallet state: " + state.error);
        }

        return state;
    }

    std::vector<blockchain::UTXO> IndexerClient::query_utxos(const std::string &address)
    {
        std::vector<blockchain::UTXO> utxos;

        try
        {
            // Indexer v4 API: UTXO queries are NOT available on the public indexer.
            // The public indexer is governance-focused (SPO, epochs, stake distribution).
            //
            // UTXO tracking is handled by the Wallet SDK locally:
            //   - DApp Connector syncs UTXOs via WebSocket subscription
            //   - levelPrivateStateProvider stores UTXO set locally
            //   - WalletFacade provides balance/UTXO accessors
            //
            // For contract-specific unshielded balances, use contractAction query.

            // Try contractAction if address looks like a contract
            const std::string query = R"(
                query ContractUnshielded($address: HexEncoded!) {
                    contractAction(address: $address) {
                        address
                        unshieldedBalances
                    }
                }
            )";

            json variables = {{"address", address}};
            auto data = graphql_query(query, variables);

            if (data.contains("contractAction") && !data["contractAction"].is_null())
            {
                auto &ca = data["contractAction"];
                if (ca.contains("unshieldedBalances") && ca["unshieldedBalances"].is_array())
                {
                    for (const auto &bal : ca["unshieldedBalances"])
                    {
                        blockchain::UTXO utxo;
                        utxo.address = address;
                        if (bal.is_object())
                        {
                            utxo.amount = bal.value("amount", uint64_t(0));
                            utxo.tx_hash = bal.value("tokenType", "");
                        }
                        utxos.push_back(std::move(utxo));
                    }
                }
            }

            if (utxos.empty())
            {
                midnight::g_logger->info(
                    "No UTXOs found via indexer for " + address +
                    ". User wallet UTXOs require local Wallet SDK sync.");
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("UTXO query via indexer: " + std::string(e.what()));
        }

        return utxos;
    }

    // ────────────────────────────────────────────────
    // Contract State Queries
    // ────────────────────────────────────────────────

    ContractState IndexerClient::query_contract_state(const std::string &contract_address)
    {
        ContractState state;
        state.contract_address = contract_address;

        try
        {
            // Indexer v4 API: Uses "contractAction" not "contractState"
            const std::string query = R"(
                query ContractAction($address: HexEncoded!) {
                    contractAction(address: $address) {
                        address
                        state
                        zswapState
                        transaction
                        unshieldedBalances
                    }
                }
            )";

            json variables = {{"address", contract_address}};
            auto data = graphql_query(query, variables);

            if (data.contains("contractAction") && !data["contractAction"].is_null())
            {
                state.exists = true;
                auto &ca = data["contractAction"];

                if (ca.contains("state"))
                {
                    state.ledger_state = ca["state"];
                }

                // Contract action doesn't have blockHeight directly
                // but we can get it from the block query
            }
        }
        catch (const std::exception &e)
        {
            state.error = e.what();
            midnight::g_logger->error("Failed to query contract state: " + state.error);
        }

        return state;
    }

    json IndexerClient::query_contract_fields(const std::string &contract_address,
                                              const std::vector<std::string> &fields)
    {
        // Build dynamic field selection
        std::ostringstream field_list;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (i > 0) field_list << " ";
            field_list << fields[i];
        }

        std::string query = R"(
            query ContractFields($address: String!) {
                contractState(address: $address) {
                    )" + field_list.str() + R"(
                }
            }
        )";

        json variables = {{"address", contract_address}};
        auto data = graphql_query(query, variables);

        if (data.contains("contractState") && !data["contractState"].is_null())
        {
            return data["contractState"];
        }

        return json::object();
    }

    // ────────────────────────────────────────────────
    // DUST Registration
    // ────────────────────────────────────────────────

    DustRegistrationStatus IndexerClient::query_dust_status(const std::string &address)
    {
        try
        {
            const std::string query = R"(
                query DustStatus($address: String!) {
                    dustRegistration(address: $address) {
                        status
                        registeredAt
                    }
                }
            )";

            json variables = {{"address", address}};
            auto data = graphql_query(query, variables);

            if (data.contains("dustRegistration") && !data["dustRegistration"].is_null())
            {
                auto status_str = data["dustRegistration"].value("status", "");

                if (status_str == "REGISTERED" || status_str == "active")
                    return DustRegistrationStatus::REGISTERED;
                if (status_str == "PENDING" || status_str == "pending")
                    return DustRegistrationStatus::PENDING;
                if (status_str == "EXPIRED" || status_str == "expired")
                    return DustRegistrationStatus::EXPIRED;

                return DustRegistrationStatus::UNKNOWN;
            }

            return DustRegistrationStatus::NOT_REGISTERED;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query DUST status: " + std::string(e.what()));
            return DustRegistrationStatus::UNKNOWN;
        }
    }

    // ────────────────────────────────────────────────
    // Block Height & Sync
    // ────────────────────────────────────────────────

    uint64_t IndexerClient::get_block_height()
    {
        try
        {
            const std::string query = R"({
                blockHeight {
                    height
                }
            })";

            auto data = graphql_query(query);

            if (data.contains("blockHeight") && data["blockHeight"].contains("height"))
            {
                return data["blockHeight"]["height"].get<uint64_t>();
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to get block height: " + std::string(e.what()));
        }

        return 0;
    }

    bool IndexerClient::is_synced()
    {
        try
        {
            const std::string query = R"({
                syncStatus {
                    synced
                    currentBlock
                    highestBlock
                }
            })";

            auto data = graphql_query(query);

            if (data.contains("syncStatus") && data["syncStatus"].contains("synced"))
            {
                return data["syncStatus"]["synced"].get<bool>();
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to check sync status: " + std::string(e.what()));
        }

        return false;
    }

} // namespace midnight::network
