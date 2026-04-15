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
#include <array>
#include <cctype>

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
     * Generate Midnight Bech32m addresses for preprod.
     */
    class AddressGenerator
    {
    public:
        static std::string generate_unshield_address(const std::string &public_key)
        {
            std::array<uint8_t, 32> payload{};
            if (!hex_to_bytes32(public_key, payload))
            {
                return "";
            }
            return bech32m_encode("mn_addr_preprod", payload);
        }

        static std::string generate_shielded_address(const std::string &private_key)
        {
            std::array<uint8_t, 32> payload{};
            if (!hex_to_bytes32(private_key, payload))
            {
                return "";
            }

            // Mock transformation to derive a distinct shielded payload.
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<uint8_t>(payload[i] ^ static_cast<uint8_t>((i * 17U + 0xA5U) & 0xFFU));
            }

            return bech32m_encode("mn_shield-addr_preprod", payload);
        }

    private:
        static constexpr uint32_t kBech32mConst = 0x2BC830A3;
        static constexpr const char *kBech32Charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

        static uint32_t bech32_polymod(const std::vector<uint8_t> &values)
        {
            static constexpr uint32_t gen[5] = {0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3};
            uint32_t chk = 1;
            for (auto value : values)
            {
                const uint32_t top = chk >> 25;
                chk = ((chk & 0x1FFFFFF) << 5) ^ value;
                for (int i = 0; i < 5; ++i)
                {
                    chk ^= ((top >> i) & 1) ? gen[i] : 0;
                }
            }
            return chk;
        }

        static std::vector<uint8_t> bech32_hrp_expand(const std::string &hrp)
        {
            std::vector<uint8_t> ret;
            ret.reserve(hrp.size() * 2 + 1);
            for (unsigned char c : hrp)
            {
                ret.push_back(c >> 5);
            }
            ret.push_back(0);
            for (unsigned char c : hrp)
            {
                ret.push_back(c & 31);
            }
            return ret;
        }

        static std::vector<uint8_t> convert_bits(const uint8_t *data, size_t data_len, int from_bits, int to_bits, bool pad)
        {
            int acc = 0;
            int bits = 0;
            const int maxv = (1 << to_bits) - 1;
            const int max_acc = (1 << (from_bits + to_bits - 1)) - 1;
            std::vector<uint8_t> ret;
            ret.reserve((data_len * from_bits + to_bits - 1) / to_bits);

            for (size_t i = 0; i < data_len; ++i)
            {
                const int value = data[i];
                acc = ((acc << from_bits) | value) & max_acc;
                bits += from_bits;
                while (bits >= to_bits)
                {
                    bits -= to_bits;
                    ret.push_back((acc >> bits) & maxv);
                }
            }

            if (pad)
            {
                if (bits != 0)
                {
                    ret.push_back(static_cast<uint8_t>((acc << (to_bits - bits)) & maxv));
                }
            }
            else if (bits >= from_bits || ((acc << (to_bits - bits)) & maxv) != 0)
            {
                return {};
            }

            return ret;
        }

        static std::string bech32m_encode(const std::string &hrp, const std::array<uint8_t, 32> &payload)
        {
            std::vector<uint8_t> data;
            data.push_back(0);
            auto converted = convert_bits(payload.data(), payload.size(), 8, 5, true);
            data.insert(data.end(), converted.begin(), converted.end());

            auto values = bech32_hrp_expand(hrp);
            values.insert(values.end(), data.begin(), data.end());
            values.insert(values.end(), 6, 0);
            const uint32_t polymod = bech32_polymod(values) ^ kBech32mConst;

            std::array<uint8_t, 6> checksum{};
            for (int i = 0; i < 6; ++i)
            {
                checksum[i] = (polymod >> (5 * (5 - i))) & 31;
            }

            std::string out = hrp + "1";
            out.reserve(hrp.size() + 1 + data.size() + checksum.size());
            for (auto v : data)
            {
                out.push_back(kBech32Charset[v]);
            }
            for (auto v : checksum)
            {
                out.push_back(kBech32Charset[v]);
            }
            return out;
        }

        static bool hex_to_bytes32(const std::string &hex_input, std::array<uint8_t, 32> &out)
        {
            std::string hex = hex_input;
            if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0)
            {
                hex = hex.substr(2);
            }

            if (hex.size() < 64)
            {
                return false;
            }

            auto nibble = [](char c) -> int
            {
                if (c >= '0' && c <= '9')
                {
                    return c - '0';
                }
                const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower >= 'a' && lower <= 'f')
                {
                    return 10 + (lower - 'a');
                }
                return -1;
            };

            for (size_t i = 0; i < out.size(); ++i)
            {
                const int hi = nibble(hex[i * 2]);
                const int lo = nibble(hex[i * 2 + 1]);
                if (hi < 0 || lo < 0)
                {
                    return false;
                }
                out[i] = static_cast<uint8_t>((hi << 4) | lo);
            }

            return true;
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
        std::cout << (check.passed ? "[PASS] " : "[FAIL] ") << check.name << "\n";
        std::cout << "   Endpoint: " << check.endpoint << "\n";
        std::cout << "   Detail: " << check.detail << "\n";
    };

    auto run_doctor = [&]() -> int
    {
        using midnight::network::NetworkConfig;
        using midnight::network::MidnightNodeRPC;
        using midnight::network::NetworkClient;

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
            NetworkClient proof(proof_endpoint, 3000);
            proof_check.passed = proof.is_connected();
            proof_check.detail = proof_check.passed ? "HTTP endpoint reachable" : "Connectivity check failed";
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
            std::cout << "Result: READY for end-to-end preprod validation.\n";
            return 0;
        }

        std::cout << "Result: NOT READY - environment is not fully prepared for end-to-end preprod validation.\n";
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
