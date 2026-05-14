#include "midnight/network/indexer_client.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/logger.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>


namespace midnight::network {
namespace {
constexpr const char *kNightTokenType =
    "0000000000000000000000000000000000000000000000000000000000000000";

std::string to_lower_copy(const std::string &s) {
  std::string r;
  r.reserve(s.size());
  for (unsigned char c : s)
    r += static_cast<char>(std::tolower(c));
  return r;
}

std::string strip_hex_prefix(std::string hex) {
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
    hex = hex.substr(2);
  return hex;
}

bool is_night_token_type(const std::string &token_type) {
  return token_type.empty() ||
         to_lower_copy(strip_hex_prefix(token_type)) == kNightTokenType;
}

bool env_flag_enabled(const char *name) {
  const char *value = std::getenv(name);
  if (value == nullptr || *value == '\0')
    return false;
  const std::string normalized = to_lower_copy(value);
  return normalized == "1" || normalized == "true" || normalized == "yes" ||
         normalized == "on";
}

uint64_t parse_u64_string(const json &value) {
  try {
    if (value.is_number_unsigned())
      return value.get<uint64_t>();
    if (value.is_number_integer()) {
      auto signed_value = value.get<int64_t>();
      return signed_value < 0 ? 0 : static_cast<uint64_t>(signed_value);
    }
    if (value.is_string()) {
      const auto str = value.get<std::string>();
      if (!str.empty()) {
        uint64_t parsed = 0;
        const auto *begin = str.data();
        const auto *end = str.data() + str.size();
        auto result = std::from_chars(begin, end, parsed);
        if (result.ec == std::errc() && result.ptr == end)
          return parsed;
        throw std::out_of_range("u128 value does not fit in uint64_t: " + str);
      }
    }
  } catch (const std::out_of_range &) {
    throw;
  } catch (...) {
  }
  return 0;
}

void append_utxos_from_transaction_json(std::vector<blockchain::UTXO> &utxos,
                                        const std::string &address,
                                        const json &tx,
                                        uint32_t fallback_block_height = 0) {
  if (!tx.is_object() || !tx.contains("unshieldedCreatedOutputs") ||
      tx["unshieldedCreatedOutputs"].is_null())
    return;

  uint32_t block_height = fallback_block_height;
  if (tx.contains("block") && tx["block"].is_object() &&
      !tx["block"].is_null()) {
    block_height = static_cast<uint32_t>(tx["block"].value("height", 0ULL));
  }

  const std::string normalized_address = to_lower_copy(address);
  const std::string tx_hash = tx.value("hash", "");

  for (const auto &out : tx["unshieldedCreatedOutputs"]) {
    if (to_lower_copy(out.value("owner", "")) != normalized_address)
      continue;
    if (out.contains("spentAtTransaction") &&
        !out["spentAtTransaction"].is_null())
      continue;

    blockchain::UTXO utxo;
    utxo.tx_hash = tx_hash;
    utxo.output_index = out.value("outputIndex", 0u);
    utxo.value = parse_u64_string(out.value("value", json("0")));
    utxo.address = out.value("owner", address);
    utxo.token_type = out.value("tokenType", "");
    utxo.amount_commitment = out.value("intentHash", "");
    utxo.block_height = block_height;
    utxo.ctime = parse_u64_string(out.value("ctime", json(0)));
    utxo.initial_nonce = out.value("initialNonce", "");
    utxo.registered_for_dust_generation =
        out.value("registeredForDustGeneration", false);
    utxos.push_back(std::move(utxo));
  }
}

WalletState wallet_state_from_utxos(const std::string &address,
                                    const std::vector<blockchain::UTXO> &utxos) {
  WalletState state;
  state.address = address;

  uint64_t total_night = 0;
  uint64_t total_dust = 0;
  uint64_t dust_registered_utxos = 0;

  for (const auto &utxo : utxos) {
    if (is_night_token_type(utxo.token_type) || utxo.token_type == "NIGHT") {
      total_night += utxo.value;
    } else if (utxo.token_type == "DUST") {
      total_dust += utxo.value;
    }
    if (utxo.registered_for_dust_generation) {
      dust_registered_utxos++;
    }
  }

  state.unshielded_balance = std::to_string(total_night);
  state.dust_balance = std::to_string(total_dust);
  state.available_utxo_count = static_cast<uint32_t>(utxos.size());
  state.dust_registered_utxo_count = dust_registered_utxos;
  state.synced = true;
  return state;
}
} // namespace

// ────────────────────────────────────────────────
// URL Parsing Helper
// ────────────────────────────────────────────────

void IndexerClient::parse_url(const std::string &url, std::string &scheme,
                              std::string &host, uint16_t &port,
                              std::string &path) {
  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    scheme = "http";
    scheme_end = 0;
  } else {
    scheme = url.substr(0, scheme_end);
    scheme_end += 3;
  }

  size_t path_start = url.find('/', scheme_end);
  std::string host_port;
  if (path_start == std::string::npos) {
    host_port = url.substr(scheme_end);
    path = "/";
  } else {
    host_port = url.substr(scheme_end, path_start - scheme_end);
    path = url.substr(path_start);
  }

  size_t colon_pos = host_port.find(':');
  if (colon_pos == std::string::npos) {
    host = host_port;
    port = (scheme == "https") ? 443 : 80;
  } else {
    host = host_port.substr(0, colon_pos);
    port = static_cast<uint16_t>(std::stoi(host_port.substr(colon_pos + 1)));
  }
}

// ────────────────────────────────────────────────
// Constructor
// ────────────────────────────────────────────────

IndexerClient::IndexerClient(const std::string &graphql_url,
                             uint32_t timeout_ms)
    : graphql_url_(graphql_url), timeout_ms_(timeout_ms) {
  std::string scheme, host, path;
  uint16_t port;
  parse_url(graphql_url, scheme, host, port, path);

  std::string base_url = scheme + "://" + host + ":" + std::to_string(port);
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
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

bool IndexerClient::is_cache_valid(const CacheEntry &entry) const {
  if (!cache_enabled_)
    return false;
  auto age = std::chrono::steady_clock::now() - entry.timestamp;
  return age < std::chrono::seconds(cache_ttl_seconds_);
}

std::string IndexerClient::get_cache_key(const std::string &prefix,
                                         const std::string &value) const {
  return prefix + ":" + value;
}

void IndexerClient::prune_expired_cache() {
  auto now = std::chrono::steady_clock::now();
  auto cutoff = now - std::chrono::seconds(cache_ttl_seconds_);

  std::unique_lock<std::shared_mutex> lock(cache_mutex_);

  for (auto it = utxo_cache_.begin(); it != utxo_cache_.end();) {
    if (it->second.timestamp < cutoff) {
      it = utxo_cache_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = wallet_cache_.begin(); it != wallet_cache_.end();) {
    if (it->second.timestamp < cutoff) {
      it = wallet_cache_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = block_cache_.begin(); it != block_cache_.end();) {
    if (it->second.timestamp < cutoff) {
      it = block_cache_.erase(it);
    } else {
      ++it;
    }
  }
}

void IndexerClient::clear_cache() {
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

json IndexerClient::execute_graphql(const json &body) {
  std::string scheme, host, path;
  uint16_t port;
  parse_url(graphql_url_, scheme, host, port, path);

  try {
    while (path.size() > 1 && path.back() == '/')
      path.pop_back();
    size_t last_slash = path.find_last_of('/');
    std::string remaining_path =
        (last_slash != std::string::npos && last_slash < path.length() - 1)
            ? path.substr(last_slash)
            : "/graphql";

    auto response = client_->post_json(remaining_path, body);
    if (response.contains("errors") && response["errors"].is_array() &&
        !response["errors"].empty()) {
      std::string err_msg = "GraphQL error: ";
      for (const auto &err : response["errors"]) {
        if (err.contains("message")) {
          err_msg += err["message"].get<std::string>() + "; ";
        }
      }
      midnight::g_logger->warn(err_msg);
    }
    return response;
  } catch (const std::exception &e) {
    midnight::g_logger->error("IndexerClient GraphQL request failed: " +
                              std::string(e.what()));
    throw;
  }
}

json IndexerClient::graphql_query(const std::string &query,
                                  const json &variables) {
  json body = json::object();
  body["query"] = query;
  if (!variables.empty()) {
    body["variables"] = variables;
  }

  auto response = execute_graphql(body);

  if (response.contains("data")) {
    midnight::g_logger->debug(
        "graphql_query: data_type=" +
        std::string(response["data"].is_null()     ? "null"
                    : response["data"].is_object() ? "object"
                    : response["data"].is_array()  ? "array"
                                                   : "other"));
    return response["data"];
  }

  midnight::g_logger->warn("graphql_query: no 'data' key in response");
  return json::object();
}

// ────────────────────────────────────────────────
// Wallet Queries (Optimized with caching)
// ────────────────────────────────────────────────

WalletState IndexerClient::query_wallet_state(const std::string &address,
                                              bool use_cache) {
  WalletState state;
  state.address = address;

  if (use_cache && cache_enabled_) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    auto it = wallet_cache_.find(address);
    if (it != wallet_cache_.end() && is_cache_valid(it->second)) {
      midnight::g_logger->debug("Wallet state cache hit for " +
                                address.substr(0, 20) + "...");
      if (it->second.data.is_object()) {
        state.address = it->second.data.value("address", address);
        state.unshielded_balance =
            it->second.data.value("unshielded_balance", "0");
        state.shielded_balance = it->second.data.value("shielded_balance", "0");
        state.dust_balance = it->second.data.value("dust_balance", "0");
        state.available_utxo_count =
            it->second.data.value("available_utxo_count", 0u);
        state.dust_registered_utxo_count =
            it->second.data.value("dust_registered_utxo_count", 0u);
        state.session_id = it->second.data.value("session_id", "");
        state.synced = it->second.data.value("synced", false);
        state.error = it->second.data.value("error", "");
      }
      return state;
    }
  }

  try {
    auto utxos = query_utxos(address, use_cache);
    state = wallet_state_from_utxos(address, utxos);

    midnight::g_logger->info("Wallet state for " + address.substr(0, 20) +
                             "...: " + state.unshielded_balance +
                             " NIGHT, " + state.dust_balance +
                             " DUST, " + std::to_string(utxos.size()) +
                             " UTXOs");

    if (state.dust_registered_utxo_count > 0) {
      midnight::g_logger->info("Dust generation UTXOs registered: " +
                               std::to_string(state.dust_registered_utxo_count));
    }

    if (use_cache && cache_enabled_) {
      std::unique_lock<std::shared_mutex> lock(cache_mutex_);
      json cache_data = json::object();
      cache_data["address"] = state.address;
      cache_data["unshielded_balance"] = state.unshielded_balance;
      cache_data["dust_balance"] = state.dust_balance;
      cache_data["available_utxo_count"] = state.available_utxo_count;
      cache_data["dust_registered_utxo_count"] =
          state.dust_registered_utxo_count;
      cache_data["synced"] = state.synced;
      cache_data["error"] = state.error;
      wallet_cache_[address] = {cache_data, std::chrono::steady_clock::now()};
    }
  } catch (const std::exception &e) {
    state.error = e.what();
    state.synced = false;
    midnight::g_logger->error("Failed to query wallet state: " + state.error);
  }

  return state;
}

WalletState
IndexerClient::query_wallet_state_from_transaction(const std::string &address,
                                                   const std::string &tx_hash) {
  return wallet_state_from_utxos(address,
                                 query_utxos_from_transaction(address, tx_hash));
}

WalletState IndexerClient::query_wallet_state(const std::string &address,
                                              uint32_t from_block,
                                              uint32_t to_block) {
  return wallet_state_from_utxos(address,
                                 query_utxos(address, from_block, to_block));
}

// ────────────────────────────────────────────────
// UTXO Query (Optimized with caching & batch)
// ────────────────────────────────────────────────

std::vector<blockchain::UTXO>
IndexerClient::query_utxos(const std::string &address, bool use_cache) {
  constexpr uint32_t FIRST_INDEXED_BLOCK = 609178u;

  if (use_cache && cache_enabled_) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    auto it = utxo_cache_.find(address);
    if (it != utxo_cache_.end() && is_cache_valid(it->second)) {
      midnight::g_logger->debug("UTXO cache hit for " + address.substr(0, 20) +
                                "...");
      std::vector<blockchain::UTXO> result;
      if (it->second.data.is_array()) {
        for (const auto &item : it->second.data) {
          blockchain::UTXO utxo;
          utxo.tx_hash = item.value("tx_hash", "");
          utxo.output_index = item.value("output_index", 0u);
          utxo.amount_commitment = item.value("amount_commitment", "");
          utxo.value = item.value("value", 0ULL);
          utxo.address = item.value("address", "");
          utxo.token_type = item.value("token_type", "NIGHT");
          utxo.block_height = item.value("block_height", 0u);
          utxo.ctime = item.value("ctime", 0ULL);
          utxo.initial_nonce = item.value("initial_nonce", "");
          utxo.registered_for_dust_generation =
              item.value("registered_for_dust_generation", false);
          result.push_back(utxo);
        }
      }
      return result;
    }
  }

  if (!env_flag_enabled("MIDNIGHT_ENABLE_FULL_UTXO_SCAN")) {
    throw std::runtime_error(
        "IndexerClient::query_utxos(address) would require a historical block "
        "scan. Use query_utxos(address, from_block, to_block), "
        "query_utxos_from_transaction(address, tx_hash), a persisted wallet "
        "sync cache, or set MIDNIGHT_ENABLE_FULL_UTXO_SCAN=1 for explicit "
        "debug-only scanning.");
  }

  uint32_t tip_height = FIRST_INDEXED_BLOCK;
  try {
    constexpr const char *tip_query = R"(
                query GetTip {
                    block { height }
                }
            )";
    json tip_result = graphql_query(tip_query, json::object());
    if (tip_result.contains("block") && !tip_result["block"].is_null()) {
      tip_height = tip_result["block"].value("height", FIRST_INDEXED_BLOCK);
    }
  } catch (...) {
  }

  auto utxos =
      scan_utxos_internal(address, FIRST_INDEXED_BLOCK, tip_height, use_cache);

  if (use_cache && cache_enabled_) {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    json cache_data = json::array();
    for (const auto &utxo : utxos) {
      json item = json::object();
      item["tx_hash"] = utxo.tx_hash;
      item["output_index"] = utxo.output_index;
      item["amount_commitment"] = utxo.amount_commitment;
      item["value"] = utxo.value;
      item["address"] = utxo.address;
      item["token_type"] = utxo.token_type;
      item["block_height"] = utxo.block_height;
      item["ctime"] = utxo.ctime;
      item["initial_nonce"] = utxo.initial_nonce;
      item["registered_for_dust_generation"] =
          utxo.registered_for_dust_generation;
      cache_data.push_back(item);
    }
    utxo_cache_[address] = {cache_data, std::chrono::steady_clock::now()};
  }

  return utxos;
}

std::vector<blockchain::UTXO>
IndexerClient::query_utxos_from_transaction(const std::string &address,
                                            const std::string &tx_hash) {
  std::vector<blockchain::UTXO> utxos;
  const auto tx = query_transaction(tx_hash);
  if (!tx.is_object() || tx.empty()) {
    throw std::runtime_error("transaction not found or indexer query failed: " +
                             tx_hash);
  }
  append_utxos_from_transaction_json(utxos, address, tx);
  return utxos;
}

std::vector<blockchain::UTXO>
IndexerClient::scan_utxos_internal(const std::string &address,
                                   uint32_t from_block, uint32_t to_block,
                                   bool /* use_cache */) {
  std::vector<blockchain::UTXO> utxos;

  try {
    midnight::g_logger->info(
        "Scanning UTXOs blocks " + std::to_string(from_block) + "-" +
        std::to_string(to_block) + " for " + address.substr(0, 20) + "...");

    // Correct query: use block(offset: { height }) per block
    // Based on midnight-research indexer API v4
    constexpr const char *block_query = R"(
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
                                intentHash
                                ctime
                                initialNonce
                                registeredForDustGeneration
                                spentAtTransaction {
                                    id
                                    hash
                                }
                            }
                        }
                    }
                }
            )";

    const std::string normalized_address = to_lower_copy(address);
    uint32_t blocks_scanned = 0;
    uint32_t created_for_address = 0;
    uint32_t spent_for_address = 0;

    for (uint32_t h = from_block; h <= to_block; ++h) {
      json variables = json::object();
      variables["height"] = static_cast<int>(h);
      json result = graphql_query(block_query, variables);

      if (!result.contains("block") || result["block"].is_null()) {
        continue;
      }

      const auto &block_data = result["block"];
      blocks_scanned++;

      if (!block_data.contains("transactions") ||
          block_data["transactions"].is_null())
        continue;

      const auto &txns = block_data["transactions"];
      std::vector<json> tx_list =
          txns.is_array() ? std::vector<json>(txns.begin(), txns.end())
                          : std::vector<json>{txns};

      for (const auto &tx : tx_list) {
        if (!tx.contains("unshieldedCreatedOutputs") ||
            tx["unshieldedCreatedOutputs"].is_null())
          continue;

        for (const auto &out : tx["unshieldedCreatedOutputs"]) {
          if (to_lower_copy(out.value("owner", "")) != normalized_address)
            continue;

          created_for_address++;
          if (out.contains("spentAtTransaction") &&
              !out["spentAtTransaction"].is_null()) {
            spent_for_address++;
            continue;
          }
        }
        append_utxos_from_transaction_json(utxos, address, tx, h);
      }

      if (blocks_scanned % 500 == 0) {
        midnight::g_logger->info(
            "  UTXO scan progress: " + std::to_string(blocks_scanned) +
            " blocks scanned");
      }
    }

    midnight::g_logger->info(
        "Block scan complete: " + std::to_string(blocks_scanned) + " blocks, " +
        std::to_string(created_for_address) + " created for address, " +
        std::to_string(spent_for_address) + " already spent");

    uint64_t total_unspent = 0;
    for (const auto &u : utxos)
      total_unspent += u.value;

    midnight::g_logger->info(
        "UTXO scan result: " + std::to_string(utxos.size()) +
        " unspent UTXOs, " + std::to_string(total_unspent / 1000000) +
        ".000000 NIGHT");
  } catch (const std::exception &e) {
    midnight::g_logger->error("UTXO scan failed: " + std::string(e.what()));
  }

  return utxos;
}

std::vector<blockchain::UTXO>
IndexerClient::query_utxos(const std::string &address, uint32_t from_block,
                           uint32_t to_block) {
  return scan_utxos_internal(address, from_block, to_block, false);
}

// ────────────────────────────────────────────────
// Contract State Queries
// ────────────────────────────────────────────────

ContractState
IndexerClient::query_contract_state(const std::string &contract_address) {
  ContractState state;
  state.contract_address = contract_address;

  try {
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
    variables["address"] = strip_hex_prefix(contract_address);
    auto data = graphql_query(query, variables);

    if (data.contains("contractAction") && !data["contractAction"].is_null()) {
      state.exists = true;
      auto &ca = data["contractAction"];

      if (ca.contains("state")) {
        state.ledger_state = ca["state"];
      }

      if (ca.contains("unshieldedBalances") &&
          !ca["unshieldedBalances"].is_null()) {
        state.unshielded_balances = ca["unshieldedBalances"];
      }

      if (ca.contains("transaction") && !ca["transaction"].is_null()) {
        auto &tx = ca["transaction"];
        if (tx.contains("block") && !tx["block"].is_null()) {
          state.block_height = tx["block"].value("height", 0);
          state.block_hash = tx["block"].value("hash", "");
        }
      }
    }
  } catch (const std::exception &e) {
    state.error = e.what();
    midnight::g_logger->error("Failed to query contract state: " + state.error);
  }

  return state;
}

ContractAction
IndexerClient::query_contract_action(const std::string &contract_address) {
  ContractAction action;
  action.address = contract_address;

  try {
    const std::string query = R"(
          query ContractAction($address: HexEncoded!) {
            contractAction(address: $address) {
              __typename
              address
              state
              zswapState
              ... on ContractCall {
                entryPoint
                deploy {
                  address
                }
              }
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
    variables["address"] = strip_hex_prefix(contract_address);
    auto data = graphql_query(query, variables);

    if (data.contains("contractAction") && !data["contractAction"].is_null()) {
      auto &ca = data["contractAction"];

      if (ca.contains("__typename")) {
        std::string typename_str = ca["__typename"].get<std::string>();
        if (typename_str == "ContractDeploy")
          action.action_type = "deploy";
        else if (typename_str == "ContractCall")
          action.action_type = "call";
        else if (typename_str == "ContractUpdate")
          action.action_type = "update";
      }

      if (ca.contains("state")) {
        action.state = ca["state"];
      }

      if (ca.contains("entryPoint") && !ca["entryPoint"].is_null()) {
        action.entry_point = ca["entryPoint"].get<std::string>();
      }

      if (ca.contains("unshieldedBalances") &&
          !ca["unshieldedBalances"].is_null()) {
        action.unshielded_balances = ca["unshieldedBalances"];
      }

      if (ca.contains("transaction") && !ca["transaction"].is_null()) {
        auto &tx = ca["transaction"];
        if (tx.contains("block") && !tx["block"].is_null()) {
          action.block_height = tx["block"].value("height", 0);
          action.block_hash = tx["block"].value("hash", "");
        }
      }

      midnight::g_logger->info("Contract action for " +
                               contract_address.substr(0, 20) +
                               "...: type=" + action.action_type);
    }
  } catch (const std::exception &e) {
    action.error = e.what();
    midnight::g_logger->error("Failed to query contract action: " +
                              action.error);
  }

  return action;
}

std::vector<ContractBalance>
IndexerClient::query_contract_balance(const std::string &contract_address) {
  std::vector<ContractBalance> balances;

  try {
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
    variables["address"] = strip_hex_prefix(contract_address);
    auto data = graphql_query(query, variables);

    if (data.contains("contractAction") && !data["contractAction"].is_null()) {
      auto &ca = data["contractAction"];
      if (ca.contains("unshieldedBalances") &&
          !ca["unshieldedBalances"].is_null()) {
        const auto &bal_array = ca["unshieldedBalances"];
        if (bal_array.is_array()) {
          for (const auto &bal : bal_array) {
            ContractBalance cb;
            cb.token_type = bal.value("tokenType", "");
            cb.amount = bal.value("amount", "0");
            balances.push_back(cb);
          }
        }
      }
    }

    midnight::g_logger->debug("Contract " + contract_address.substr(0, 20) +
                              "... has " + std::to_string(balances.size()) +
                              " balance entries");
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to query contract balance: " +
                              std::string(e.what()));
  }

  return balances;
}

json IndexerClient::query_contract_fields(
    const std::string &contract_address,
    const std::vector<std::string> &fields) {
  std::ostringstream field_list;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i > 0)
      field_list << " ";
    field_list << fields[i];
  }

  std::string query = R"(
            query ContractFields($address: HexEncoded!) {
                contractAction(address: $address) {
                    )" +
                      field_list.str() + R"(
                }
            }
        )";

  json variables = json::object();
  variables["address"] = strip_hex_prefix(contract_address);
  auto data = graphql_query(query, variables);

  if (data.contains("contractAction") && !data["contractAction"].is_null()) {
    return data["contractAction"];
  }

  return json::object();
}

// ────────────────────────────────────────────────
// Transaction Queries
// ────────────────────────────────────────────────

json IndexerClient::query_transaction(const std::string &tx_hash) {
  if (tx_hash.empty()) {
    return json::object();
  }

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
              ... on ContractCall {
                entryPoint
                deploy { address }
              }
              unshieldedBalances {
                tokenType
                amount
              }
            }
            unshieldedCreatedOutputs {
              owner
              value
              tokenType
              outputIndex
              intentHash
              spentAtTransaction {
                id
                hash
              }
            }
            unshieldedSpentOutputs {
              owner
              value
              tokenType
              outputIndex
            }
          }
        }
      )";

  json variables = json::object();
  variables["hash"] = midnight::util::ensure_hex_prefix(tx_hash);

  try {
    auto data = graphql_query(query, variables);
    if (data.contains("transactions") && data["transactions"].is_array()) {
      const auto &txs = data["transactions"];
      if (!txs.empty() && !txs[0].is_null()) {
        return txs[0];
      }
    }
  } catch (const std::exception &e) {
    midnight::g_logger->warn("Transaction query failed: " +
                             std::string(e.what()));
  }

  return json::object();
}

// ────────────────────────────────────────────────
// DUST Registration
// ────────────────────────────────────────────────

DustRegistrationStatus
IndexerClient::query_dust_status(const std::string &address) {
  try {
    std::string lower_addr;
    lower_addr.reserve(address.size());
    for (char c : address) {
      lower_addr +=
          static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    bool is_stake_address = (lower_addr.rfind("stake", 0) == 0);

    if (is_stake_address) {
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
          !data["dustGenerationStatus"].empty()) {
        for (const auto &dust : data["dustGenerationStatus"]) {
          std::string cardano_addr;
          if (dust.contains("cardanoRewardAddress")) {
            try {
              cardano_addr = dust["cardanoRewardAddress"].get<std::string>();
            } catch (...) {
            }
          }
          if (cardano_addr == address) {
            bool registered = dust.value("registered", false);
            return registered ? DustRegistrationStatus::REGISTERED
                              : DustRegistrationStatus::NOT_REGISTERED;
          }
        }
        return DustRegistrationStatus::NOT_REGISTERED;
      }
      return DustRegistrationStatus::NOT_REGISTERED;
    } else {
      midnight::g_logger->debug(
          "DUST status query: address is not a Cardano stake address.");
      return DustRegistrationStatus::UNKNOWN;
    }
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to query DUST status: " +
                              std::string(e.what()));
    return DustRegistrationStatus::UNKNOWN;
  }
}

DustGenerationStatus IndexerClient::query_dust_generation_status(
    const std::string &cardano_reward_address) {
  DustGenerationStatus status;
  status.cardano_reward_address = cardano_reward_address;

  try {
    std::string lower_addr;
    lower_addr.reserve(cardano_reward_address.size());
    for (char c : cardano_reward_address) {
      lower_addr +=
          static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower_addr.rfind("stake", 0) != 0) {
      midnight::g_logger->warn("query_dust_generation_status: address is not a "
                               "Cardano stake address: " +
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
        !data["dustGenerationStatus"].empty()) {
      const auto &dust = data["dustGenerationStatus"][0];
      status.cardano_reward_address =
          dust.value("cardanoRewardAddress", cardano_reward_address);
      status.dust_address = dust.value("dustAddress", "");
      status.registered = dust.value("registered", false);
      status.night_balance = dust.value("nightBalance", "0");
      status.generation_rate = dust.value("generationRate", "0");
      status.max_capacity = dust.value("maxCapacity", "0");
      status.current_capacity = dust.value("currentCapacity", "0");
      status.utxo_tx_hash = dust.value("utxoTxHash", "");
      status.utxo_output_index = dust.value("utxoOutputIndex", 0u);

      midnight::g_logger->info(
          "DUST generation status for " + cardano_reward_address.substr(0, 20) +
          "...: registered=" +
          std::string(status.registered ? "true" : "false") +
          ", dust_addr=" + status.dust_address);
    }
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to query DUST generation status: " +
                              std::string(e.what()));
  }

  return status;
}

std::vector<DustGenerationStatus> IndexerClient::query_dust_generation_statuses(
    const std::vector<std::string> &cardano_reward_addresses) {
  std::vector<DustGenerationStatus> results;

  if (cardano_reward_addresses.empty()) {
    return results;
  }

  try {
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
    for (const auto &addr : cardano_reward_addresses) {
      variables["cardanoRewardAddresses"].push_back(addr);
    }

    auto data = graphql_query(query, variables);

    if (data.contains("dustGenerationStatus") &&
        data["dustGenerationStatus"].is_array()) {
      for (const auto &dust : data["dustGenerationStatus"]) {
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
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to query DUST generation statuses: " +
                              std::string(e.what()));
  }

  return results;
}

std::vector<DustRegistration> IndexerClient::query_dust_registrations(
    const std::string &cardano_reward_address) {
  std::vector<DustRegistration> registrations;

  try {
    std::string lower_addr;
    lower_addr.reserve(cardano_reward_address.size());
    for (char c : cardano_reward_address) {
      lower_addr +=
          static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower_addr.rfind("stake", 0) != 0) {
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
                maxCapacity
                currentCapacity
                utxoTxHash
                utxoOutputIndex
                        }
                    }
                }
            )";

    json variables = json::object();
    variables["cardanoRewardAddresses"] = json::array({cardano_reward_address});

    auto data = graphql_query(query, variables);

    if (data.contains("dustGenerations") &&
        data["dustGenerations"].is_array() &&
        !data["dustGenerations"].empty()) {
      const auto &generations = data["dustGenerations"][0];
      if (generations.contains("registrations") &&
          generations["registrations"].is_array()) {
        for (const auto &reg : generations["registrations"]) {
          DustRegistration registration;
          registration.dust_address = reg.value("dustAddress", "");
          registration.valid = reg.value("valid", false);
          registration.night_balance = reg.value("nightBalance", "0");
          registration.generation_rate = reg.value("generationRate", "0");
          registration.max_capacity = reg.value("maxCapacity", "0");
          registration.current_capacity = reg.value("currentCapacity", "0");
          registration.utxo_tx_hash = reg.value("utxoTxHash", "");
          registration.utxo_output_index = reg.value("utxoOutputIndex", 0u);
          registrations.push_back(registration);
        }
      }

      midnight::g_logger->info(
          "DUST registrations for " + cardano_reward_address.substr(0, 20) +
          "...: " + std::to_string(registrations.size()) + " registrations");
    }
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to query DUST registrations: " +
                              std::string(e.what()));
  }

  return registrations;
}

// ────────────────────────────────────────────────
// Block Height & Sync
// ────────────────────────────────────────────────

uint64_t IndexerClient::get_block_height() {
  try {
    const std::string query = R"({
                block {
                    hash
                    height
                    timestamp
                }
            })";

    auto data = graphql_query(query);

    if (data.contains("block") && !data["block"].is_null()) {
      const auto &block = data["block"];
      std::string block_hash = block.value("hash", "");

      if (!block_hash.empty()) {
        midnight::g_logger->info("Indexer synced, latest block: " +
                                 block_hash.substr(0, 20) + "...");

        if (block.contains("height") && !block["height"].is_null()) {
          try {
            if (block["height"].is_number()) {
              last_tip_height_ = block["height"].get<uint64_t>();
              return last_tip_height_;
            } else if (block["height"].is_string()) {
              last_tip_height_ =
                  std::stoull(block["height"].get<std::string>());
              return last_tip_height_;
            }
          } catch (...) {
            midnight::g_logger->warn("Failed to parse block height");
          }
        }
      }
    }
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to get block height: " +
                              std::string(e.what()));
  }

  return 0;
}

BlockInfo IndexerClient::query_block(uint64_t height, const std::string &hash) {
  BlockInfo info;

  try {
    const std::string block_fields = R"(
                {
                    hash
                    height
                    timestamp
                    transactions {
                        id
                        hash
                        unshieldedCreatedOutputs {
                            owner
                            value
                            tokenType
                            outputIndex
                            intentHash
                            spentAtTransaction {
                                id
                                hash
                            }
                        }
                        unshieldedSpentOutputs {
                            owner
                            value
                            tokenType
                            outputIndex
                        }
                    }
                }
            )";

    std::string query;
    json variables = json::object();
    if (height > 0) {
      query = "query GetBlock($height: Int!) { block(offset: { height: $height "
              "}) " +
              block_fields + " }";
      variables["height"] = static_cast<int64_t>(height);
    } else if (!hash.empty()) {
      query = "query GetBlock($hash: HexEncoded!) { block(offset: { hash: "
              "$hash }) " +
              block_fields + " }";
      variables["hash"] = midnight::util::ensure_hex_prefix(hash);
    } else {
      query = "query GetBlock { block " + block_fields + " }";
    }

    auto data = graphql_query(query, variables);

    if (data.contains("block") && !data["block"].is_null()) {
      const auto &block = data["block"];
      info.hash = block.value("hash", "");
      if (block.contains("transactions") && block["transactions"].is_array()) {
        info.transactions = block["transactions"];
      }

      if (block.contains("height") && !block["height"].is_null()) {
        try {
          if (block["height"].is_number()) {
            info.height = block["height"].get<uint64_t>();
          } else {
            info.height = std::stoull(block["height"].get<std::string>());
          }
        } catch (...) {
        }
      }

      if (block.contains("timestamp") && !block["timestamp"].is_null()) {
        try {
          if (block["timestamp"].is_number()) {
            info.timestamp = block["timestamp"].get<uint64_t>();
          } else {
            info.timestamp = std::stoull(block["timestamp"].get<std::string>());
          }
        } catch (...) {
        }
      }

      midnight::g_logger->info(
          "Block query: height=" + std::to_string(info.height) +
          ", hash=" + info.hash.substr(0, 16) + "...");
    } else {
      midnight::g_logger->warn(
          "Block not found: height=" + std::to_string(height) +
          ", hash=" + (hash.empty() ? "(none)" : hash.substr(0, 16) + "..."));
    }
  } catch (const std::exception &e) {
    info.error = e.what();
    midnight::g_logger->error("Failed to query block: " + info.error);
  }

  return info;
}

bool IndexerClient::is_synced() {
  try {
    const std::string query = R"({
                block {
                    height
                }
            })";

    auto data = graphql_query(query);

    if (data.contains("block") && !data["block"].is_null()) {
      const auto &block = data["block"];
      if (block.contains("height") && !block["height"].is_null()) {
        try {
          if (block["height"].is_number()) {
            return true;
          }
          if (block["height"].is_string()) {
            (void)std::stoull(block["height"].get<std::string>());
            return true;
          }
        } catch (...) {
        }
      }
    }
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to check sync status: " +
                              std::string(e.what()));
  }

  return false;
}

// ────────────────────────────────────────────────
// Shielded Wallet Support
// ────────────────────────────────────────────────

ShieldedSession
IndexerClient::connect_shielded_wallet(const std::string &viewing_key,
                                       uint64_t start_index) {
  ShieldedSession session;
  session.start_index = start_index;

  if (viewing_key.empty()) {
    midnight::g_logger->error(
        "Cannot connect shielded wallet: viewing key is empty");
    return session;
  }

  try {
    const std::string mutation = R"(
          mutation Connect($viewingKey: ViewingKey!, $options: ConnectOptions) {
                    connect(viewingKey: $viewingKey, options: $options)
                }
            )";

    json variables = json::object();
    variables["viewingKey"] = viewing_key;

    json options = json::object();
    options["startIndex"] = static_cast<int64_t>(start_index);
    variables["options"] = options;

    auto data = graphql_query(mutation, variables);

    if (data.contains("connect") && !data["connect"].is_null()) {
      session.session_id = data["connect"].get<std::string>();
      session.is_connected = !session.session_id.empty();

      midnight::g_logger->info(
          "Shielded wallet connected: " + session.session_id.substr(0, 20) +
          "... (start_index=" + std::to_string(start_index) + ")");
    } else {
      midnight::g_logger->error(
          "Shielded wallet connect failed: no session_id returned");
    }
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to connect shielded wallet: " +
                              std::string(e.what()));
  }

  return session;
}

bool IndexerClient::disconnect_shielded_wallet(const std::string &session_id) {
  if (session_id.empty()) {
    midnight::g_logger->error("Cannot disconnect: session_id is empty");
    return false;
  }

  try {
    const std::string mutation = R"(
          mutation Disconnect($sessionId: HexEncoded!) {
                    disconnect(sessionId: $sessionId)
                }
            )";

    json variables = json::object();
    variables["sessionId"] = midnight::util::ensure_hex_prefix(session_id);

    auto data = graphql_query(mutation, variables);

    if (data.contains("disconnect")) {
      midnight::g_logger->info("Shielded wallet disconnected: " +
                               session_id.substr(0, 20) + "...");
      return true;
    }

    midnight::g_logger->error("Disconnect failed: no response");
    return false;
  } catch (const std::exception &e) {
    midnight::g_logger->error("Failed to disconnect shielded wallet: " +
                              std::string(e.what()));
    return false;
  }
}

std::string
IndexerClient::query_shielded_balance(const std::string &session_id) {
  if (session_id.empty()) {
    midnight::g_logger->error(
        "Cannot query shielded balance: session_id is empty");
    return "0";
  }

  midnight::g_logger->warn(
      "shieldedBalance query is not available in indexer v4. "
      "Use shieldedTransactions subscription and track balances client-side.");
  return "0";
}

// ────────────────────────────────────────────────
// Nullifier Queries (v4.1.0)
// ────────────────────────────────────────────────

std::vector<DustNullifierTransaction> IndexerClient::query_dust_nullifiers(
    const std::vector<std::string> &nullifier_prefixes, uint64_t from_block,
    uint64_t to_block) {
  std::vector<DustNullifierTransaction> results;

  if (nullifier_prefixes.empty()) {
    midnight::g_logger->warn(
        "query_dust_nullifiers called with empty prefixes");
    return results;
  }

  midnight::g_logger->warn(
      "dustNullifierTransactions is a subscription-only field. "
      "Use GraphQLSubscriptionClient over WebSocket instead of HTTP.");
  (void)from_block;
  (void)to_block;

  return results;
}

std::vector<ShieldedNullifierTransaction>
IndexerClient::query_shielded_nullifiers(
    const std::vector<std::string> &nullifier_prefixes, uint64_t from_block,
    uint64_t to_block) {
  std::vector<ShieldedNullifierTransaction> results;

  if (nullifier_prefixes.empty()) {
    midnight::g_logger->warn(
        "query_shielded_nullifiers called with empty prefixes");
    return results;
  }

  midnight::g_logger->warn(
      "shieldedNullifierTransactions is a subscription-only field. "
      "Use GraphQLSubscriptionClient over WebSocket instead of HTTP.");
  (void)from_block;
  (void)to_block;

  return results;
}

} // namespace midnight::network
