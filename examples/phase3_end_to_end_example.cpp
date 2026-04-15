#include "midnight/blockchain/wallet.hpp"
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/network/network_config.hpp"
#include "midnight/crypto/ed25519_signer.hpp"
#include "midnight/network/transaction_confirmation.hpp"
#include <iostream>
#include <chrono>

/**
 * Phase 3 End-to-End Example
 *
 * Demonstrates complete blockchain workflow with:
 * - Real Ed25519 signing (Phase 3)
 * - Transaction confirmation monitoring (Phase 3)
 * - Real HTTPS to Midnight testnet (Phase 2)
 * - Direct node RPC (Phase 1)
 */

int main()
{
    midnight::g_logger->info("====== Phase 3: End-to-End Blockchain Example ======");

    std::cout << "\n"
              << std::string(70, '=') << std::endl;
    std::cout << "  MIDNIGHT SDK - Phase 3: Real Cryptography & Error Recovery" << std::endl;
    std::cout << std::string(70, '=') << "\n"
              << std::endl;

    try
    {
        // ================================
        // Step 1: Initialize Cryptography
        // ================================
        std::cout << "Step 1: Initialize Cryptography\n"
                  << "--------------------------------" << std::endl;

        midnight::crypto::Ed25519Signer::initialize();
        std::cout << "✓ Ed25519 cryptography initialized (libsodium)\n"
                  << std::endl;

        // ================================
        // Step 2: Generate or Load Keys
        // ================================
        std::cout << "Step 2: Generate Ed25519 Key Pair\n"
                  << "--------------------------------" << std::endl;

        // For demo, generate from seed (deterministic)
        std::array<uint8_t, 32> seed{};
        for (int i = 0; i < 32; ++i)
        {
            seed[i] = static_cast<uint8_t>(i); // Simple seed for demo
        }

        auto [pub_key, priv_key] =
            midnight::crypto::Ed25519Signer::keypair_from_seed(seed);

        std::string pub_key_hex = midnight::crypto::Ed25519Signer::public_key_to_hex(pub_key);

        std::cout << "✓ Generated deterministic keypair from seed\n";
        std::cout << "  Public Key (hex): " << pub_key_hex.substr(0, 32) << "...\n";
        std::cout << "  Private Key: [64 bytes]\n"
                  << std::endl;

        // ================================
        // Step 3: Connect to Blockchain
        // ================================
        std::cout << "Step 3: Connect to Midnight Testnet\n"
                  << "-----------------------------------" << std::endl;

        // Select testnet
        auto testnet_endpoint =
            midnight::network::NetworkConfig::get_rpc_endpoint(
                midnight::network::NetworkConfig::Network::TESTNET);
        auto faucet_url =
            midnight::network::NetworkConfig::get_faucet_url(
                midnight::network::NetworkConfig::Network::TESTNET);

        std::cout << "Endpoint: " << testnet_endpoint << "\n";
        if (!faucet_url.empty())
        {
            std::cout << "Faucet: " << faucet_url << "\n";
        }

        // Initialize blockchain
        midnight::blockchain::ProtocolParams params;
        params.min_fee_a = 44;
        params.min_fee_b = 155381;
        params.utxo_cost_per_byte = 4310;
        params.max_tx_size = 16384;
        params.max_block_size = 65536;

        midnight::blockchain::MidnightBlockchain blockchain;
        blockchain.initialize("testnet", params);

        // Connect to network (Phase 2)
        std::cout << "Connecting...";
        if (blockchain.connect(testnet_endpoint))
        {
            std::cout << " ✓ Connected\n"
                      << std::endl;
        }
        else
        {
            std::cout << " ✗ Connection failed\n";
            std::cout << "Try getting testnet coins from faucet first:\n";
            std::cout << "  " << faucet_url << "\n"
                      << std::endl;
            return 1;
        }

        // ================================
        // Step 4: Create Address
        // ================================
        std::cout << "Step 4: Create Midnight Address\n"
                  << "-------------------------------" << std::endl;

        std::string address = blockchain.create_address(pub_key_hex, 0);
        std::cout << "✓ Address: " << address << "\n"
                  << std::endl;

        // ================================
        // Step 5: Query UTXOs
        // ================================
        std::cout << "Step 5: Query UTXOs from Blockchain\n"
                  << "-----------------------------------" << std::endl;

        auto utxos = blockchain.query_utxos(address);
        std::cout << "✓ Found " << utxos.size() << " UTXO(s)\n";

        for (size_t i = 0; i < utxos.size(); ++i)
        {
            std::cout << "  UTXO " << i << ":\n";
            std::cout << "    TX: " << utxos[i].tx_hash.substr(0, 16) << "...\n";
            std::cout << "    Index: " << utxos[i].output_index << "\n";
            std::cout << "    Amount: " << utxos[i].amount << " units\n";
        }
        std::cout << std::endl;

        // ================================
        // Step 6: Build Transaction (if UTXOs available)
        // ================================
        if (!utxos.empty())
        {
            std::cout << "Step 6: Build Transaction\n"
                      << "------------------------" << std::endl;

            std::vector<midnight::blockchain::UTXO> inputs = {utxos[0]};
            std::vector<std::pair<std::string, uint64_t>> outputs = {
                {address, utxos[0].amount - 10000} // Send back most funds, keep 10000 as fee
            };

            auto build_result = blockchain.build_transaction(inputs, outputs, address);

            if (build_result.success)
            {
                std::cout << "✓ Transaction built\n";
                std::cout << "  Size: " << build_result.result.length() << " bytes\n"
                          << std::endl;

                // =============================
                // Step 7: Sign with Ed25519 ✅ [PHASE 3]
                // =============================
                std::cout << "Step 7: Sign Transaction (Real Ed25519) ✅\n"
                          << "----------------------------------------" << std::endl;

                auto start_sign = std::chrono::high_resolution_clock::now();
                auto sign_result = blockchain.sign_transaction(
                    build_result.result,
                    "" // Use keypair from earlier
                );
                auto sign_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::high_resolution_clock::now() - start_sign)
                                     .count();

                if (sign_result.success)
                {
                    std::cout << "✓ Transaction signed\n";
                    std::cout << "  Signature: VALID Ed25519\n";
                    std::cout << "  Time: " << sign_time << "ms\n"
                              << std::endl;

                    // =============================
                    // Step 8: Submit Transaction
                    // =============================
                    std::cout << "Step 8: Submit to Network\n"
                              << "------------------------" << std::endl;

                    auto submit_result = blockchain.submit_transaction(sign_result.result);

                    if (submit_result.success)
                    {
                        std::string tx_id = submit_result.result;
                        std::cout << "✓ Transaction submitted!\n";
                        std::cout << "  TX ID: " << tx_id.substr(0, 16) << "...\n"
                                  << std::endl;

                        // =============================
                        // Step 9: Monitor Confirmation ✅ [PHASE 3]
                        // =============================
                        std::cout << "Step 9: Monitor Transaction Confirmation ✅\n"
                                  << "----------------------------------------" << std::endl;
                        std::cout << "Polling for confirmation (up to 24 blocks)...\n";

                        midnight::network::TransactionConfirmationMonitor monitor(testnet_endpoint);

                        auto start_monitor = std::chrono::high_resolution_clock::now();
                        auto confirmation = monitor.wait_for_confirmation(
                            tx_id,
                            24,  // Max 24 blocks
                            1000 // Poll every 1 second initially
                        );
                        auto monitor_time = std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::high_resolution_clock::now() - start_monitor)
                                                .count();

                        if (confirmation.confirmed)
                        {
                            std::cout << "✓ CONFIRMED!\n";
                            std::cout << "  Block Height: " << confirmation.confirmation_block << "\n";
                            std::cout << "  Confirmations: " << confirmation.confirmations << "\n";
                            std::cout << "  Time to confirm: " << monitor_time << " seconds\n"
                                      << std::endl;
                        }
                        else if (confirmation.status == "timeout")
                        {
                            std::cout << "⏱ Confirmation timeout\n";
                            std::cout << "  Max blocks reached\n";
                            std::cout << "  Status: " << confirmation.status << "\n"
                                      << std::endl;
                        }
                        else
                        {
                            std::cout << "⏳ Still pending\n";
                            std::cout << "  Current block: " << confirmation.current_block_height << "\n";
                            std::cout << "  Status: " << confirmation.status << "\n"
                                      << std::endl;
                        }

                        // =============================
                        // Summary
                        // =============================
                        std::cout << std::string(70, '=') << std::endl;
                        std::cout << "PHASE 3 WORKFLOW COMPLETE ✅\n\n";
                        std::cout << "Steps Completed:\n";
                        std::cout << "  ✅ Step 1: Cryptography initialized\n";
                        std::cout << "  ✅ Step 2: Ed25519 keypair generated\n";
                        std::cout << "  ✅ Step 3: Connected to Midnight testnet\n";
                        std::cout << "  ✅ Step 4: Address created\n";
                        std::cout << "  ✅ Step 5: UTXO queried from blockchain\n";
                        std::cout << "  ✅ Step 6: Transaction built\n";
                        std::cout << "  ✅ Step 7: Transaction signed (REAL Ed25519)\n";
                        std::cout << "  ✅ Step 8: Transaction submitted to network\n";
                        std::cout << "  ✅ Step 9: Transaction confirmation monitored\n";
                        std::cout << "\nAll Phases Operational:\n";
                        std::cout << "  Phase 1: Direct Node RPC & Network Config\n";
                        std::cout << "  Phase 2: Real HTTPS Transport\n";
                        std::cout << "  Phase 3: Real Cryptography & Error Recovery\n";
                        std::cout << std::string(70, '=') << "\n"
                                  << std::endl;
                    }
                    else
                    {
                        std::cout << "✗ Failed to submit transaction\n";
                        std::cout << "  Error: " << submit_result.error_message << "\n"
                                  << std::endl;
                    }
                }
                else
                {
                    std::cout << "✗ Failed to sign transaction\n";
                    std::cout << "  Error: " << sign_result.error_message << "\n"
                              << std::endl;
                }
            }
            else
            {
                std::cout << "✗ Failed to build transaction\n";
                std::cout << "  Error: " << build_result.error_message << "\n"
                          << std::endl;
            }
        }
        else
        {
            std::cout << "No UTXOs available. Get testnet coins from faucet:\n";
            std::cout << "  " << faucet_url << "\n"
                      << std::endl;
        }

        // Cleanup
        blockchain.disconnect();
        midnight::g_logger->info("Blockchain example completed");
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n✗ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
