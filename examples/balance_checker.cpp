// Quick balance checker using IndexerClient
#include "midnight/network/indexer_client.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <address> [funding_tx_hash|block_height]\n";
        return 1;
    }

    std::string address = argv[1];
    const char* network_env = std::getenv("MIDNIGHT_NETWORK");
    std::string network = network_env && *network_env ? network_env : "preview";
    
    const char* indexer_env = std::getenv("MIDNIGHT_INDEXER_URL");
    std::string graphql_url = indexer_env && *indexer_env
        ? indexer_env
        : "https://indexer." + network + ".midnight.network/api/v4/graphql";

    std::cout << "Querying balance for: " << address << "\n";
    std::cout << "Network: " << network << "\n\n";

    try {
        midnight::network::IndexerClient indexer(graphql_url, 30000);
        indexer.set_cache_enabled(false);

        midnight::network::WalletState state;
        if (argc >= 3) {
            std::string source = argv[2];
            if (source.rfind("0x", 0) == 0 || source.size() == 64) {
                state = indexer.query_wallet_state_from_transaction(address, source);
            } else {
                auto block = static_cast<uint32_t>(std::stoul(source));
                state = indexer.query_wallet_state(address, block, block);
            }
        } else {
            state = indexer.query_wallet_state(address);
        }
        
        std::cout << "=== WALLET STATE ===\n";
        std::cout << "Address: " << state.address << "\n";
        std::cout << "Unshielded Balance (NIGHT): " << state.unshielded_balance << "\n";
        std::cout << "Shielded Balance: " << state.shielded_balance << "\n";
        std::cout << "Dust Balance: " << state.dust_balance << "\n";
        std::cout << "Available UTXOs: " << state.available_utxo_count << "\n";
        std::cout << "Synced: " << (state.synced ? "yes" : "no") << "\n";
        
        if (!state.error.empty()) {
            std::cout << "Error: " << state.error << "\n";
        }
        
        // Also query UTXOs
        std::cout << "\n=== UTXOs ===\n";
        std::vector<midnight::blockchain::UTXO> utxos;
        if (argc >= 3) {
            std::string source = argv[2];
            if (source.rfind("0x", 0) == 0 || source.size() == 64) {
                utxos = indexer.query_utxos_from_transaction(address, source);
            } else {
                auto block = static_cast<uint32_t>(std::stoul(source));
                utxos = indexer.query_utxos(address, block, block);
            }
        } else {
            utxos = indexer.query_utxos(address, false);
        }
        std::cout << "Total UTXOs: " << utxos.size() << "\n";
        for (const auto& utxo : utxos) {
            std::cout << "  - TX: " << utxo.tx_hash
                      << " #" << utxo.output_index
                      << " Value: " << utxo.value
                      << " Token: " << utxo.token_type
                      << " Block: " << utxo.block_height << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
