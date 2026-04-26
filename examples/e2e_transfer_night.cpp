/**
 * Midnight E2E Validation Tool — Correct Workflow
 *
 * Demonstrates the CORRECT Midnight token architecture:
 *   - NIGHT tokens live in the Zswap UTXO set (NOT Substrate Balances)
 *   - Balance is queried via Indexer GraphQL (unshieldedUTXOs)
 *   - Transfers require ZK proofs + Pedersen-Schnorr signatures
 *   - Transaction format: midnight:transaction[v9]
 *   - DUST is generated from registered NIGHT UTXOs
 *
 * Commands:
 *   --generate, -g        Generate new HD wallet + show faucet address
 *   --info, -i            Query Midnight runtime info (pallets, Zswap state)
 *   --balance, -b         Query NIGHT balance via Indexer (correct method)
 *   --utxos, -u           List individual unshielded UTXOs
 *   --help, -h            Show usage
 *
 * Required:
 *   --mnemonic, -m WORDS  24-word BIP39 mnemonic (for balance/utxos)
 *   --rpc, -r URL         RPC endpoint (default: preview)
 *   --indexer URL         Indexer GraphQL URL (default: preview)
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <fstream>

#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/network/substrate_rpc.hpp"
#include "midnight/network/indexer_client.hpp"
#include "midnight/network/network_config.hpp"

// ═══════════════════════════════════════════════════════════════
// Display Helpers
// ═══════════════════════════════════════════════════════════════

static const int BOX_W = 66;

static void box_top(const std::string &title)
{
    std::cout << "\n";
    std::string bar(BOX_W, '=');
    std::cout << "  " << bar << "\n";
    std::cout << "  " << title << "\n";
    std::cout << "  " << bar << "\n";
}

static void box_row(const std::string &label, const std::string &value)
{
    std::cout << "  " << std::setw(22) << std::left << label << value << "\n";
}

static void box_sep()
{
    std::cout << "  " << std::string(BOX_W, '-') << "\n";
}

static void box_end()
{
    std::cout << "  " << std::string(BOX_W, '=') << "\n\n";
}

// ═══════════════════════════════════════════════════════════════
// Command: Generate Wallet
// ═══════════════════════════════════════════════════════════════

static int cmd_generate()
{
    using namespace midnight::wallet;

    box_top("Midnight Wallet Generator — Preview Network");

    std::string mnemonic = bip39::generate_mnemonic(24);
    auto hd = HDWallet::from_mnemonic(mnemonic);

    auto night_key = hd.derive_night(0, 0);
    auto dust_key = hd.derive_dust(0, 0);
    auto zswap_key = hd.derive_zswap(0, 0);

    // Generate Bech32m unshielded address (mn_addr_preview1...)
    // This matches the official @midnight-ntwrk/wallet-sdk-address-format
    std::string night_addr = address::encode_unshielded(
        night_key.public_key, address::Network::Preview);
    std::string dust_addr = address::encode_dust(
        dust_key.public_key, address::Network::Preview);

    box_row("Network:", "Midnight Preview");
    box_row("Source:", "Native BIP39/SLIP-10 HD Wallet");
    box_sep();

    // Display mnemonic
    std::cout << "  MNEMONIC (SAVE THIS — NEVER SHARE):\n";
    std::istringstream iss(mnemonic);
    std::string word;
    int word_num = 0;
    std::string line;
    while (iss >> word)
    {
        word_num++;
        line += std::to_string(word_num) + "." + word + " ";
        if (word_num % 6 == 0 || word_num == 24)
        {
            std::cout << "    " << line << "\n";
            line.clear();
        }
    }

    box_sep();
    std::cout << "  KEYS (hex):\n";
    box_row("  Night Key:", night_key.address.substr(0, 48) + "...");
    box_row("  Dust Key:", dust_key.address.substr(0, 48) + "...");
    box_row("  Zswap Key:", zswap_key.address.substr(0, 48) + "...");
    box_sep();

    std::cout << "  UNSHIELDED ADDRESS (Bech32m — USE THIS FOR FAUCET):\n";
    std::cout << "    " << night_addr << "\n";
    box_sep();

    std::cout << "  DUST ADDRESS (Bech32m):\n";
    std::cout << "    " << dust_addr << "\n";
    box_sep();

    std::cout << "  MIDNIGHT TOKEN ARCHITECTURE:\n";
    std::cout << "    - NIGHT: public unshielded token (UTXO model)\n";
    std::cout << "    - DUST:  shielded, non-transferable (pays TX fees)\n";
    std::cout << "    - Transfers use ZK proofs + Pedersen-Schnorr signatures\n";
    std::cout << "    - Address format: Bech32m (mn_addr_preview1...)\n";
    box_sep();

    std::cout << "  NEXT STEPS:\n";
    std::cout << "    1. Go to: https://faucet.preview.midnight.network/\n";
    std::cout << "    2. Paste the UNSHIELDED ADDRESS (mn_addr_preview1...) above\n";
    std::cout << "    3. Request NIGHT tokens\n";
    std::cout << "    4. Run: e2e_transfer_night -m \"...\" --balance\n";
    std::cout << "    5. Run: e2e_transfer_night -m \"...\" --utxos\n";
    box_end();

    // Save to file
    std::ofstream f("wallet_preview.txt");
    if (f.is_open())
    {
        f << "Midnight Preview Wallet\n";
        f << "=======================\n";
        f << "Mnemonic: " << mnemonic << "\n";
        f << "\n--- Unshielded Address (Bech32m — for faucet) ---\n";
        f << "Night Address: " << night_addr << "\n";
        f << "Dust Address:  " << dust_addr << "\n";
        f << "\n--- Raw Keys (hex) ---\n";
        f << "Night Public Key: " << night_key.address << "\n";
        f << "Dust Public Key:  " << dust_key.address << "\n";
        f << "Zswap Public Key: " << zswap_key.address << "\n";
        f << "\nFaucet: https://faucet.preview.midnight.network/\n";
        f << "Note: Paste the mn_addr_preview1... address into the faucet.\n";
        f.close();
        std::cout << "  Wallet saved to: wallet_preview.txt\n\n";
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════
// Command: Runtime Info
// ═══════════════════════════════════════════════════════════════

static int cmd_info(const std::string &rpc_url)
{
    using namespace midnight::network;

    box_top("Midnight Runtime Info — Preview Network");
    box_row("RPC:", rpc_url);
    box_sep();

    try
    {
        SubstrateRPC rpc(rpc_url);

        // Basic node info
        auto chain = rpc.get_chain_name();
        auto version = rpc.get_runtime_version();
        auto health = rpc.get_system_health();
        auto node_ver = rpc.get_node_version();
        auto genesis = rpc.get_genesis_hash();

        box_row("Chain:", chain);
        box_row("Node Version:", node_ver);
        box_row("Spec Name:", version.spec_name);
        box_row("Spec Version:", std::to_string(version.spec_version));
        box_row("TX Version:", std::to_string(version.tx_version));
        box_row("Peers:", std::to_string(health.value("peers", 0)));
        box_row("Syncing:", health.value("isSyncing", false) ? "yes" : "no");
        box_row("Genesis:", genesis.substr(0, 48) + "...");

        // Finalized block
        auto head = rpc.get_finalized_head();
        auto header = rpc.get_header(head);
        box_row("Finalized Block:", head.substr(0, 48) + "...");
        if (header.contains("number"))
        {
            std::string num = header["number"].get<std::string>();
            box_row("Block Number:", num);
        }

        box_sep();

        // Midnight-specific RPC
        std::cout << "  MIDNIGHT-SPECIFIC RPC:\n";

        auto ledger_ver = rpc.midnight_ledger_version();
        std::cout << "    midnight_ledgerVersion: " << ledger_ver.dump() << "\n";

        auto zswap_root = rpc.midnight_zswap_state_root();
        if (!zswap_root.empty())
            std::cout << "    midnight_zswapStateRoot: " << zswap_root.substr(0, 48) << "...\n";

        auto api_versions = rpc.midnight_api_versions();
        if (!api_versions.empty())
            std::cout << "    midnight_apiVersions: " << api_versions.dump().substr(0, 80) << "...\n";

        box_sep();

        // Pending transaction format analysis
        auto pending = rpc.get_pending_extrinsics();
        std::cout << "  PENDING TRANSACTIONS: " << pending.size() << "\n";
        if (!pending.empty())
        {
            // Decode first TX format
            auto &tx = pending[0];
            // Search for the transaction type identifier
            std::string type_marker = "6d69646e69676874"; // "midnight" in hex
            auto pos = tx.find(type_marker);
            if (pos != std::string::npos)
            {
                std::cout << "    TX type: midnight:transaction (ZK proof-based)\n";
            }
        }

        box_end();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "  ERROR: " << e.what() << "\n";
        box_end();
        return 1;
    }
}

// ═══════════════════════════════════════════════════════════════
// Command: Balance Query (via Indexer — correct method)
// ═══════════════════════════════════════════════════════════════

static int cmd_balance(const std::string &mnemonic,
                       const std::string &rpc_url,
                       const std::string &indexer_url)
{
    using namespace midnight::wallet;
    using namespace midnight::network;

    auto hd = HDWallet::from_mnemonic(mnemonic);
    auto night_key = hd.derive_night(0, 0);

    box_top("NIGHT Balance — Midnight Preview");
    box_row("Account:", night_key.address.substr(0, 48) + "...");
    box_row("RPC:", rpc_url);
    box_row("Indexer:", indexer_url);
    box_sep();

    // 1. RPC: basic node info
    try
    {
        SubstrateRPC rpc(rpc_url);
        auto chain = rpc.get_chain_name();
        auto version = rpc.get_runtime_version();
        box_row("Chain:", chain);
        box_row("Spec Version:", std::to_string(version.spec_version));

        // System.Account (nonce only on Midnight — balance is always 0)
        auto info = rpc.get_account_info(night_key.address);
        box_row("System Nonce:", std::to_string(info.nonce));
        box_row("System Balance:", std::to_string(info.free) + " (always 0 — NIGHT is UTXO)");
    }
    catch (const std::exception &e)
    {
        std::cout << "  RPC Warning: " << e.what() << "\n";
    }

    box_sep();

    // 2. Indexer: real NIGHT balance from Zswap UTXO set
    std::cout << "  NIGHT BALANCE (via Indexer — correct method):\n";

    try
    {
        IndexerClient indexer(indexer_url);

        // Query wallet state
        auto wallet_state = indexer.query_wallet_state(night_key.address);

        if (!wallet_state.error.empty())
        {
            std::cout << "    Indexer error: " << wallet_state.error << "\n";
            std::cout << "    (Indexer may use a different address format)\n";
        }
        else
        {
            box_row("  Unshielded NIGHT:", wallet_state.unshielded_balance + " units");
            box_row("  DUST Balance:", wallet_state.dust_balance);
            box_row("  UTXO Count:", std::to_string(wallet_state.available_utxo_count));
            box_row("  Synced:", wallet_state.synced ? "yes" : "no");
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "    Indexer query failed: " << e.what() << "\n";
        std::cout << "    This is expected if the indexer requires authentication\n";
        std::cout << "    or uses a different address format.\n";
    }

    box_sep();

    // 3. Try raw GraphQL introspection
    std::cout << "  INDEXER SCHEMA DISCOVERY:\n";
    try
    {
        IndexerClient indexer(indexer_url);
        auto schema = indexer.graphql_query(
            R"({ __schema { queryType { fields { name } } } })");

        if (schema.contains("__schema") &&
            schema["__schema"].contains("queryType") &&
            schema["__schema"]["queryType"].contains("fields"))
        {
            for (const auto &field : schema["__schema"]["queryType"]["fields"])
            {
                std::string name = field["name"].get<std::string>();
                std::cout << "    → " << name << "\n";
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "    Schema query failed: " << e.what() << "\n";
    }

    box_sep();

    std::cout << "  ARCHITECTURE NOTES:\n";
    std::cout << "    - NIGHT balance lives in Zswap UTXO set (NOT System.Account)\n";
    std::cout << "    - System.Account.free is always 0 for regular users\n";
    std::cout << "    - Real balance = sum of unshielded NIGHT UTXOs\n";
    std::cout << "    - Transfer requires: UTXO selection + ZK proof + Pedersen-Schnorr sig\n";
    std::cout << "    - TX format: midnight:transaction[v9](signature[v1],proof,pedersen-schnorr[v1])\n";
    box_end();

    return 0;
}

// ═══════════════════════════════════════════════════════════════
// Command: List UTXOs
// ═══════════════════════════════════════════════════════════════

static int cmd_utxos(const std::string &mnemonic,
                     const std::string &indexer_url)
{
    using namespace midnight::wallet;
    using namespace midnight::network;

    auto hd = HDWallet::from_mnemonic(mnemonic);
    auto night_key = hd.derive_night(0, 0);

    box_top("Unshielded NIGHT UTXOs — Preview Network");
    box_row("Account:", night_key.address.substr(0, 48) + "...");
    box_row("Indexer:", indexer_url);
    box_sep();

    try
    {
        IndexerClient indexer(indexer_url);
        auto utxos = indexer.query_utxos(night_key.address);

        if (utxos.empty())
        {
            std::cout << "  No unshielded UTXOs found.\n";
            std::cout << "  Fund this address via: https://faucet.preview.midnight.network/\n";
        }
        else
        {
            uint64_t total = 0;
            for (size_t i = 0; i < utxos.size(); i++)
            {
                const auto &utxo = utxos[i];
                std::cout << "  UTXO #" << i << ":\n";
                std::cout << "    TX Hash: " << utxo.tx_hash.substr(0, 48) << "...\n";
                std::cout << "    Index:   " << utxo.output_index << "\n";
                std::cout << "    Value:   " << utxo.amount << " units\n";
                total += utxo.amount;
            }
            box_sep();
            box_row("Total UTXOs:", std::to_string(utxos.size()));
            box_row("Total Balance:", std::to_string(total) + " units");
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "  Indexer error: " << e.what() << "\n";
    }

    box_end();
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════

int main(int argc, char *argv[])
{
    std::string mnemonic;
    bool do_generate = false;
    bool do_info = false;
    bool do_balance = false;
    bool do_utxos = false;
    std::string rpc_url = "https://rpc.preview.midnight.network";
    std::string indexer_url = "https://indexer.preview.midnight.network/api/v4/graphql";

    // Parse arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--generate" || arg == "-g")
            do_generate = true;
        else if (arg == "--info" || arg == "-i")
            do_info = true;
        else if (arg == "--balance" || arg == "-b")
            do_balance = true;
        else if (arg == "--utxos" || arg == "-u")
            do_utxos = true;
        else if ((arg == "--mnemonic" || arg == "-m") && i + 1 < argc)
            mnemonic = argv[++i];
        else if ((arg == "--rpc" || arg == "-r") && i + 1 < argc)
            rpc_url = argv[++i];
        else if (arg == "--indexer" && i + 1 < argc)
            indexer_url = argv[++i];
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Midnight E2E Validation Tool (Correct Workflow)\n\n";
            std::cout << "Commands:\n";
            std::cout << "  --generate, -g           Generate new wallet + show faucet address\n";
            std::cout << "  --info, -i               Query Midnight runtime info\n";
            std::cout << "  --balance, -b             Check NIGHT balance via Indexer (correct)\n";
            std::cout << "  --utxos, -u              List unshielded NIGHT UTXOs\n\n";
            std::cout << "Options:\n";
            std::cout << "  --mnemonic, -m WORDS     24-word BIP39 mnemonic\n";
            std::cout << "  --rpc, -r URL            RPC endpoint (default: preview)\n";
            std::cout << "  --indexer URL            Indexer GraphQL URL (default: preview)\n\n";
            std::cout << "Architecture:\n";
            std::cout << "  Midnight does NOT use Substrate Balances pallet.\n";
            std::cout << "  NIGHT tokens live in the Zswap UTXO set.\n";
            std::cout << "  Balance = sum of unshielded NIGHT UTXOs (via Indexer).\n";
            std::cout << "  Transfer = ZK proof + Pedersen-Schnorr sig + DUST fee.\n";
            std::cout << "  TX format: midnight:transaction[v9]\n\n";
            std::cout << "Examples:\n";
            std::cout << "  e2e_transfer_night --generate\n";
            std::cout << "  e2e_transfer_night --info\n";
            std::cout << "  e2e_transfer_night -m \"word1 word2 ...\" --balance\n";
            std::cout << "  e2e_transfer_night -m \"word1 word2 ...\" --utxos\n";
            return 0;
        }
    }

    // Dispatch
    if (do_generate)
        return cmd_generate();

    if (do_info)
        return cmd_info(rpc_url);

    if (mnemonic.empty() && (do_balance || do_utxos))
    {
        std::cerr << "Error: --mnemonic required for balance/utxo queries\n";
        return 1;
    }

    if (do_balance)
        return cmd_balance(mnemonic, rpc_url, indexer_url);

    if (do_utxos)
        return cmd_utxos(mnemonic, indexer_url);

    // Default: show info
    return cmd_info(rpc_url);
}
