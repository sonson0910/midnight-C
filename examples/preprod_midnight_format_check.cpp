#include "midnight/network/indexer_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/core/common_utils.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr const char *kIndexerUrl =
    "https://indexer.preprod.midnight.network/api/v4/graphql";
constexpr const char *kRpcUrl = "https://rpc.preprod.midnight.network";
constexpr const char *kAddress =
    "mn_addr_preprod1vm7tp364dgq8u9shfms29wll0w4qs4c37aqhtwd8pjqq78zuekvs5vf04q";
constexpr const char *kTxHash =
    "e8b6bab117b98cd7d7dbe6ab86584d81ec3d9960218f001cd3696f957dea977e";
constexpr const char *kBlockHash =
    "939906f65687c6250fce1eb21a59737f23b382e4a8335004d2b9dd9de33ecc2c";
constexpr uint64_t kBlockHeight = 748878;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string strip0x(const std::string &value) {
  return midnight::util::strip_hex_prefix(value);
}

} // namespace

int main() {
  try {
    midnight::network::IndexerClient indexer(kIndexerUrl, 30000);
    indexer.set_cache_enabled(false);

    auto tx = indexer.query_transaction(std::string("0x") + kTxHash);
    require(tx.is_object() && !tx.empty(), "transaction query returned empty");
    require(tx.value("id", 0) == 151162, "unexpected transaction id");
    require(strip0x(tx.value("hash", "")) == kTxHash, "unexpected transaction hash");
    require(tx.contains("block") && tx["block"].value("height", 0) == kBlockHeight,
            "unexpected transaction block height");

    auto block = indexer.query_block(kBlockHeight);
    require(block.height == kBlockHeight, "unexpected block height");
    require(strip0x(block.hash) == kBlockHash, "unexpected block hash");

    bool tx_in_block = false;
    for (const auto &item : block.transactions) {
      if (strip0x(item.value("hash", "")) == kTxHash) {
        tx_in_block = true;
        break;
      }
    }
    require(tx_in_block, "target transaction not found in block transactions");

    auto utxos = indexer.query_utxos(kAddress, static_cast<uint32_t>(kBlockHeight),
                                     static_cast<uint32_t>(kBlockHeight));
    bool found_target_utxo = false;
    for (const auto &utxo : utxos) {
      if (utxo.address == kAddress && utxo.output_index == 0 &&
          utxo.value == 1000000000ULL && strip0x(utxo.tx_hash) == kTxHash) {
        found_target_utxo = true;
        break;
      }
    }
    require(found_target_utxo, "expected faucet UTXO was not found");

    midnight::network::MidnightNodeRPC rpc(kRpcUrl, 15000);
    bool rejected_wallet_address = false;
    try {
      (void)rpc.get_contract_state(kAddress);
    } catch (const std::exception &) {
      rejected_wallet_address = true;
    }
    require(rejected_wallet_address,
            "midnight_contractState accepted a wallet address unexpectedly");

    std::cout << "Preprod Midnight format check passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Preprod Midnight format check failed: " << e.what() << "\n";
    return 1;
  }
}
