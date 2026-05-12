/**
 * fast_balance_checker.cpp
 * 
 * Optimized balance checker - scans only recent blocks (default 5000)
 * instead of full chain scan. Much faster for recently funded wallets.
 * 
 * Usage:
 *   Set MIDNIGHT_MNEMONIC env var, then run:
 *   ./fast_balance_checker.exe [--lookback 5000]
 *   
 *   Or provide address directly:
 *   ./fast_balance_checker.exe --address <unshielded_address>
 */

#include "midnight/network/indexer_client.hpp"
#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/core/logger.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <future>
#include <atomic>
#include <set>
#include <map>

using namespace midnight;
using namespace midnight::wallet;
using namespace midnight::network;
using namespace midnight::blockchain;

class FastBalanceChecker {
private:
    std::string graphql_url_;
    std::string address_;
    uint32_t concurrency_;
    std::atomic<uint32_t> blocks_scanned_{0};
    std::atomic<uint32_t> txs_found_{0};
    
public:
    FastBalanceChecker(const std::string& graphql_url, const std::string& address, uint32_t concurrency = 16)
        : graphql_url_(graphql_url), address_(address), concurrency_(concurrency) {}
    
    struct UTXO {
        std::string tx_hash;
        uint32_t output_index;
        uint64_t value;
    };
    
    // Get current block height from indexer
    uint32_t get_current_height() {
        constexpr const char* query = R"(
            query {
                block {
                    height
                }
            }
        )";
        
        IndexerClient indexer(graphql_url_);
        // graphql_query returns the "data" object directly
        auto result = indexer.graphql_query(query, nlohmann::json::object());
        
        // result IS the data object, so check block directly
        if (result.contains("block") && !result["block"].is_null()) {
            return result["block"]["height"].get<uint32_t>();
        }
        return 0;
    }
    
    // Scan recent blocks for UTXOs
    std::vector<UTXO> scan_recent_blocks(uint32_t lookback_blocks = 500) {
        std::vector<UTXO> utxos;
        uint32_t current_height = get_current_height();
        uint32_t start_height = (current_height > lookback_blocks) ? (current_height - lookback_blocks) : 1;
        
        std::cout << "Scanning blocks " << start_height << " to " << current_height 
                  << " (" << lookback_blocks << " blocks)..." << std::endl;
        
        // Sequential scan for stability (100 blocks per HTTP request)
        uint32_t batch_size = 100;
        uint32_t batch_count = 0;
        
        for (uint32_t batch_start = start_height; batch_start <= current_height; batch_start += batch_size) {
            uint32_t batch_end = std::min(batch_start + batch_size - 1, current_height);
            batch_count++;
            std::cout << "  Batch " << batch_count << ": blocks " << batch_start << "-" << batch_end << std::endl;
            
            auto batch_utxos = scan_block_range(batch_start, batch_end);
            utxos.insert(utxos.end(), batch_utxos.begin(), batch_utxos.end());
        }
        
        return utxos;
    }
    
    uint32_t get_blocks_scanned() const { return blocks_scanned_; }
    uint32_t get_txs_found() const { return txs_found_; }
    
private:
    std::vector<UTXO> scan_block_range(uint32_t from, uint32_t to) {
        std::vector<UTXO> local_utxos;
        IndexerClient indexer(graphql_url_);

        try {
            auto utxos = indexer.query_utxos(address_, from, to);
            blocks_scanned_ += (to >= from) ? (to - from + 1) : 0;
            txs_found_ += static_cast<uint32_t>(utxos.size());
            for (const auto& found : utxos) {
            UTXO u;
                u.tx_hash = found.tx_hash;
                u.output_index = found.output_index;
                u.value = found.value;
            local_utxos.push_back(u);
            }
        } catch (const std::exception&) {
            // Skip failed ranges.
        }
        
        return local_utxos;
    }
};

int main(int argc, char* argv[]) {
    std::string mnemonic;
    std::string address;
    std::string graphql_url = "https://indexer.preprod.midnight.network/api/v4/graphql";
    uint32_t lookback = 500;
    
    // Parse args
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--graphql" && i + 1 < argc) {
            graphql_url = argv[++i];
        } else if (arg == "--lookback" && i + 1 < argc) {
            lookback = std::stoul(argv[++i]);
        } else if (arg == "--mnemonic" && i + 1 < argc) {
            mnemonic = argv[++i];
        } else if (arg == "--address" && i + 1 < argc) {
            address = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: fast_balance_checker [options]\n"
                      << "  --mnemonic <phrase>    Mnemonic phrase (24 words)\n"
                      << "  --graphql <url>        GraphQL URL (default: preprod indexer)\n"
                      << "  --lookback <n>         Number of recent blocks to scan (default: 5000)\n"
                      << "  --address <addr>       Direct address to check\n";
            return 0;
        }
    }
    
    // Get mnemonic from env if not provided
    if (mnemonic.empty()) {
        const char* env_mnemonic = std::getenv("MIDNIGHT_MNEMONIC");
        if (env_mnemonic) {
            mnemonic = env_mnemonic;
        }
    }
    
    if (!mnemonic.empty()) {
        // Derive address from mnemonic using HDWallet
        std::cout << "Deriving address from mnemonic..." << std::endl;
        
        auto wallet = HDWallet::from_mnemonic(mnemonic);
        
        // Derive unshielded address key pair
        auto keypair = wallet.derive_night(0, 0);
        address = keypair.address;
        
        std::cout << "Unshielded address: " << address << std::endl;
    }
    
    if (address.empty()) {
        std::cerr << "ERROR: Provide --mnemonic or --address" << std::endl;
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Midnight Fast Balance Checker" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  GraphQL: " << graphql_url << std::endl;
    std::cout << "  Address: " << address << std::endl;
    std::cout << "  Lookback: " << lookback << " blocks" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    auto start = std::chrono::steady_clock::now();
    
    FastBalanceChecker checker(graphql_url, address);
    
    auto utxos = checker.scan_recent_blocks(lookback);
    
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  SCAN RESULTS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Time:     " << elapsed << " ms" << std::endl;
    std::cout << "  Blocks:   " << checker.get_blocks_scanned() << std::endl;
    std::cout << "  TXs:      " << checker.get_txs_found() << std::endl;
    std::cout << "  UTXOs:    " << utxos.size() << std::endl;
    
    uint64_t total = 0;
    for (const auto& utxo : utxos) {
        total += utxo.value;
    }
    
    double night = total / 1000000.0;
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  Balance:  " << night << " NIGHT" << std::endl;
    std::cout << std::endl;
    
    if (!utxos.empty()) {
        std::cout << "  UTXO Details:" << std::endl;
        for (size_t i = 0; i < std::min(utxos.size(), size_t(10)); ++i) {
            std::cout << "    " << (i + 1) << ". " << utxos[i].tx_hash.substr(0, 16) << "... "
                      << " idx=" << utxos[i].output_index << " "
                      << (utxos[i].value / 1000000.0) << " NIGHT" << std::endl;
        }
        if (utxos.size() > 10) {
            std::cout << "    ... and " << (utxos.size() - 10) << " more" << std::endl;
        }
    }
    
    std::cout << std::endl;
    
    return 0;
}
