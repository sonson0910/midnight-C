// Quick balance checker using IndexerClient
#include "midnight/network/indexer_client.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <address>\n";
        return 1;
    }

    std::string address = argv[1];
    std::string network = "preview";
    
    std::string graphql_url = 
        network == "preview" 
            ? "https://indexer.preview.midnight.network/api/v4/graphql"
            : "https://indexer.preprod.midnight.network/api/v4/graphql";

    std::cout << "Querying balance for: " << address << "\n";
    std::cout << "Network: " << network << "\n\n";

    try {
        midnight::network::IndexerClient indexer(graphql_url, 30000);
        indexer.set_cache_enabled(false);
        
        auto state = indexer.query_wallet_state(address);
        
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
        auto utxos = indexer.query_utxos(address, false);
        std::cout << "Total UTXOs: " << utxos.size() << "\n";
        for (const auto& utxo : utxos) {
            std::cout << "  - Value: " << utxo.value << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
