// Midnight Wallet Generator - Preview/Preprod Network
// Generates a completely NEW wallet with NIGHT, DUST, and Shielded addresses

#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/wallet/shielded_address.hpp"
#include "midnight/ledger/ledger_backend.hpp"
#include "midnight/core/logger.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>

using namespace midnight;
using namespace midnight::wallet;

// Helper to convert bytes to hex string
std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    return oss.str();
}

std::string env_or(const char* name, const std::string& fallback) {
    if (const char* value = std::getenv(name)) {
        if (*value) {
            return value;
        }
    }
    return fallback;
}

std::string upper_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

int main() {
    std::cout << "\n";
    std::string network_id = env_or("MIDNIGHT_NETWORK", "preview");
    std::transform(network_id.begin(), network_id.end(), network_id.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string network_name = upper_copy(network_id);
    std::string indexer_http = env_or(
        "MIDNIGHT_INDEXER_BASE_URL",
        "https://indexer." + network_id + ".midnight.network");
    std::string indexer_ws = env_or(
        "MIDNIGHT_INDEXER_WS_BASE_URL",
        "wss://indexer." + network_id + ".midnight.network");
    std::string node_http = env_or(
        "MIDNIGHT_NODE_URL",
        "https://rpc." + network_id + ".midnight.network");
    std::string faucet_url = env_or(
        "MIDNIGHT_FAUCET_URL",
        "https://faucet." + network_id + ".midnight.network");

    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                                                                ║\n";
    std::cout << "║              MIDNIGHT WALLET GENERATOR                       ║\n";
    std::cout << "║                                                                ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "Network:     " << network_name << "\n";
    std::cout << "Indexer:    " << indexer_http << "\n";
    std::cout << "Faucet:     " << faucet_url << "\n";
    std::cout << "\n";

    try {
        // ─── Step 1: Generate random mnemonic ─────────────────────────────────────
        std::cout << "[1/5] Generating random 24-word mnemonic...\n";
        std::string mnemonic = midnight::wallet::bip39::generate_mnemonic(24);

        // Validate the mnemonic
        if (!midnight::wallet::bip39::validate_mnemonic(mnemonic)) {
            std::cerr << "✗ Error: Generated invalid mnemonic!\n";
            return 1;
        }
        std::cout << "✓ Mnemonic generated and validated\n\n";

        // ─── Step 2: Create BIP39 seed ────────────────────────────────────────
        std::cout << "[2/5] Creating BIP39 seed...\n";
        auto wallet = HDWallet::from_mnemonic(mnemonic, "");
        const std::string seed_hex = bytes_to_hex(wallet.master_seed());
        std::cout << "✓ Seed created (" << wallet.master_seed().size() << " bytes)\n\n";

        std::cout << "[3/5] Loading native Midnight ledger FFI...\n";
        std::string ffi_library;
        if (const char* env = std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY")) {
            ffi_library = env;
        }
#ifdef MIDNIGHT_EXAMPLE_LEDGER_FFI_LIBRARY
        if (ffi_library.empty()) {
            ffi_library = MIDNIGHT_EXAMPLE_LEDGER_FFI_LIBRARY;
        }
#endif
        auto backend = midnight::ledger::make_ffi_ledger_backend(ffi_library);
        auto* ffi_backend = dynamic_cast<midnight::ledger::FfiLedgerBackend*>(backend.get());
        if (ffi_backend == nullptr || !ffi_backend->is_available() || !ffi_backend->can_derive_wallet_addresses()) {
            std::cerr << "✗ Error: libmidnight_ledger_ffi is required for production-canonical wallet addresses.\n";
            if (ffi_backend != nullptr && !ffi_backend->last_error().empty()) {
                std::cerr << "  " << ffi_backend->last_error() << "\n";
            }
            std::cerr << "  Set MIDNIGHT_LEDGER_FFI_LIBRARY or build with MIDNIGHT_BUILD_LEDGER_FFI=ON.\n";
            return 1;
        }
        std::cout << "✓ Native ledger FFI loaded\n\n";

        // ─── Step 4: Derive canonical Midnight addresses ──────────────────────
        std::cout << "[4/5] Deriving canonical wallet addresses from ledger helpers...\n";
        const auto addresses = ffi_backend->derive_wallet_addresses(seed_hex, network_id);
        if (!addresses.success) {
            std::cerr << "✗ Error: " << addresses.error << "\n";
            return 1;
        }

        std::string night_address = addresses.unshielded;
        std::string dust_address = addresses.dust;
        std::string shielded_address = addresses.shielded;

        std::cout << "✓ NIGHT Address: " << night_address << "\n";
        std::cout << "  User Address: " << addresses.user_address << "\n";
        std::cout << "  Verifying Key: " << addresses.verifying_key << "\n\n";
        std::cout << "✓ DUST Address: " << dust_address << "\n";
        std::cout << "  Dust Public: " << addresses.dust_public << "\n\n";
        std::cout << "✓ Shielded Address: " << shielded_address << "\n";
        std::cout << "  Coin Public Key: " << addresses.coin_public << "\n\n";

        // ─── Step 5: Save to JSON file ─────────────────────────────────────────
        std::cout << "[5/5] Saving wallet file...\n";
        std::ostringstream json;
        json << "{\n";
        json << "  \"config\": {\n";
        json << "    \"indexerHttpUrl\": \"" << indexer_http << "/api/v4/graphql\",\n";
        json << "    \"indexerWsUrl\": \"" << indexer_ws << "/api/v4/graphql\",\n";
        json << "    \"nodeHttpUrl\": \"" << node_http << "\",\n";
        json << "    \"proofServerUrl\": \"http://127.0.0.1:6300\"\n";
        json << "  },\n";
        json << "  \"derivation_paths\": {\n";
        json << "    \"night\": \"m/44'/2400'/0'/0/0\",\n";
        json << "    \"dust\": \"m/44'/2400'/0'/2/0\",\n";
        json << "    \"shielded\": \"m/44'/2400'/0'/3/0\"\n";
        json << "  },\n";
        json << "  \"network\": \"" << network_id << "\",\n";
        json << "  \"mnemonic\": \"" << mnemonic << "\",\n";
        json << "  \"seed_hex\": \"" << seed_hex << "\",\n";
        json << "  \"night_address\": \"" << night_address << "\",\n";
        json << "  \"night_user_address\": \"" << addresses.user_address << "\",\n";
        json << "  \"night_verifying_key\": \"" << addresses.verifying_key << "\",\n";
        json << "  \"dust_address\": \"" << dust_address << "\",\n";
        json << "  \"dust_public\": \"" << addresses.dust_public << "\",\n";
        json << "  \"shielded_address\": \"" << shielded_address << "\",\n";
        json << "  \"shielded_coin_public_key\": \"" << addresses.coin_public << "\",\n";
        json << "  \"shielded_coin_public_key_tagged\": \"" << addresses.coin_public_tagged << "\"\n";
        json << "}\n";

        std::string filename = "wallet_" + network_id + ".json";
        std::ofstream out(filename);
        if (out.is_open()) {
            out << json.str();
            out.close();
            std::cout << "═══════════════════════════════════════════════════════════════════\n";
            std::cout << "✓ Wallet saved to: " << filename << "\n";
        } else {
            std::cerr << "✗ Error: Could not save wallet file!\n";
            return 1;
        }

        // ─── Display Summary ────────────────────────────────────────────────────
        std::cout << "\n";
        std::cout << "═══════════════════════════════════════════════════════════════════\n";
        std::cout << "                    ⚠️  SECURITY WARNING  ⚠️                       \n";
        std::cout << "═══════════════════════════════════════════════════════════════════\n";
        std::cout << "  • NEVER share your mnemonic or private keys!\n";
        std::cout << "  • Store this information securely (password manager, etc.)\n";
        std::cout << "  • Anyone with these keys has FULL access to your wallet\n";
        std::cout << "═══════════════════════════════════════════════════════════════════\n\n";

        std::cout << "═══════════════════════════════════════════════════════════════════\n";
        std::cout << "                    WALLET SUMMARY                               \n";
        std::cout << "═══════════════════════════════════════════════════════════════════\n";
        std::cout << "  Network:     " << network_name << "\n";
        std::cout << "  NIGHT:       " << night_address << "\n";
        std::cout << "  DUST:         " << dust_address << "\n";
        std::cout << "  SHIELDED:     " << shielded_address << "\n\n";

        std::cout << "═══════════════════════════════════════════════════════════════════\n";
        std::cout << "                    NEXT STEPS                                \n";
        std::cout << "═══════════════════════════════════════════════════════════════════\n\n";

        std::cout << "  1️⃣  FUND YOUR WALLET:\n";
        std::cout << "     → Visit: " << faucet_url << "/\n";
        std::cout << "     → Enter NIGHT address: " << night_address << "\n\n";

        std::cout << "  2️⃣  CHECK BALANCE:\n";
        std::cout << "     → Wait ~30 seconds for confirmation\n";
        std::cout << "     → Run: midnight-balance --address " << night_address << "\n\n";

        std::cout << "  3️⃣  REGISTER DUST (after funding):\n";
        std::cout << "     → Run: midnight-register-dust --dust-address " << dust_address << "\n";
        std::cout << "     → Fees: ~0.001 NIGHT\n\n";

        std::cout << "═══════════════════════════════════════════════════════════════════\n\n";

    } catch (const std::exception& e) {
        std::cerr << "\n✗ Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
