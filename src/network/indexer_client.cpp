#include "midnight/network/indexer_client.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <set>
#include <map>
#include <algorithm>

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
        std::string scheme, host, path;
        uint16_t port;
        parse_url(graphql_url, scheme, host, port, path);

        std::string base_url = scheme + "://" + host + ":" + std::to_string(port);
        if (!path.empty()) {
            size_t last_slash = path.find_last_of('/');
            if (last_slash != 0) {
                base_url += path.substr(0, last_slash);
            }
        }
        client_ = std::make_unique<NetworkClient>(base_url, timeout_ms);
        client_->set_header("Content-Type", "application/json");
        client_->set_header("Accept", "application/json");
    }

    // ────────────────────────────────────────────────
    // Cache Implementation
    // ────────────────────────────────────────────────

    bool IndexerClient::is_cache_valid(const CacheEntry& entry) const
    {
        if (!cache_enabled_) return false;
        auto age = std::chrono::steady_clock::now() - entry.timestamp;
        return age < std::chrono::seconds(cache_ttl_seconds_);
    }

    std::string IndexerClient::get_cache_key(const std::string& prefix, const std::string& value) const
    {
        return prefix + ":" + value;
    }

    void IndexerClient::prune_expired_cache()
    {
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - std::chrono::seconds(cache_ttl_seconds_);

        std::unique_lock<std::shared_mutex> lock(cache_mutex_);

        for (auto it = utxo_cache_.begin(); it != utxo_cache_.end(); ) {
            if (it->second.timestamp < cutoff) {
                it = utxo_cache_.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = wallet_cache_.begin(); it != wallet_cache_.end(); ) {
            if (it->second.timestamp < cutoff) {
                it = wallet_cache_.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = block_cache_.begin(); it != block_cache_.end(); ) {
            if (it->second.timestamp < cutoff) {
                it = block_cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void IndexerClient::clear_cache()
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex_);
        utxo_cache_.clear();
        wallet_cache_.clear();
        block_cache_.clear();
        known_spent_hashes_.clear();
        address_last_scan_block_.clear();
        midnight::g_logger->debug("IndexerClient cache cleared");
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
            size_t last_slash = path.find_last_of('/');
            std::string remaining_path = (last_slash != std::string::npos && last_slash < path.length() - 1)
                ? path.substr(last_slash)
                : "/graphql";

            auto response = client_->post_json(remaining_path, body);
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
        json body = json::object();
        body["query"] = query;
        if (!variables.empty())
        {
            body["variables"] = variables;
        }

        auto response = execute_graphql(body);

        if (response.contains("data"))
        {
            midnight::g_logger->debug("graphql_query: data_type=" +
                                    std::string(response["data"].is_null() ? "null" :
                                               response["data"].is_object() ? "object" :
                                               response["data"].is_array() ? "array" : "other"));
            return response["data"];
        }

        midnight::g_logger->warn("graphql_query: no 'data' key in response");
        return json::object();
    }

    // ────────────────────────────────────────────────
    // Wallet Queries (Optimized with caching)
    // ────────────────────────────────────────────────

    WalletState IndexerClient::query_wallet_state(const std::string &address, bool use_cache)
    {
        WalletState state;
        state.address = address;

        if (use_cache && cache_enabled_) {
            std::shared_lock<std::shared_mutex> lock(cache_mutex_);
            auto it = wallet_cache_.find(address);
            if (it != wallet_cache_.end() && is_cache_valid(it->second)) {
                midnight::g_logger->debug("Wallet state cache hit for " + address.substr(0, 20) + "...");
                if (it->second.data.is_object()) {
                    state.address = it->second.data.value("address", address);
                    state.unshielded_balance = it->second.data.value("unshielded_balance", "0");
                    state.shielded_balance = it->second.data.value("shielded_balance", "0");
                    state.dust_balance = it->second.data.value("dust_balance", "0");
                    state.available_utxo_count = it->second.data.value("available_utxo_count", 0u);
                    state.session_id = it->second.data.value("session_id", "");
                    state.synced = it->second.data.value("synced", false);
                    state.error = it->second.data.value("error", "");
                }
                return state;
            }
        }

        try
        {
            auto utxos = query_utxos(address, use_cache);

            uint64_t total_night = 0;
            uint64_t total_dust = 0;

            for (const auto &utxo : utxos)
            {
                // Use the decrypted value from the UTXO
                if (utxo.token_type == "NIGHT" || utxo.token_type.empty())
                {
                    total_night += utxo.value;
                }
                else if (utxo.token_type == "DUST")
                {
                    total_dust += utxo.value;
                }
                else
                {
                    total_night += utxo.value;
                }
            }

            state.unshielded_balance = std::to_string(total_night);
            state.dust_balance = std::to_string(total_dust);
            state.available_utxo_count = static_cast<uint32_t>(utxos.size());
            state.synced = true;

            midnight::g_logger->info("Wallet state for " + address.substr(0, 20) + "...: " +
                                     std::to_string(total_night) + " NIGHT, " +
                                     std::to_string(total_dust) + " DUST, " +
                                     std::to_string(utxos.size()) + " UTXOs");

            auto dust_status = query_dust_status(address);
            if (dust_status == DustRegistrationStatus::REGISTERED)
            {
                midnight::g_logger->info("Dust registered: yes");
            }

            if (use_cache && cache_enabled_) {
                std::unique_lock<std::shared_mutex> lock(cache_mutex_);
                json cache_data = json::object();
                cache_data["address"] = state.address;
                cache_data["unshielded_balance"] = state.unshielded_balance;
                cache_data["dust_balance"] = state.dust_balance;
                cache_data["available_utxo_count"] = state.available_utxo_count;
                cache_data["synced"] = state.synced;
                cache_data["error"] = state.error;
                wallet_cache_[address] = {cache_data, std::chrono::steady_clock::now()};
            }
        }
        catch (const std::exception &e)
        {
            state.error = e.what();
            state.synced = false;
            midnight::g_logger->error("Failed to query wallet state: " + state.error);
        }

        return state;
    }

    // ────────────────────────────────────────────────
    // UTXO Query (Optimized with caching & batch)
    // ────────────────────────────────────────────────

    std::vector<blockchain::UTXO> IndexerClient::query_utxos(const std::string &address, bool use_cache)
    {
        constexpr uint32_t FIRST_INDEXED_BLOCK = 609178u;

        if (use_cache && cache_enabled_) {
            std::shared_lock<std::shared_mutex> lock(cache_mutex_);
            auto it = utxo_cache_.find(address);
                if (it != utxo_cache_.end() && is_cache_valid(it->second)) {
                midnight::g_logger->debug("UTXO cache hit for " + address.substr(0, 20) + "...");
                std::vector<blockchain::UTXO> result;
                if (it->second.data.is_array()) {
                    for (const auto& item : it->second.data) {
                        blockchain::UTXO utxo;
                        utxo.tx_hash = item.value("tx_hash", "");
                        utxo.output_index = item.value("output_index", 0u);
                        utxo.amount_commitment = item.value("amount_commitment", "");
                        utxo.value = item.value("value", 0ULL);
                        utxo.address = item.value("address", "");
                        result.push_back(utxo);
                    }
                }
                return result;
            }
        }

        uint32_t tip_height = FIRST_INDEXED_BLOCK;
        try {
            constexpr const char* tip_query = R"(
                query GetTip {
                    block { height }
                }
            )";
            json tip_result = graphql_query(tip_query, json::object());
            if (tip_result.contains("block") && !tip_result["block"].is_null())
            {
                tip_height = tip_result["block"].value("height", FIRST_INDEXED_BLOCK);
            }
        } catch (...) {}

        uint32_t last_scan_block = FIRST_INDEXED_BLOCK;
        {
            std::shared_lock<std::shared_mutex> lock(cache_mutex_);
            auto it = address_last_scan_block_.find(address);
            if (it != address_last_scan_block_.end()) {
                last_scan_block = it->second;
            }
        }

        if (last_scan_block >= tip_height) {
            midnight::g_logger->debug("UTXO already scanned to tip for " + address.substr(0, 20) + "...");
            std::vector<blockchain::UTXO> empty_result;
            if (use_cache && cache_enabled_) {
                std::unique_lock<std::shared_mutex> lock(cache_mutex_);
                json cache_data = json::array();
                utxo_cache_[address] = {cache_data, std::chrono::steady_clock::now()};
            }
            return empty_result;
        }

        auto utxos = scan_utxos_internal(address, last_scan_block + 1, tip_height, use_cache);

        {
            std::unique_lock<std::shared_mutex> lock(cache_mutex_);
            address_last_scan_block_[address] = tip_height;
        }

        if (use_cache && cache_enabled_) {
            std::unique_lock<std::shared_mutex> lock(cache_mutex_);
            json cache_data = json::array();
            for (const auto& utxo : utxos) {
                json item = json::object();
                item["tx_hash"] = utxo.tx_hash;
                item["output_index"] = utxo.output_index;
                item["amount_commitment"] = utxo.amount_commitment;
                item["value"] = utxo.value;
                item["address"] = utxo.address;
                cache_data.push_back(item);
            }
            utxo_cache_[address] = {cache_data, std::chrono::steady_clock::now()};
        }

        return utxos;
    }

    std::vector<blockchain::UTXO> IndexerClient::scan_utxos_internal(const std::string &address,
                                                                      uint32_t from_block,
                                                                      uint32_t to_block,
                                                                      bool /* use_cache */)
    {
        std::vector<blockchain::UTXO> utxos;

        try
        {
            midnight::g_logger->info("Scanning UTXOs blocks " +
                                    std::to_string(from_block) + "-" +
                                    std::to_string(to_block) + " for " +
                                    address.substr(0, 20) + "...");

            // Correct query: use block(offset: { height }) per block
            // Based on midnight-research indexer API v4
            constexpr const char* block_query = R"(
                query GetBlock($height: Int!) {
                    block(offset: { height: $height }) {
                        height
                        transactions {
                            id
                            hash
                            unshieldedCreatedOutputs {
                                owner
                                value
                                tokenType
                                outputIndex
                            }
                            unshieldedSpentOutputs {
                                owner
                                value
                                tokenType
                            }
                        }
                    }
                }
            )";

            std::set<std::string> spent_tx_hashes;
            std::map<std::pair<std::string, uint32_t>, uint64_t> created_amounts;
            uint32_t blocks_scanned = 0;

            for (uint32_t h = from_block; h <= to_block; ++h)
            {
                json variables = json::object();
                variables["height"] = static_cast<int>(h);
                json result = graphql_query(block_query, variables);

                if (!result.contains("data") || result["data"].is_null() ||
                    !result["data"].contains("block") || result["data"]["block"].is_null())
                {
                    continue;
                }

                const auto& block_data = result["data"]["block"];
                blocks_scanned++;

                if (!block_data.contains("transactions") || block_data["transactions"].is_null())
                    continue;

                const auto& txns = block_data["transactions"];
                std::vector<json> tx_list = txns.is_array()
                                                ? std::vector<json>(txns.begin(), txns.end())
                                                : std::vector<json>{txns};

                for (const auto& tx : tx_list)
                {
                    std::string tx_hash = tx.value("hash", "");

                    if (tx.contains("unshieldedSpentOutputs") && !tx["unshieldedSpentOutputs"].is_null())
                    {
                        for (const auto& sout : tx["unshieldedSpentOutputs"])
                        {
                            if (sout.value("owner", "") == address)
                            {
                                spent_tx_hashes.insert(tx_hash);
                            }
                        }
                    }

                    if (tx.contains("unshieldedCreatedOutputs") && !tx["unshieldedCreatedOutputs"].is_null())
                    {
                        for (const auto& out : tx["unshieldedCreatedOutputs"])
                        {
                            if (out.value("owner", "") == address)
                            {
                                uint32_t out_idx = out.value("outputIndex", 0u);
                                uint64_t amount = std::stoull(out.value("value", "0"));
                                created_amounts[{tx_hash, out_idx}] = amount;
                            }
                        }
                    }
                }

                if (blocks_scanned % 500 == 0)
                {
                    midnight::g_logger->info("  UTXO scan progress: " +
                                            std::to_string(blocks_scanned) + " blocks scanned");
                }
            }

            midnight::g_logger->info("Block scan complete: " + std::to_string(blocks_scanned) +
                                    " blocks, " + std::to_string(spent_tx_hashes.size()) +
                                    " spent txs, " + std::to_string(created_amounts.size()) + " created");

            for (const auto& entry : created_amounts)
            {
                const std::string& created_tx_hash = entry.first.first;
                uint32_t created_out_idx = entry.first.second;
                uint64_t created_amount = entry.second;

                if (spent_tx_hashes.find(created_tx_hash) != spent_tx_hashes.end())
                    continue;

                blockchain::UTXO utxo;
                utxo.tx_hash = created_tx_hash;
                utxo.output_index = created_out_idx;
                utxo.value = created_amount;
                utxo.address = address;
                utxos.push_back(utxo);
            }

            uint64_t total_unspent = 0;
            for (const auto& u : utxos)
                total_unspent += u.value;

            midnight::g_logger->info("UTXO scan result: " + std::to_string(utxos.size()) +
                                    " unspent UTXOs, " + std::to_string(total_unspent / 1000000) + ".000000 NIGHT");
        }
        catch (const std::exception& e)
        {
            midnight::g_logger->error("UTXO scan failed: " + std::string(e.what()));
        }

        return utxos;
    }

    std::vector<blockchain::UTXO> IndexerClient::query_utxos(const std::string &address,
                                                            uint32_t from_block,
                                                            uint32_t to_block)
    {
        return scan_utxos_internal(address, from_block, to_block, false);
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
            const std::string query = R"(
                query ContractAction($address: HexEncoded!) {
                    contractAction(address: $address) {
                        address
                        state
                        zswapState
                        transaction {
                            block {
                                height
                                hash
                            }
                        }
                        unshieldedBalances {
                            tokenType
                            amount
                        }
                    }
                }
            )";

            json variables = json::object();
            variables["address"] = contract_address;
            auto data = graphql_query(query, variables);

            if (data.contains("contractAction") && !data["contractAction"].is_null())
            {
                state.exists = true;
                auto &ca = data["contractAction"];

                if (ca.contains("state"))
                {
                    state.ledger_state = ca["state"];
                }

                if (ca.contains("unshieldedBalances") && !ca["unshieldedBalances"].is_null())
                {
                    state.unshielded_balances = ca["unshieldedBalances"];
                }

                if (ca.contains("transaction") && !ca["transaction"].is_null())
                {
                    auto &tx = ca["transaction"];
                    if (tx.contains("block") && !tx["block"].is_null())
                    {
                        state.block_height = tx["block"].value("height", 0);
                        state.block_hash = tx["block"].value("hash", "");
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            state.error = e.what();
            midnight::g_logger->error("Failed to query contract state: " + state.error);
        }

        return state;
    }

    ContractAction IndexerClient::query_contract_action(const std::string &contract_address)
    {
        ContractAction action;
        action.address = contract_address;

        try
        {
            const std::string query = R"(
                query ContractAction($address: HexEncoded!) {
                    contractAction(address: $address) {
                        __typename
                        address
                        state
                        entryPoint
                        unshieldedBalances {
                            tokenType
                            amount
                        }
                        transaction {
                            block {
                                height
                                hash
                            }
                        }
                    }
                }
            )";

            json variables = json::object();
            variables["address"] = contract_address;
            auto data = graphql_query(query, variables);

            if (data.contains("contractAction") && !data["contractAction"].is_null())
            {
                auto &ca = data["contractAction"];

                if (ca.contains("__typename"))
                {
                    std::string typename_str = ca["__typename"].get<std::string>();
                    if (typename_str == "ContractDeploy")
                        action.action_type = "deploy";
                    else if (typename_str == "ContractCall")
                        action.action_type = "call";
                    else if (typename_str == "ContractUpdate")
                        action.action_type = "update";
                }

                if (ca.contains("state"))
                {
                    action.state = ca["state"];
                }

                if (ca.contains("entryPoint") && !ca["entryPoint"].is_null())
                {
                    action.entry_point = ca["entryPoint"].get<std::string>();
                }

                if (ca.contains("unshieldedBalances") && !ca["unshieldedBalances"].is_null())
                {
                    action.unshielded_balances = ca["unshieldedBalances"];
                }

                if (ca.contains("transaction") && !ca["transaction"].is_null())
                {
                    auto &tx = ca["transaction"];
                    if (tx.contains("block") && !tx["block"].is_null())
                    {
                        action.block_height = tx["block"].value("height", 0);
                        action.block_hash = tx["block"].value("hash", "");
                    }
                }

                midnight::g_logger->info("Contract action for " + contract_address.substr(0, 20) +
                                        "...: type=" + action.action_type);
            }
        }
        catch (const std::exception &e)
        {
            action.error = e.what();
            midnight::g_logger->error("Failed to query contract action: " + action.error);
        }

        return action;
    }

    std::vector<ContractBalance> IndexerClient::query_contract_balance(
        const std::string &contract_address)
    {
        std::vector<ContractBalance> balances;

        try
        {
            const std::string query = R"(
                query ContractBalance($address: HexEncoded!) {
                    contractAction(address: $address) {
                        unshieldedBalances {
                            tokenType
                            amount
                        }
                    }
                }
            )";

            json variables = json::object();
            variables["address"] = contract_address;
            auto data = graphql_query(query, variables);

            if (data.contains("contractAction") && !data["contractAction"].is_null())
            {
                auto &ca = data["contractAction"];
                if (ca.contains("unshieldedBalances") && !ca["unshieldedBalances"].is_null())
                {
                    const auto &bal_array = ca["unshieldedBalances"];
                    if (bal_array.is_array())
                    {
                        for (const auto &bal : bal_array)
                        {
                            ContractBalance cb;
                            cb.token_type = bal.value("tokenType", "");
                            cb.amount = bal.value("amount", "0");
                            balances.push_back(cb);
                        }
                    }
                }
            }

            midnight::g_logger->debug("Contract " + contract_address.substr(0, 20) +
                                    "... has " + std::to_string(balances.size()) + " balance entries");
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query contract balance: " + std::string(e.what()));
        }

        return balances;
    }

    json IndexerClient::query_contract_fields(const std::string &contract_address,
                                              const std::vector<std::string> &fields)
    {
        std::ostringstream field_list;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (i > 0) field_list << " ";
            field_list << fields[i];
        }

        std::string query = R"(
            query ContractFields($address: HexEncoded!) {
                contractAction(address: $address) {
                    )" + field_list.str() + R"(
                }
            }
        )";

        json variables = json::object();
        variables["address"] = contract_address;
        auto data = graphql_query(query, variables);

        if (data.contains("contractAction") && !data["contractAction"].is_null())
        {
            return data["contractAction"];
        }

        return json::object();
    }

    // ────────────────────────────────────────────────
    // Transaction Queries
    // ────────────────────────────────────────────────

    json IndexerClient::query_transaction(const std::string &tx_hash)
    {
        if (tx_hash.empty())
        {
            return json::object();
        }

        std::string clean_hash = tx_hash;
        if (clean_hash.substr(0, 2) == "0x")
            clean_hash = clean_hash.substr(2);

        const std::string query = R"(
            query TransactionByHash($hash: HexEncoded!) {
                transactions(offset: { hash: $hash }) {
                    id
                    hash
                    block {
                        height
                        hash
                    }
                    contractActions {
                        __typename
                        address
                        state
                        zswapState
                    }
                    unshieldedCreatedOutputs {
                        owner
                        value
                        tokenType
                        outputIndex
                    }
                    unshieldedSpentOutputs {
                        owner
                        value
                        tokenType
                    }
                }
            }
        )";

        json variables = json::object();
        variables["hash"] = "0x" + clean_hash;

        try
        {
            auto data = graphql_query(query, variables);
            if (data.contains("transactions") && data["transactions"].is_array())
            {
                const auto &txs = data["transactions"];
                if (!txs.empty() && !txs[0].is_null())
                {
                    return txs[0];
                }
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("Transaction query failed: " + std::string(e.what()));
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
            std::string lower_addr;
            lower_addr.reserve(address.size());
            for (char c : address) {
                lower_addr += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            bool is_stake_address = (lower_addr.rfind("stake", 0) == 0);

            if (is_stake_address)
            {
                const std::string query = R"(
                    query DustStatus($cardanoRewardAddresses: [CardanoRewardAddress!]!) {
                        dustGenerationStatus(cardanoRewardAddresses: $cardanoRewardAddresses) {
                            cardanoRewardAddress
                            registered
                            nightBalance
                            generationRate
                            maxCapacity
                            currentCapacity
                        }
                    }
                )";
                json variables = json::object();
                variables["cardanoRewardAddresses"] = json::array({address});

                auto data = graphql_query(query, variables);

                if (data.contains("dustGenerationStatus") &&
                    data["dustGenerationStatus"].is_array() &&
                    !data["dustGenerationStatus"].empty())
                {
                    for (const auto &dust : data["dustGenerationStatus"])
                    {
                        std::string cardano_addr;
                        if (dust.contains("cardanoRewardAddress"))
                        {
                            try { cardano_addr = dust["cardanoRewardAddress"].get<std::string>(); } catch (...) {}
                        }
                        if (cardano_addr == address)
                        {
                            bool registered = dust.value("registered", false);
                            return registered ? DustRegistrationStatus::REGISTERED : DustRegistrationStatus::NOT_REGISTERED;
                        }
                    }
                    return DustRegistrationStatus::NOT_REGISTERED;
                }
                return DustRegistrationStatus::NOT_REGISTERED;
            }
            else
            {
                midnight::g_logger->debug(
                    "DUST status query: address is not a Cardano stake address.");
                return DustRegistrationStatus::UNKNOWN;
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query DUST status: " + std::string(e.what()));
            return DustRegistrationStatus::UNKNOWN;
        }
    }

    DustGenerationStatus IndexerClient::query_dust_generation_status(const std::string &cardano_reward_address)
    {
        DustGenerationStatus status;
        status.cardano_reward_address = cardano_reward_address;

        try
        {
            std::string lower_addr;
            lower_addr.reserve(cardano_reward_address.size());
            for (char c : cardano_reward_address) {
                lower_addr += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (lower_addr.rfind("stake", 0) != 0)
            {
                midnight::g_logger->warn(
                    "query_dust_generation_status: address is not a Cardano stake address: " +
                    cardano_reward_address);
                return status;
            }

            const std::string query = R"(
                query DustGenerationStatus($cardanoRewardAddresses: [CardanoRewardAddress!]!) {
                    dustGenerationStatus(cardanoRewardAddresses: $cardanoRewardAddresses) {
                        cardanoRewardAddress
                        dustAddress
                        registered
                        nightBalance
                        generationRate
                        maxCapacity
                        currentCapacity
                        utxoTxHash
                        utxoOutputIndex
                    }
                }
            )";

            json variables = json::object();
            variables["cardanoRewardAddresses"] = json::array({cardano_reward_address});

            auto data = graphql_query(query, variables);

            if (data.contains("dustGenerationStatus") &&
                data["dustGenerationStatus"].is_array() &&
                !data["dustGenerationStatus"].empty())
            {
                const auto &dust = data["dustGenerationStatus"][0];
                status.cardano_reward_address = dust.value("cardanoRewardAddress", cardano_reward_address);
                status.dust_address = dust.value("dustAddress", "");
                status.registered = dust.value("registered", false);
                status.night_balance = dust.value("nightBalance", "0");
                status.generation_rate = dust.value("generationRate", "0");
                status.max_capacity = dust.value("maxCapacity", "0");
                status.current_capacity = dust.value("currentCapacity", "0");
                status.utxo_tx_hash = dust.value("utxoTxHash", "");
                status.utxo_output_index = dust.value("utxoOutputIndex", 0u);

                midnight::g_logger->info("DUST generation status for " +
                    cardano_reward_address.substr(0, 20) + "...: registered=" +
                    std::string(status.registered ? "true" : "false") +
                    ", dust_addr=" + status.dust_address);
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query DUST generation status: " + std::string(e.what()));
        }

        return status;
    }

    std::vector<DustGenerationStatus> IndexerClient::query_dust_generation_statuses(
        const std::vector<std::string> &cardano_reward_addresses)
    {
        std::vector<DustGenerationStatus> results;

        if (cardano_reward_addresses.empty())
        {
            return results;
        }

        try
        {
            const std::string query = R"(
                query DustGenerationStatuses($cardanoRewardAddresses: [CardanoRewardAddress!]!) {
                    dustGenerationStatus(cardanoRewardAddresses: $cardanoRewardAddresses) {
                        cardanoRewardAddress
                        dustAddress
                        registered
                        nightBalance
                        generationRate
                        maxCapacity
                        currentCapacity
                        utxoTxHash
                        utxoOutputIndex
                    }
                }
            )";

            json variables = json::object();
            variables["cardanoRewardAddresses"] = json::array();
            for (const auto &addr : cardano_reward_addresses)
            {
                variables["cardanoRewardAddresses"].push_back(addr);
            }

            auto data = graphql_query(query, variables);

            if (data.contains("dustGenerationStatus") &&
                data["dustGenerationStatus"].is_array())
            {
                for (const auto &dust : data["dustGenerationStatus"])
                {
                    DustGenerationStatus status;
                    status.cardano_reward_address = dust.value("cardanoRewardAddress", "");
                    status.dust_address = dust.value("dustAddress", "");
                    status.registered = dust.value("registered", false);
                    status.night_balance = dust.value("nightBalance", "0");
                    status.generation_rate = dust.value("generationRate", "0");
                    status.max_capacity = dust.value("maxCapacity", "0");
                    status.current_capacity = dust.value("currentCapacity", "0");
                    status.utxo_tx_hash = dust.value("utxoTxHash", "");
                    status.utxo_output_index = dust.value("utxoOutputIndex", 0u);
                    results.push_back(status);
                }

                midnight::g_logger->info("DUST generation statuses for " +
                    std::to_string(results.size()) + " addresses");
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query DUST generation statuses: " + std::string(e.what()));
        }

        return results;
    }

    std::vector<DustRegistration> IndexerClient::query_dust_registrations(const std::string &cardano_reward_address)
    {
        std::vector<DustRegistration> registrations;

        try
        {
            std::string lower_addr;
            lower_addr.reserve(cardano_reward_address.size());
            for (char c : cardano_reward_address) {
                lower_addr += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (lower_addr.rfind("stake", 0) != 0)
            {
                midnight::g_logger->warn(
                    "query_dust_registrations: address is not a Cardano stake address: " +
                    cardano_reward_address);
                return registrations;
            }

            const std::string query = R"(
                query DustRegistrations($cardanoRewardAddresses: [CardanoRewardAddress!]!) {
                    dustGenerations(cardanoRewardAddresses: $cardanoRewardAddresses) {
                        cardanoRewardAddress
                        registrations {
                            dustAddress
                            valid
                            nightBalance
                            generationRate
                        }
                    }
                }
            )";

            json variables = json::object();
            variables["cardanoRewardAddresses"] = json::array({cardano_reward_address});

            auto data = graphql_query(query, variables);

            if (data.contains("dustGenerations") &&
                data["dustGenerations"].is_array() &&
                !data["dustGenerations"].empty())
            {
                const auto &generations = data["dustGenerations"][0];
                if (generations.contains("registrations") &&
                    generations["registrations"].is_array())
                {
                    for (const auto &reg : generations["registrations"])
                    {
                        DustRegistration registration;
                        registration.dust_address = reg.value("dustAddress", "");
                        registration.valid = reg.value("valid", false);
                        registration.night_balance = reg.value("nightBalance", "0");
                        registration.generation_rate = reg.value("generationRate", "0");
                        registrations.push_back(registration);
                    }
                }

                midnight::g_logger->info("DUST registrations for " +
                    cardano_reward_address.substr(0, 20) + "...: " +
                    std::to_string(registrations.size()) + " registrations");
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query DUST registrations: " + std::string(e.what()));
        }

        return registrations;
    }

    // ────────────────────────────────────────────────
    // Block Height & Sync
    // ────────────────────────────────────────────────

    uint64_t IndexerClient::get_block_height()
    {
        try
        {
            const std::string query = R"({
                block {
                    hash
                    height
                    timestamp
                }
            })";

            auto data = graphql_query(query);

            if (data.contains("block") && !data["block"].is_null())
            {
                const auto &block = data["block"];
                std::string block_hash = block.value("hash", "");

                if (!block_hash.empty())
                {
                    midnight::g_logger->info("Indexer synced, latest block: " +
                                             block_hash.substr(0, 20) + "...");

                    if (block.contains("height") && !block["height"].is_null())
                    {
                        try {
                            if (block["height"].is_number()) {
                                last_tip_height_ = block["height"].get<uint64_t>();
                                return last_tip_height_;
                            } else if (block["height"].is_string()) {
                                last_tip_height_ = std::stoull(block["height"].get<std::string>());
                                return last_tip_height_;
                            }
                        } catch (...) {
                            midnight::g_logger->warn("Failed to parse block height");
                        }
                    }
                }
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

    // ────────────────────────────────────────────────
    // Shielded Wallet Support
    // ────────────────────────────────────────────────

    ShieldedSession IndexerClient::connect_shielded_wallet(const std::string &viewing_key,
                                                         uint64_t start_index)
    {
        ShieldedSession session;
        session.start_index = start_index;

        if (viewing_key.empty())
        {
            midnight::g_logger->error("Cannot connect shielded wallet: viewing key is empty");
            return session;
        }

        try
        {
            const std::string mutation = R"(
                mutation Connect($viewingKey: String!, $options: ConnectOptions) {
                    connect(viewingKey: $viewingKey, options: $options)
                }
            )";

            json variables = json::object();
            variables["viewingKey"] = viewing_key;

            json options = json::object();
            options["startIndex"] = static_cast<int64_t>(start_index);
            variables["options"] = options;

            auto data = graphql_query(mutation, variables);

            if (data.contains("connect") && !data["connect"].is_null())
            {
                session.session_id = data["connect"].get<std::string>();
                session.is_connected = !session.session_id.empty();

                midnight::g_logger->info("Shielded wallet connected: " +
                                        session.session_id.substr(0, 20) + "... (start_index=" +
                                        std::to_string(start_index) + ")");
            }
            else
            {
                midnight::g_logger->error("Shielded wallet connect failed: no session_id returned");
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to connect shielded wallet: " + std::string(e.what()));
        }

        return session;
    }

    bool IndexerClient::disconnect_shielded_wallet(const std::string &session_id)
    {
        if (session_id.empty())
        {
            midnight::g_logger->error("Cannot disconnect: session_id is empty");
            return false;
        }

        try
        {
            const std::string mutation = R"(
                mutation Disconnect($sessionId: String!) {
                    disconnect(sessionId: $sessionId)
                }
            )";

            json variables = json::object();
            variables["sessionId"] = session_id;

            auto data = graphql_query(mutation, variables);

            if (data.contains("disconnect"))
            {
                bool success = data["disconnect"].get<bool>();
                if (success)
                {
                    midnight::g_logger->info("Shielded wallet disconnected: " + session_id.substr(0, 20) + "...");
                }
                else
                {
                    midnight::g_logger->warn("Disconnect returned false for session: " + session_id.substr(0, 20) + "...");
                }
                return success;
            }

            midnight::g_logger->error("Disconnect failed: no response");
            return false;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to disconnect shielded wallet: " + std::string(e.what()));
            return false;
        }
    }

    std::string IndexerClient::query_shielded_balance(const std::string &session_id)
    {
        if (session_id.empty())
        {
            midnight::g_logger->error("Cannot query shielded balance: session_id is empty");
            return "0";
        }

        try
        {
            const std::string query = R"(
                query ShieldedBalance($sessionId: String!) {
                    shieldedBalance(sessionId: $sessionId) {
                        balance
                        pending
                        currency
                    }
                }
            )";

            json variables = json::object();
            variables["sessionId"] = session_id;

            auto data = graphql_query(query, variables);

            if (data.contains("shieldedBalance") && !data["shieldedBalance"].is_null())
            {
                auto &balance_data = data["shieldedBalance"];
                if (balance_data.contains("balance"))
                {
                    return balance_data["balance"].get<std::string>();
                }
            }

            midnight::g_logger->debug("No shielded balance found for session");
            return "0";
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query shielded balance: " + std::string(e.what()));
            return "0";
        }
    }

    // ────────────────────────────────────────────────
    // Nullifier Queries (v4.1.0)
    // ────────────────────────────────────────────────

    std::vector<DustNullifierTransaction> IndexerClient::query_dust_nullifiers(
        const std::vector<std::string> &nullifier_prefixes,
        uint64_t from_block,
        uint64_t to_block)
    {
        std::vector<DustNullifierTransaction> results;

        if (nullifier_prefixes.empty())
        {
            midnight::g_logger->warn("query_dust_nullifiers called with empty prefixes");
            return results;
        }

        try
        {
            const std::string query = R"(
                query DustNullifiers($nullifierPrefixes: [String!]!, $fromBlock: Long, $toBlock: Long) {
                    dustNullifierTransactions(
                        nullifierPrefixes: $nullifierPrefixes,
                        fromBlock: $fromBlock,
                        toBlock: $toBlock
                    ) {
                        nullifier
                        commitment
                        transactionId
                        blockHeight
                        blockHash
                    }
                }
            )";

            json variables = json::object();
            variables["nullifierPrefixes"] = nullifier_prefixes;
            variables["fromBlock"] = static_cast<int64_t>(from_block);
            if (to_block > 0)
            {
                variables["toBlock"] = static_cast<int64_t>(to_block);
            }

            auto data = graphql_query(query, variables);

            if (data.contains("dustNullifierTransactions") &&
                data["dustNullifierTransactions"].is_array())
            {
                for (const auto &item : data["dustNullifierTransactions"])
                {
                    DustNullifierTransaction tx;
                    tx.nullifier = item.value("nullifier", "");
                    tx.commitment = item.value("commitment", "");

                    if (item.contains("transactionId") && !item["transactionId"].is_null())
                    {
                        try {
                            if (item["transactionId"].is_number()) {
                                tx.transaction_id = item["transactionId"].get<uint64_t>();
                            } else {
                                tx.transaction_id = std::stoull(item["transactionId"].get<std::string>());
                            }
                        } catch (...) {}
                    }

                    if (item.contains("blockHeight") && !item["blockHeight"].is_null())
                    {
                        try {
                            if (item["blockHeight"].is_number()) {
                                tx.block_height = item["blockHeight"].get<uint64_t>();
                            } else {
                                tx.block_height = std::stoull(item["blockHeight"].get<std::string>());
                            }
                        } catch (...) {}
                    }

                    tx.block_hash = item.value("blockHash", "");
                    results.push_back(tx);
                }
            }

            midnight::g_logger->info("Dust nullifiers query returned " +
                                    std::to_string(results.size()) + " results");
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query dust nullifiers: " + std::string(e.what()));
        }

        return results;
    }

    std::vector<ShieldedNullifierTransaction> IndexerClient::query_shielded_nullifiers(
        const std::vector<std::string> &nullifier_prefixes,
        uint64_t from_block,
        uint64_t to_block)
    {
        std::vector<ShieldedNullifierTransaction> results;

        if (nullifier_prefixes.empty())
        {
            midnight::g_logger->warn("query_shielded_nullifiers called with empty prefixes");
            return results;
        }

        try
        {
            const std::string query = R"(
                query ShieldedNullifiers($nullifierPrefixes: [String!]!, $fromBlock: Long, $toBlock: Long) {
                    shieldedNullifierTransactions(
                        nullifierPrefixes: $nullifierPrefixes,
                        fromBlock: $fromBlock,
                        toBlock: $toBlock
                    ) {
                        transactionId
                        blockHash
                        blockHeight
                        nullifier
                    }
                }
            )";

            json variables = json::object();
            variables["nullifierPrefixes"] = nullifier_prefixes;
            variables["fromBlock"] = static_cast<int64_t>(from_block);
            if (to_block > 0)
            {
                variables["toBlock"] = static_cast<int64_t>(to_block);
            }

            auto data = graphql_query(query, variables);

            if (data.contains("shieldedNullifierTransactions") &&
                data["shieldedNullifierTransactions"].is_array())
            {
                for (const auto &item : data["shieldedNullifierTransactions"])
                {
                    ShieldedNullifierTransaction tx;

                    if (item.contains("transactionId") && !item["transactionId"].is_null())
                    {
                        try {
                            if (item["transactionId"].is_number()) {
                                tx.transaction_id = item["transactionId"].get<uint64_t>();
                            } else {
                                tx.transaction_id = std::stoull(item["transactionId"].get<std::string>());
                            }
                        } catch (...) {}
                    }

                    tx.block_hash = item.value("blockHash", "");

                    if (item.contains("blockHeight") && !item["blockHeight"].is_null())
                    {
                        try {
                            if (item["blockHeight"].is_number()) {
                                tx.block_height = item["blockHeight"].get<uint64_t>();
                            } else {
                                tx.block_height = std::stoull(item["blockHeight"].get<std::string>());
                            }
                        } catch (...) {}
                    }

                    tx.nullifier = item.value("nullifier", "");
                    results.push_back(tx);
                }
            }

            midnight::g_logger->info("Shielded nullifiers query returned " +
                                    std::to_string(results.size()) + " results");
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to query shielded nullifiers: " + std::string(e.what()));
        }

        return results;
    }

} // namespace midnight::network
