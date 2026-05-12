// Midnight Wallet Generator - Preview Network
// Generates a completely NEW wallet with NIGHT, DUST, and Shielded addresses

#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/wallet/shielded_address.hpp"
#include "midnight/core/logger.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

using namespace midnight::wallet;
using namespace midnight::address;

// Helper to convert bytes to hex string
std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    return oss.str();
}

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                                                                ║\n";
    std::cout << "║          MIDNIGHT WALLET GENERATOR - PREVIEW NETWORK          ║\n";
    std::cout << "║                                                                ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "Network:     PREVIEW\n";
    std::cout << "Indexer:    https://indexer.preview.midnight.network\n";
    std::cout << "Faucet:     https://faucet.preview.midnight.network\n";
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

        // ─── Step 2: Create HDWallet from mnemonic ──────────────────────────────
        std::cout << "[2/5] Creating HD wallet from mnemonic...\n";
        auto wallet = HDWallet::from_mnemonic(mnemonic, "");
        std::cout << "✓ HD wallet created (seed: " << wallet.master_seed().size() << " bytes)\n\n";

        // ─── Step 3: Derive NIGHT address ──────────────────────────────────────
        std::cout << "[3/5] Deriving NIGHT address (path: m/44'/2400'/0'/0/0)...\n";
        auto night_key = wallet.derive_night(0, 0);

        // Create Bech32m address for Night
        std::string night_address = address::encode_unshielded(
            night_key.public_key,
            Network::Preview
        );

        std::cout << "✓ NIGHT Address: " << night_address << "\n";
        std::cout << "  Public Key:   " << bytes_to_hex(night_key.public_key) << "\n";
        std::cout << "  Private Key:   [64 bytes - DO NOT SHARE]\n\n";

        // ─── Step 4: Derive DUST address ────────────────────────────────────────
        std::cout << "[4/5] Deriving DUST address (path: m/44'/2400'/0'/2/0)...\n";
        auto dust_key = wallet.derive_dust(0, 0);

        // Create Bech32m address for Dust
        std::string dust_address = address::encode_dust(
            dust_key.public_key,
            Network::Preview
        );

        std::cout << "✓ DUST Address: " << dust_address << "\n";
        std::cout << "  Public Key:  " << bytes_to_hex(dust_key.public_key) << "\n";
        std::cout << "  Private Key:  [64 bytes - DO NOT SHARE]\n\n";

        // ─── Step 5: Derive Shielded address ────────────────────────────────────
        std::cout << "[5/5] Deriving Shielded address (path: m/44'/2400'/0'/3/0)...\n";
        auto shielded_key = wallet.derive_zswap(0, 0);

        // Derive encryption key for shielded
        std::vector<uint8_t> enc_pub_key = wallet.derive_shielded_encryption_key(shielded_key.secret_key);

        // Create shielded address
        std::string shielded_address = address::encode_shielded(
            shielded_key.public_key,
            enc_pub_key,
            Network::Preview
        );

        std::cout << "✓ Shielded Address: " << shielded_address << "\n";
        std::cout << "  Coin Public Key:       " << bytes_to_hex(shielded_key.public_key) << "\n";
        std::cout << "  Encryption Public Key:  " << bytes_to_hex(enc_pub_key) << "\n\n";

        // ─── Save to JSON file ──────────────────────────────────────────────────
        std::ostringstream json;
        json << "{\n";
        json << "  \"config\": {\n";
        json << "    \"indexerHttpUrl\": \"https://indexer.preview.midnight.network/api/v4/graphql\",\n";
        json << "    \"indexerWsUrl\": \"wss://indexer.preview.midnight.network/api/v4/graphql\",\n";
        json << "    \"nodeHttpUrl\": \"https://node.preview.midnight.network\",\n";
        json << "    \"proofServerUrl\": \"http://127.0.0.1:6300\"\n";
        json << "  },\n";
        json << "  \"derivation_paths\": {\n";
        json << "    \"night\": \"m/44'/2400'/0'/0/0\",\n";
        json << "    \"dust\": \"m/44'/2400'/0'/2/0\",\n";
        json << "    \"shielded\": \"m/44'/2400'/0'/3/0\"\n";
        json << "  },\n";
        json << "  \"network\": \"preview\",\n";
        json << "  \"mnemonic\": \"" << mnemonic << "\",\n";
        json << "  \"night_address\": \"" << night_address << "\",\n";
        json << "  \"night_sk\": \"" << bytes_to_hex(night_key.secret_key) << "\",\n";
        json << "  \"night_pk\": \"" << bytes_to_hex(night_key.public_key) << "\",\n";
        json << "  \"dust_address\": \"" << dust_address << "\",\n";
        json << "  \"dust_sk\": \"" << bytes_to_hex(dust_key.secret_key) << "\",\n";
        json << "  \"dust_pk\": \"" << bytes_to_hex(dust_key.public_key) << "\",\n";
        json << "  \"shielded_address\": \"" << shielded_address << "\",\n";
        json << "  \"shielded_coin_public_key\": \"" << bytes_to_hex(shielded_key.public_key) << "\",\n";
        json << "  \"shielded_encryption_public_key\": \"" << bytes_to_hex(enc_pub_key) << "\"\n";
        json << "}\n";

        std::string filename = "wallet_new_preview.json";
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
        std::cout << "  Network:     PREVIEW\n";
        std::cout << "  NIGHT:       " << night_address << "\n";
        std::cout << "  DUST:         " << dust_address << "\n";
        std::cout << "  SHIELDED:     " << shielded_address << "\n\n";

        std::cout << "═══════════════════════════════════════════════════════════════════\n";
        std::cout << "                    NEXT STEPS                                \n";
        std::cout << "═══════════════════════════════════════════════════════════════════\n\n";

        std::cout << "  1️⃣  FUND YOUR WALLET:\n";
        std::cout << "     → Visit: https://faucet.preview.midnight.network/\n";
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
