#include "midnight/network/indexer_client.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <regex>
#include <set>
#include <map>
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
        // Parse URL to extract components
        std::string scheme, host, path;
        uint16_t port;
        parse_url(graphql_url, scheme, host, port, path);

        // Build base URL with path prefix if present (e.g., /api/v4)
        // NetworkClient needs full path to make correct requests
        std::string base_url = scheme + "://" + host + ":" + std::to_string(port);
        if (!path.empty()) {
            // Extract path prefix (everything except the last segment)
            // e.g., /api/v4/graphql -> /api/v4
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
    // Core GraphQL Execution
    // ────────────────────────────────────────────────

    json IndexerClient::execute_graphql(const json &body)
    {
        std::string scheme, host, path;
        uint16_t port;
        parse_url(graphql_url_, scheme, host, port, path);

        try
        {
            // Client was constructed with base_url containing path prefix (e.g., /api/v4)
            // So we only need to pass the remaining part (e.g., "/graphql")
            // Extract only the filename from path to avoid duplication
            size_t last_slash = path.find_last_of('/');
            std::string remaining_path = (last_slash != std::string::npos && last_slash < path.length() - 1)
                ? path.substr(last_slash)  // e.g., "/graphql"
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
        json body = {{"query", query}};
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
    // Wallet Queries
    // ────────────────────────────────────────────────

    WalletState IndexerClient::query_wallet_state(const std::string &address)
    {
        WalletState state;
        state.address = address;

        try
        {
            // ═══════════════════════════════════════════════════════════════════════
            // Indexer v4 HTTP GraphQL Architecture
            //
            // IMPORTANT: The Indexer's HTTP GraphQL endpoint does NOT support
            // efficient UTXO queries by address. There is NO endpoint like:
            //   query { unshieldedUTXOs(address: "mn_addr_preview...") { ... } }
            //
            // The HTTP API is designed for:
            //   - Block data (block { hash timestamp })
            //   - Transaction data (transactions { ... })
            //   - Governance data (dustGenerationStatus, etc.)
            //
            // For REAL-TIME wallet UTXO tracking, use WebSocket subscriptions:
            //   subscription { unshieldedTransactions(address: "Bech32m") { ... } }
            //
            // Our HTTP workaround: Query transactions with pagination,
            // filter by owner address, and track spent outputs to calculate unspent UTXOs.
            // ═══════════════════════════════════════════════════════════════════════

            // 1. Get UTXOs for this address (filtering from transactions)
            auto utxos = query_utxos(address);

            // 2. Calculate balance by summing UTXO values
            uint64_t total_night = 0;
            uint64_t total_dust = 0;

            for (const auto &utxo : utxos)
            {
                if (utxo.token_type == "NIGHT" || utxo.token_type.empty())
                {
                    total_night += utxo.amount;
                }
                else if (utxo.token_type == "DUST")
                {
                    total_dust += utxo.amount;
                }
                else
                {
                    // Default to NIGHT if token_type not specified
                    total_night += utxo.amount;
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

            // 3. Query dust generation status using address
            // Note: dustGenerationStatus requires Cardano stake address, not Midnight address
            auto dust_status = query_dust_status(address);
            if (dust_status == DustRegistrationStatus::REGISTERED)
            {
                midnight::g_logger->info("Dust registered: yes");
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

    std::vector<blockchain::UTXO> IndexerClient::query_utxos(const std::string &address)
    {
        std::vector<blockchain::UTXO> utxos;

        try
        {
            // ═══════════════════════════════════════════════════════════════════════════════
            // Indexer v4 API: Query UTXOs via HTTP block-height pagination
            //
            // Strategy: The Indexer only stores indexed data starting from block 609178.
            // All earlier blocks return empty. We:
            //   1. Query `block { height }` to get the current chain tip height.
            //   2. Iterate block heights from FIRST_INDEXED_BLOCK (609178) to tip.
            //   3. For each block, query `block(offset: { height: N }).transactions`
            //      which returns all transactions in that block.
            //   4. Track spent outputs to subtract from created outputs.
            //
            // Key discovery (May 2026):
            //   - `transactions(offset: { hash: ... })` alone returns from genesis
            //     but the Indexer has no data before block 609178 → always empty.
            //   - `block(offset: { height: N })` IS supported and correctly returns
            //     all transactions in block N (not just the first one).
            //   - This is the ONLY way to scan all indexed blocks.
            //
            // Response fields (UnshieldedTransaction):
            //   - unshieldedCreatedOutputs: { owner, value, tokenType, outputIndex }
            //   - unshieldedSpentOutputs:   { owner, value, tokenType }
            // ═══════════════════════════════════════════════════════════════════════════════

            midnight::g_logger->info("Scanning UTXOs via block-height pagination for " +
                                    address.substr(0, 20) + "...");

            // First block with indexed transaction data on preview network.
            constexpr uint32_t FIRST_INDEXED_BLOCK = 609178u;

            // ── Step 1: Get current chain tip height ──────────────────────────────────
            constexpr const char* tip_query = R"(
                query GetTip {
                    block { height }
                }
            )";
            json tip_result = graphql_query(tip_query, json::object());
            uint32_t tip_height = FIRST_INDEXED_BLOCK; // fallback if query fails

            if (tip_result.contains("block") && !tip_result["block"].is_null())
            {
                tip_height = tip_result["block"].value("height", FIRST_INDEXED_BLOCK);
                midnight::g_logger->info("Chain tip height: " + std::to_string(tip_height));
            }
            else
            {
                midnight::g_logger->warn("Could not determine chain tip; scanning from block " +
                                        std::to_string(FIRST_INDEXED_BLOCK));
            }

            // ── Step 2: Prepare per-block transaction query ────────────────────────────
            // Note: outputIndex is a 0-based index within the transaction's created outputs.
            constexpr const char* blk_tx_query = R"(
                query GetBlockTxs($h: Int!) {
                    block(offset: { height: $h }) {
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

            std::set<std::string> spent_tx_hashes; // tx hashes that spent from our address
            std::set<std::string> created_tx_hashes; // tx hashes that created outputs for our address
            std::map<std::pair<std::string, uint32_t>, uint64_t> created_amounts; // (tx_hash, output_index) -> amount
            uint32_t blocks_scanned = 0;
            uint32_t blocks_with_txs = 0;
            uint64_t total_created_raw = 0;

            // ── Step 3: Scan blocks ───────────────────────────────────────────────────
            // Use a reasonable limit to avoid hammering the API.
            // The gap from FIRST_INDEXED_BLOCK to tip is typically small on preview (< 10k blocks).
            constexpr uint32_t MAX_SCAN_BLOCKS = 100000u;

            for (uint32_t height = FIRST_INDEXED_BLOCK;
                 height <= tip_height && (height - FIRST_INDEXED_BLOCK) < MAX_SCAN_BLOCKS;
                 ++height)
            {
                json blk_result = graphql_query(blk_tx_query, json({{"h", static_cast<int>(height)}}));

                if (!blk_result.contains("block") || blk_result["block"].is_null())
                {
                    // No more indexed blocks — stop early.
                    midnight::g_logger->debug("No indexed block at height " + std::to_string(height) +
                                            ", stopping scan.");
                    break;
                }

                blocks_scanned++;
                const auto& blk = blk_result["block"];

                // Log progress every 50 blocks.
                if (blocks_scanned % 50 == 0)
                {
                    midnight::g_logger->info("  UTXO scan progress: block " +
                                            std::to_string(height) + "/" +
                                            std::to_string(tip_height) + ", " +
                                            std::to_string(blocks_with_txs) + " blocks with txs.");
                }

                if (!blk.contains("transactions") || blk["transactions"].is_null())
                    continue;

                const auto& txns = blk["transactions"];
                std::vector<json> tx_list = txns.is_array()
                                                ? std::vector<json>(txns.begin(), txns.end())
                                                : std::vector<json>{txns};

                if (!tx_list.empty())
                    blocks_with_txs++;

                for (const auto& tx : tx_list)
                {
                    std::string tx_hash = tx.value("hash", "");

                    // ── Check unshieldedSpentOutputs ──────────────────────────────────
                    if (tx.contains("unshieldedSpentOutputs") &&
                        !tx["unshieldedSpentOutputs"].is_null())
                    {
                        for (const auto& sout : tx["unshieldedSpentOutputs"])
                        {
                            if (sout.value("owner", "") == address)
                            {
                                spent_tx_hashes.insert(tx_hash);
                            }
                        }
                    }

                    // ── Check unshieldedCreatedOutputs ─────────────────────────────────
                    if (tx.contains("unshieldedCreatedOutputs") &&
                        !tx["unshieldedCreatedOutputs"].is_null())
                    {
                        for (const auto& out : tx["unshieldedCreatedOutputs"])
                        {
                            if (out.value("owner", "") == address)
                            {
                                created_tx_hashes.insert(tx_hash);
                                uint32_t out_idx = out.value("outputIndex", 0u);
                                uint64_t amount = std::stoull(out.value("value", "0"));
                                created_amounts[{tx_hash, out_idx}] = amount;
                                total_created_raw += amount;
                            }
                        }
                    }
                }
            }

            midnight::g_logger->info("Block scan complete: " + std::to_string(blocks_scanned) +
                                    " blocks scanned, " + std::to_string(blocks_with_txs) +
                                    " with txs. " + std::to_string(created_tx_hashes.size()) +
                                    " txs created outputs for address, " +
                                    std::to_string(spent_tx_hashes.size()) + " txs spent from address.");

            // ── Step 4: Build unspent UTXO list ───────────────────────────────────────
            // A UTXO is unspent if its creating tx is NOT in spent_tx_hashes.
            for (const auto& entry : created_amounts)
            {
                const std::string& created_tx_hash = entry.first.first;
                uint32_t created_out_idx = entry.first.second;
                uint64_t created_amount = entry.second;

                if (spent_tx_hashes.find(created_tx_hash) != spent_tx_hashes.end())
                    continue; // this output was already spent

                blockchain::UTXO utxo;
                utxo.tx_hash = created_tx_hash;
                utxo.output_index = created_out_idx;
                utxo.amount = created_amount;
                utxo.address = address;
                utxos.push_back(utxo);
            }

            // ── Step 5: Log summary ───────────────────────────────────────────────────
            uint64_t total_unspent = 0;
            for (const auto& u : utxos)
                total_unspent += u.amount;

            midnight::g_logger->info("UTXO scan result for " + address.substr(0, 20) +
                                    "...: " + std::to_string(utxos.size()) + " unspent UTXOs, " +
                                    std::to_string(total_unspent) + " raw total (" +
                                    std::to_string(total_unspent / 1000000) + ".000000 NIGHT).");
        }
        catch (const std::exception& e)
        {
            midnight::g_logger->error("UTXO query failed: " + std::string(e.what()));
        }

        return utxos;
    }

    std::vector<blockchain::UTXO> IndexerClient::query_utxos(const std::string &address,
                                                            uint32_t from_block,
                                                            uint32_t to_block)
    {
        std::vector<blockchain::UTXO> utxos;
        try {
            constexpr const char* blk_tx_query = R"(
                query GetBlockTxs($h: Int!) {
                    block(offset: { height: $h }) {
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
            std::set<std::string> created_tx_hashes;
            std::map<std::pair<std::string, uint32_t>, uint64_t> created_amounts;
            uint32_t blocks_with_txs = 0;

            for (uint32_t height = from_block; height <= to_block; ++height) {
                json blk_result = graphql_query(blk_tx_query, json{{"h", static_cast<int>(height)}});
                if (!blk_result.contains("block") || blk_result["block"].is_null())
                    continue;

                const auto& blk = blk_result["block"];
                if (!blk.contains("transactions") || blk["transactions"].is_null())
                    continue;

                const auto& txns = blk["transactions"];
                std::vector<json> tx_list = txns.is_array()
                                                ? std::vector<json>(txns.begin(), txns.end())
                                                : std::vector<json>{txns};
                if (!tx_list.empty()) blocks_with_txs++;

                for (const auto& tx : tx_list) {
                    std::string tx_hash = tx.value("hash", "");

                    if (tx.contains("unshieldedSpentOutputs") && !tx["unshieldedSpentOutputs"].is_null()) {
                        for (const auto& sout : tx["unshieldedSpentOutputs"]) {
                            if (sout.value("owner", "") == address)
                                spent_tx_hashes.insert(tx_hash);
                        }
                    }

                    if (tx.contains("unshieldedCreatedOutputs") && !tx["unshieldedCreatedOutputs"].is_null()) {
                        for (const auto& out : tx["unshieldedCreatedOutputs"]) {
                            if (out.value("owner", "") == address) {
                                created_tx_hashes.insert(tx_hash);
                                uint32_t out_idx = out.value("outputIndex", 0u);
                                uint64_t amount = std::stoull(out.value("value", "0"));
                                created_amounts[{tx_hash, out_idx}] = amount;
                            }
                        }
                    }
                }
            }

            midnight::g_logger->debug("Block-range query [" + std::to_string(from_block) +
                                    "-" + std::to_string(to_block) + "]: " +
                                    std::to_string(blocks_with_txs) + " blocks with txs, " +
                                    std::to_string(created_tx_hashes.size()) + " created, " +
                                    std::to_string(spent_tx_hashes.size()) + " spent");

            for (const auto& entry : created_amounts) {
                const std::string& tx_h = entry.first.first;
                uint32_t out_idx = entry.first.second;
                uint64_t amount = entry.second;
                if (spent_tx_hashes.find(tx_h) != spent_tx_hashes.end())
                    continue;
                blockchain::UTXO utxo;
                utxo.tx_hash = tx_h;
                utxo.output_index = out_idx;
                utxo.amount = amount;
                utxo.address = address;
                utxos.push_back(utxo);
            }
        } catch (const std::exception& e) {
            midnight::g_logger->error("Block-range UTXO query failed: " + std::string(e.what()));
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

        // Indexer v4 API: Uses "contractAction" not "contractState"
        std::string query = R"(
            query ContractFields($address: HexEncoded!) {
                contractAction(address: $address) {
                    )" + field_list.str() + R"(
                }
            }
        )";

        json variables = {{"address", contract_address}};
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

        // ═══════════════════════════════════════════════════════════════════════
        // Indexer v4.1.0 API: "transactions" is a PLURAL field that takes offset.
        // Returns an array. We take the first element [0].
        // The "transaction" (singular) approach may not exist in all versions.
        // ═══════════════════════════════════════════════════════════════════════

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

        json variables = {{"hash", "0x" + clean_hash}};

        try
        {
            auto data = graphql_query(query, variables);
            // v4.1.0: transactions() returns array, take [0]
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
            // ═══════════════════════════════════════════════════════════════════════
            // Indexer v4 API: dustGenerationStatus requires Cardano stake address
            //
            // The dustGenerationStatus query takes cardanoRewardAddresses parameter
            // which expects Cardano stake addresses (stake_test1... or stake1...).
            //
            // Address format notes:
            //   - Cardano stake addresses: stake_test1... (testnet) or stake1... (mainnet)
            //   - Midnight addresses:       mn_addr_... (Bech32m with hrp: mn_addr_preview/preprod/mainnet)
            //
            // Conversion: Midnight addresses encode the Cardano stake key using CIP-1852
            // derivation inside the address payload. For now, users should provide the
            // Cardano stake address directly.
            //
            // If a Midnight address is passed, we query with an empty array and return
            // UNKNOWN since we cannot determine per-address status without conversion.
            // ═══════════════════════════════════════════════════════════════════════

            // Check if address looks like a Cardano stake address
            std::string lower_addr;
            lower_addr.reserve(address.size());
            for (char c : address) {
                lower_addr += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            bool is_stake_address = (lower_addr.rfind("stake", 0) == 0);

            if (is_stake_address)
            {
                // Use address as Cardano stake address
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
                json variables = {{"cardanoRewardAddresses", json::array({address})}};

                auto data = graphql_query(query, variables);

                // dustGenerationStatus returns array
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
                            if (registered)
                                return DustRegistrationStatus::REGISTERED;
                            return DustRegistrationStatus::NOT_REGISTERED;
                        }
                    }
                    return DustRegistrationStatus::NOT_REGISTERED;
                }
                return DustRegistrationStatus::NOT_REGISTERED;
            }
            else
            {
                // Midnight address - cannot convert to Cardano stake address without CIP-1852 implementation.
                // Return UNKNOWN with a clear message rather than querying with empty array.
                midnight::g_logger->debug(
                    "DUST status query: address is not a Cardano stake address. "
                    "Provide a Cardano stake address (stake_test1... or stake1...) "
                    "for DUST registration queries.");
                return DustRegistrationStatus::UNKNOWN;
            }
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
            // Indexer v4: Use block query to get latest block height
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

                    // Parse block height
                    if (block.contains("height") && !block["height"].is_null())
                    {
                        try {
                            if (block["height"].is_number()) {
                                return block["height"].get<uint64_t>();
                            } else if (block["height"].is_string()) {
                                return std::stoull(block["height"].get<std::string>());
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

} // namespace midnight::network
