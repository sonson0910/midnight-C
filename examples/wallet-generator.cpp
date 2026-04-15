/**
 * Midnight Wallet Generator - CLI Tool
 *
 * Generates sr25519 keypair and unshield address for Midnight Preprod
 * Usage: wallet-generator [--save-private-key] [--output-file wallet.json]
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <random>
#include <cstring>
#include <ctime>
#include <chrono>
#include <string>
#include <vector>
#include <memory>

#include "midnight/network/network_config.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/zk/proof_server_client.hpp"
#include "midnight/zk/proof_server_resilient_client.hpp"

namespace midnight::wallet
{

    // ============================================================================
    // Mock SR25519 Key Generation (for CLI tool)
    // ============================================================================

    /**
     * Generate mock sr25519 keypair
     * In production: Use libsodium or sr25519 library
     */
    class KeyPairGenerator
    {
    public:
        struct KeyPair
        {
            std::string private_key; // 66 hex chars (32 bytes)
            std::string public_key;  // 66 hex chars (32 bytes)
            std::string seed;        // 66 hex chars (32 bytes)
        };

        static KeyPair generate()
        {
            KeyPair pair;

            // Generate random seed (32 bytes)
            std::random_device rd;
            std::mt19937 gen(rd() + std::time(nullptr));
            std::uniform_int_distribution<> dis(0, 15);

            std::stringstream ss;
            ss << "0x";
            for (int i = 0; i < 64; ++i)
            {
                ss << std::hex << dis(gen);
            }

            pair.seed = ss.str();

            // Derive keys from seed (mock: just use parts of seed)
            pair.private_key = "0x" + pair.seed.substr(2, 64);
            pair.public_key = "0x" + pair.seed.substr(2, 64);

            return pair;
        }
    };

    /**
     * Generate sr25519 address (unshield format)
     * Midnight unshield addresses start with "5" (ss58 prefix for sr25519)
     */
    class AddressGenerator
    {
    public:
        /**
         * Generate unshield address from public key
         * Format: 5[A-Za-z0-9]{46} (ss58 encoded)
         */
        static std::string generate_unshield_address(const std::string &public_key)
        {
            // Mock: Generate valid-looking ss58 address
            std::string address = "5";

            // Use public key hex as seed for address generation
            std::mt19937 gen(std::hash<std::string>{}(public_key));
            const char charset[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
            constexpr size_t charset_size = sizeof(charset) - 1;
            std::uniform_int_distribution<size_t> dis(0, charset_size - 1);

            for (int i = 0; i < 46; ++i)
            {
                address += charset[dis(gen)];
            }

            return address;
        }

        /**
         * Generate shielded address (private, for receiving private transactions)
         * Format: Different from unshield, encrypted
         */
        static std::string generate_shielded_address(const std::string &private_key)
        {
            std::string address = "z";

            std::mt19937 gen(std::hash<std::string>{}(private_key));
            const char charset[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
            constexpr size_t charset_size = sizeof(charset) - 1;
            std::uniform_int_distribution<size_t> dis(0, charset_size - 1);

            for (int i = 0; i < 46; ++i)
            {
                address += charset[dis(gen)];
            }

            return address;
        }
    };

    // ============================================================================
    // Wallet Structure
    // ============================================================================

    struct Wallet
    {
        std::string name;
        std::string version = "1.0";
        std::string created;

        std::string private_key; // NEVER share
        std::string public_key;
        std::string seed; // Recovery seed

        std::string unshield_address; // Public address (for receiving)
        std::string shielded_address; // Private address (for private txs)

        std::string network = "midnight-preprod";
        uint32_t coin_type = 877; // Midnight coin type
    };

    // ============================================================================
    // Wallet Manager
    // ============================================================================

    class WalletManager
    {
    public:
        /**
         * Create new wallet
         */
        static Wallet create_wallet(const std::string &name = "default")
        {
            Wallet wallet;
            wallet.name = name;

            // Generate current timestamp
            auto now = std::time(nullptr);
            auto tm = std::localtime(&now);
            char timestamp[32];
            std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);
            wallet.created = timestamp;

            // Generate keypair
            auto keypair = KeyPairGenerator::generate();
            wallet.private_key = keypair.private_key;
            wallet.public_key = keypair.public_key;
            wallet.seed = keypair.seed;

            // Generate addresses
            wallet.unshield_address = AddressGenerator::generate_unshield_address(keypair.public_key);
            wallet.shielded_address = AddressGenerator::generate_shielded_address(keypair.private_key);

            return wallet;
        }

        /**
         * Export wallet to JSON
         */
        static std::string export_to_json(const Wallet &wallet, bool include_private_key = false)
        {
            std::stringstream ss;
            ss << "{\n";
            ss << "  \"wallet\": {\n";
            ss << "    \"name\": \"" << wallet.name << "\",\n";
            ss << "    \"version\": \"" << wallet.version << "\",\n";
            ss << "    \"created\": \"" << wallet.created << "\",\n";
            ss << "    \"network\": \"" << wallet.network << "\",\n";
            ss << "    \"coin_type\": " << wallet.coin_type << ",\n";

            if (include_private_key)
            {
                ss << "    \"private_key\": \"" << wallet.private_key << "\",\n";
                ss << "    \"seed\": \"" << wallet.seed << "\",\n";
            }

            ss << "    \"public_key\": \"" << wallet.public_key << "\",\n";
            ss << "    \"addresses\": {\n";
            ss << "      \"unshield\": \"" << wallet.unshield_address << "\",\n";
            ss << "      \"shielded\": \"" << wallet.shielded_address << "\"\n";
            ss << "    }\n";
            ss << "  }\n";
            ss << "}\n";

            return ss.str();
        }

        /**
         * Export private key to PEM file
         */
        static bool save_private_key(const Wallet &wallet, const std::string &filename)
        {
            std::ofstream file(filename);
            if (!file.is_open())
            {
                return false;
            }

            file << "-----BEGIN MIDNIGHT PRIVATE KEY-----\n";
            file << "Version: 1.0\n";
            file << "Network: " << wallet.network << "\n";
            file << "Created: " << wallet.created << "\n";
            file << "Address: " << wallet.unshield_address << "\n";
            file << "\n";

            // Write private key in chunks (PEM format)
            std::string key = wallet.private_key.substr(2); // Remove 0x prefix
            for (size_t i = 0; i < key.length(); i += 64)
            {
                file << key.substr(i, 64) << "\n";
            }

            file << "\n";
            file << "-----END MIDNIGHT PRIVATE KEY-----\n";

            file.close();
            return true;
        }
    };

} // namespace midnight::wallet

// ============================================================================
// CLI Interface
// ============================================================================

int main(int argc, char *argv[])
{
    using namespace midnight::wallet;

    std::string wallet_name = "default";
    std::string output_file = "";
    bool save_private_key = false;
    bool show_private_key = false;
    bool run_preprod_doctor = false;

    struct DoctorCheck
    {
        std::string name;
        std::string endpoint;
        bool passed = false;
        std::string detail;
    };

    auto print_doctor_result = [](const DoctorCheck &check)
    {
        std::cout << (check.passed ? "✅ PASS " : "❌ FAIL ") << check.name << "\n";
        std::cout << "   Endpoint: " << check.endpoint << "\n";
        std::cout << "   Detail: " << check.detail << "\n";
    };

    auto run_doctor = [&]() -> int
    {
        using midnight::network::NetworkConfig;
        using midnight::network::MidnightNodeRPC;
        using midnight::network::NetworkClient;
        using midnight::zk::ProofServerClient;
        using midnight::zk::ProofServerResilientClient;
        using midnight::zk::ResilienceConfig;

        const auto testnet = NetworkConfig::Network::TESTNET;
        const std::string node_endpoint = NetworkConfig::get_rpc_endpoint(testnet);
        const std::string indexer_endpoint = "https://indexer.preprod.midnight.network/api/v3/graphql";
        const std::string faucet_endpoint = NetworkConfig::get_faucet_url(testnet);
        const std::string proof_endpoint = "http://127.0.0.1:6300";

        std::cout << "=== Midnight Preprod Doctor ===\n";
        std::cout << "Checklist: node / indexer / proof server / faucet\n\n";

        std::vector<DoctorCheck> checks;
        checks.reserve(4);

        DoctorCheck node_check{"Node RPC", node_endpoint, false, "Not checked"};
        try
        {
            MidnightNodeRPC node(node_endpoint, 5000);
            auto node_info = node.get_node_info();
            node_check.passed = !node_info.is_null();
            node_check.detail = node_check.passed ? "JSON-RPC reachable" : "Empty response";
        }
        catch (const std::exception &e)
        {
            node_check.detail = e.what();
        }
        checks.push_back(node_check);

        DoctorCheck indexer_check{"Indexer GraphQL", indexer_endpoint, false, "Not checked"};
        try
        {
            NetworkClient indexer(indexer_endpoint, 5000);
            nlohmann::json payload = {{"query", "query { __typename }"}};
            auto response = indexer.post_json("/", payload);
            indexer_check.passed = response.contains("data") || response.contains("errors");
            indexer_check.detail = indexer_check.passed ? "GraphQL endpoint reachable" : "Unexpected response";
        }
        catch (const std::exception &e)
        {
            indexer_check.detail = e.what();
        }
        checks.push_back(indexer_check);

        DoctorCheck proof_check{"Proof server", proof_endpoint, false, "Not checked"};
        try
        {
            ProofServerClient::Config cfg;
            cfg.host = "127.0.0.1";
            cfg.port = 6300;
            cfg.timeout_ms = std::chrono::milliseconds(3000);
            auto proof_client = std::make_shared<ProofServerClient>(cfg);
            ResilienceConfig resilience;
            resilience.max_retries = 1;
            resilience.initial_backoff_ms = std::chrono::milliseconds(100);
            ProofServerResilientClient resilient(proof_client, resilience);
            proof_check.passed = resilient.connect_resilient();
            proof_check.detail = proof_check.passed ? "Proof server reachable" : proof_client->get_last_error();
        }
        catch (const std::exception &e)
        {
            proof_check.detail = e.what();
        }
        checks.push_back(proof_check);

        DoctorCheck faucet_check{"Faucet", faucet_endpoint, false, "Not checked"};
        try
        {
            NetworkClient faucet(faucet_endpoint, 5000);
            faucet_check.passed = faucet.is_connected();
            faucet_check.detail = faucet_check.passed
                                      ? "Endpoint parsed and reachable by client"
                                      : "Connectivity check failed";
        }
        catch (const std::exception &e)
        {
            faucet_check.detail = e.what();
        }
        checks.push_back(faucet_check);

        int passed_count = 0;
        for (const auto &check : checks)
        {
            print_doctor_result(check);
            std::cout << "\n";
            if (check.passed)
            {
                ++passed_count;
            }
        }

        const bool all_passed = passed_count == static_cast<int>(checks.size());
        std::cout << "Summary: " << passed_count << "/" << checks.size() << " checks passed\n";
        std::cout << "TX flow mode support: mock + real RPC\n";
        if (all_passed)
        {
            std::cout << "Result: ✅ Ready for end-to-end preprod validation.\n";
            return 0;
        }

        std::cout << "Result: ⚠️  Environment not fully ready for end-to-end preprod validation.\n";
        std::cout << "Hint: bring up proof server and retry `wallet-generator --preprod-doctor`.\n";
        return 2;
    };

    // Parse arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--save-private-key")
        {
            save_private_key = true;
        }
        else if (arg == "--show-private-key")
        {
            show_private_key = true;
        }
        else if (arg == "--output-file" && i + 1 < argc)
        {
            output_file = argv[++i];
        }
        else if (arg == "--name" && i + 1 < argc)
        {
            wallet_name = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Midnight Wallet Generator CLI\n\n";
            std::cout << "Usage: wallet-generator [OPTIONS]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --name NAME              Wallet name (default: 'default')\n";
            std::cout << "  --output-file FILE       Save wallet JSON to file\n";
            std::cout << "  --save-private-key       Save private key to PEM file\n";
            std::cout << "  --show-private-key       Display private key in output\n";
            std::cout << "  --preprod-doctor         Run preprod connectivity checklist\n";
            std::cout << "  --help                   Show this help message\n\n";
            std::cout << "Examples:\n";
            std::cout << "  wallet-generator\n";
            std::cout << "  wallet-generator --name my-wallet --output-file wallet.json\n";
            std::cout << "  wallet-generator --save-private-key --show-private-key\n";
            std::cout << "  wallet-generator --preprod-doctor\n";
            return 0;
        }
        else if (arg == "--preprod-doctor")
        {
            run_preprod_doctor = true;
        }
    }

    if (run_preprod_doctor)
    {
        return run_doctor();
    }

    // Generate wallet
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║        Midnight Wallet Generator - Preprod Testnet        ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";

    Wallet wallet = WalletManager::create_wallet(wallet_name);

    std::cout << "║ Generating wallet: " << std::setw(40) << std::left << wallet_name << "║\n";
    std::cout << "║ Network: " << std::setw(49) << std::left << wallet.network << "║\n";
    std::cout << "║ Created: " << std::setw(49) << std::left << wallet.created << "║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";

    // Display public information
    std::cout << "║ PUBLIC KEY:                                                ║\n";
    std::cout << "║ " << std::setw(62) << std::left << wallet.public_key.substr(0, 60) << "║\n";
    if (wallet.public_key.length() > 60)
    {
        std::cout << "║ " << std::setw(62) << std::left << wallet.public_key.substr(60) << "║\n";
    }

    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ UNSHIELD ADDRESS (for receiving NIGHT):                    ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ " << std::setw(62) << wallet.unshield_address << "║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ SHIELDED ADDRESS (for private transactions):               ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ " << std::setw(62) << wallet.shielded_address << "║\n";
    std::cout << "║                                                            ║\n";

    // Display private information if requested
    if (show_private_key)
    {
        std::cout << "╠════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ ⚠️  PRIVATE KEY (DO NOT SHARE!):                            ║\n";
        std::cout << "║ " << std::setw(62) << wallet.private_key.substr(0, 60) << "║\n";
        if (wallet.private_key.length() > 60)
        {
            std::cout << "║ " << std::setw(62) << wallet.private_key.substr(60) << "║\n";
        }
        std::cout << "║                                                            ║\n";
        std::cout << "║ RECOVERY SEED (BACKUP THIS NOW!):                         ║\n";
        std::cout << "║ " << std::setw(62) << wallet.seed.substr(0, 60) << "║\n";
        if (wallet.seed.length() > 60)
        {
            std::cout << "║ " << std::setw(62) << wallet.seed.substr(60) << "║\n";
        }
    }

    std::cout << "╠════════════════════════════════════════════════════════════╣\n";

    // Save files if requested
    if (!output_file.empty())
    {
        std::ofstream file(output_file);
        file << WalletManager::export_to_json(wallet, show_private_key);
        file.close();

        std::cout << "║ ✓ Wallet exported to: " << std::setw(42) << std::left
                  << output_file << "║\n";
    }

    if (save_private_key)
    {
        std::string privkey_file = wallet_name + "_private.pem";
        WalletManager::save_private_key(wallet, privkey_file);

        std::cout << "║ ✓ Private key saved to: " << std::setw(40) << std::left
                  << privkey_file << "║\n";
    }

    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ NEXT STEPS:                                                ║\n";
    std::cout << "║ 1. Copy your UNSHIELD ADDRESS above                        ║\n";
    std::cout << "║ 2. Go to: https://faucet.preprod.midnight.network/         ║\n";
    std::cout << "║ 3. Paste address and request NIGHT tokens                  ║\n";
    std::cout << "║ 4. Check balance: https://indexer.preprod.midnight.network/║\n";
    std::cout << "║ 5. Use for testing transactions                            ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║ ⚠️  SECURITY WARNING:                                       ║\n";
    std::cout << "║ - Never share your private key                             ║\n";
    std::cout << "║ - Never commit private key to Git                          ║\n";
    std::cout << "║ - Backup recovery seed in secure location                  ║\n";
    std::cout << "║ - This is testnet - for demo purposes only                 ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    return 0;
}
