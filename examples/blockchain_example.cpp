#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/network/network_config.hpp"
#include <iostream>

int main()
{
    midnight::g_logger->info("Midnight Blockchain Example Started");

    // Select Midnight network
    // Options: DEVNET (local), TESTNET (preprod), STAGENET, MAINNET
    midnight::network::NetworkConfig::Network network = midnight::network::NetworkConfig::Network::TESTNET;
    std::string network_name = midnight::network::NetworkConfig::network_name(network);
    std::string rpc_endpoint = midnight::network::NetworkConfig::get_rpc_endpoint(network);
    std::string faucet_url = midnight::network::NetworkConfig::get_faucet_url(network);

    midnight::g_logger->info("Using Midnight " + network_name + " network");

    // Initialize blockchain manager
    midnight::blockchain::ProtocolParams params;
    params.min_fee_a = 44;
    params.min_fee_b = 155381;
    params.utxo_cost_per_byte = 4310;
    params.max_tx_size = 16384;
    params.max_block_size = 65536;

    midnight::blockchain::MidnightBlockchain blockchain;
    blockchain.initialize(network_name, params);

    // Connect to Midnight node
    std::cout << "\n=== Connecting to " << network_name << " network ===" << std::endl;
    std::cout << "RPC Endpoint: " << rpc_endpoint << std::endl;
    if (!faucet_url.empty())
    {
        std::cout << "Faucet: " << faucet_url << std::endl;
        std::cout << "Get testnet coins from the faucet to test transactions" << std::endl;
    }
    std::cout << std::endl;

    if (blockchain.connect(rpc_endpoint))
    {
        midnight::g_logger->info("Connected to Midnight node");

        // Create wallet from mnemonic
        midnight::blockchain::Wallet wallet;
        std::string mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
        wallet.create_from_mnemonic(mnemonic, "");

        std::string address = wallet.get_address();
        std::cout << "Wallet address: " << address << std::endl;

        // Query UTXOs from blockchain via RPC
        std::cout << "\n=== Querying UTXOs from blockchain ===" << std::endl;
        auto utxos = blockchain.query_utxos(address);
        std::cout << "Found " << utxos.size() << " UTXOs" << std::endl;

        for (size_t i = 0; i < utxos.size(); ++i)
        {
            std::cout << "  UTXO " << i << ": " << utxos[i].tx_hash.substr(0, 16)
                      << "... amount=" << utxos[i].amount << std::endl;
        }

        // Build transaction if UTXOs available
        if (!utxos.empty())
        {
            std::cout << "\n=== Building transaction ===" << std::endl;

            // Use first UTXO as input
            std::vector<midnight::blockchain::UTXO> inputs = {utxos[0]};

            // Create output to another address
            std::vector<std::pair<std::string, uint64_t>> outputs = {
                {"addr_test_qz2f04xp48tpam5sx70f6k5gx5g88p5z4n9ch9s7k", 1000000}};

            auto build_result = blockchain.build_transaction(inputs, outputs, address);
            if (build_result.success)
            {
                std::cout << "Transaction built successfully: " << build_result.result << std::endl;

                // Sign transaction
                std::cout << "\n=== Signing transaction ===" << std::endl;
                std::string private_key = "0123456789abcdef0123456789abcdef"; // Mock for demo
                auto sign_result = blockchain.sign_transaction(build_result.result, private_key);

                if (sign_result.success)
                {
                    std::cout << "Transaction signed successfully" << std::endl;

                    // Submit transaction to network
                    std::cout << "\n=== Submitting transaction ===" << std::endl;
                    auto submit_result = blockchain.submit_transaction(sign_result.result);

                    if (submit_result.success)
                    {
                        std::cout << "Transaction submitted! TX ID: " << submit_result.result << std::endl;
                    }
                    else
                    {
                        std::cout << "Failed to submit transaction: " << submit_result.error_message << std::endl;
                    }
                }
                else
                {
                    std::cout << "Failed to sign transaction: " << sign_result.error_message << std::endl;
                }
            }
            else
            {
                std::cout << "Failed to build transaction: " << build_result.error_message << std::endl;
            }
        }
        else
        {
            std::cout << "No UTXOs available for transaction building" << std::endl;
        }

        blockchain.disconnect();
        midnight::g_logger->info("Disconnected from Midnight node");
    }
    else
    {
        std::cerr << "Failed to connect to Midnight node at " << rpc_endpoint << std::endl;
        std::cerr << "Make sure a Midnight node is running at that address" << std::endl;
        std::cerr << "\nNetwork Information:" << std::endl;
        std::cerr << "- Testnet endpoint: https://preprod.midnight.network/api" << std::endl;
        std::cerr << "- Make sure OpenSSL is installed for HTTPS support" << std::endl;
    }

    midnight::g_logger->info("Midnight Blockchain Example Finished");
    return 0;
}
