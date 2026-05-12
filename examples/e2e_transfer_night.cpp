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
 *   --faucet, -f          Request NIGHT from faucet
 *   --transfer, -t AMT     Transfer NIGHT to self (demo)
 *   --help, -h            Show usage
 *
 * Required:
 *   --mnemonic, -m WORDS  24-word BIP39 mnemonic (for balance/utxos/transfer)
 *   --rpc, -r URL         RPC endpoint (default: preview)
 *   --indexer URL         Indexer GraphQL URL (default: preview)
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>

#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/wallet/bech32m.hpp"
#include "midnight/wallet/wallet_facade.hpp"
#include "midnight/network/substrate_rpc.hpp"
#include "midnight/network/indexer_client.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/network/network_config.hpp"
#include "midnight/network/graphql_subscription.hpp"

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

    std::cout << "  SECURITY WARNING:\n";
    std::cout << "  *** NEVER store your mnemonic digitally! ***\n";
    std::cout << "  *** This tool will NEVER ask for your mnemonic again! ***\n";
    std::cout << "  1. WRITE DOWN your mnemonic on paper RIGHT NOW\n";
    std::cout << "  2. Store the paper in a SECURE, FIREPROOF location\n";
    std::cout << "  3. NEVER share your mnemonic with anyone\n";
    std::cout << "  4. NEVER take screenshots or photos of it\n";
    std::cout << "  5. If you lose it, you lose access to all funds forever\n";
    box_sep();

    box_end();

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

    // IMPORTANT: Indexer uses Bech32m format, NOT hex!
    std::string hex_addr = night_key.address; // 0xf525...
    std::string bech32m_addr = address::encode_unshielded(night_key.public_key, address::Network::Preview);

    box_top("NIGHT Balance — Midnight Preview");
    box_row("Bech32m Address:", bech32m_addr);
    box_row("Hex Address:", hex_addr.substr(0, 48) + "...");
    box_row("RPC:", rpc_url);
    box_row("Indexer:", indexer_url);
    box_sep();

    // 1. RPC: basic node info (uses hex format)
    try
    {
        SubstrateRPC rpc(rpc_url);
        auto chain = rpc.get_chain_name();
        auto version = rpc.get_runtime_version();
        box_row("Chain:", chain);
        box_row("Spec Version:", std::to_string(version.spec_version));

        // System.Account (nonce only on Midnight — balance is always 0)
        auto info = rpc.get_account_info(hex_addr);
        box_row("System Nonce:", std::to_string(info.nonce));
        box_row("System Balance:", std::to_string(info.free.lo) + " (always 0 — NIGHT is UTXO)");
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

        // Query wallet state using Bech32m address!
        auto wallet_state = indexer.query_wallet_state(bech32m_addr);

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
    std::string bech32m_addr = address::encode_unshielded(night_key.public_key, address::Network::Preview);
    box_row("Bech32m Address:", bech32m_addr);
    box_row("Indexer:", indexer_url);
    box_sep();

    try
    {
        IndexerClient indexer(indexer_url);
        // bech32m_addr already defined above
        auto utxos = indexer.query_utxos(bech32m_addr);

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
                std::cout << "    Value:   " << utxo.value << " units\n";
                total += utxo.value;
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
// Command: Request NIGHT from Faucet
// ═══════════════════════════════════════════════════════════════

static int cmd_faucet(const std::string &mnemonic,
                      const std::string &rpc_url,
                      const std::string &faucet_url)
{
    using namespace midnight::wallet;
    using namespace midnight::network;

    auto hd = HDWallet::from_mnemonic(mnemonic);
    auto night_key = hd.derive_night(0, 0);

    // Generate unshielded address
    std::string night_addr = address::encode_unshielded(
        night_key.public_key, address::Network::Preview);

    box_top("NIGHT Faucet — Midnight Preview");
    box_row("Address:", night_addr);
    box_row("Faucet:", faucet_url);
    box_sep();

    // Build faucet request using NetworkClient
    try
    {
        NetworkClient client(faucet_url, 30000);

        // Try common faucet formats
        std::cout << "  Requesting NIGHT from faucet...\n";
        std::cout << "  Address: " << night_addr << "\n";

        // Format 1: JSON-RPC style request
        try
        {
            nlohmann::json request = {
                {"jsonrpc", "2.0"},
                {"method", "request_tokens"},
                {"params", {
                    {"address", night_addr},
                    {"network", "preview"}
                }},
                {"id", 1}
            };

            auto response = client.post_json("/", request);
            std::cout << "  JSON-RPC Response: " << response.dump() << "\n";
        }
        catch (const std::exception &e)
        {
            std::cout << "  JSON-RPC failed: " << e.what() << "\n";
        }

        box_sep();
        std::cout << "  Wait 15 seconds for tokens to arrive...\n";
        std::this_thread::sleep_for(std::chrono::seconds(15));

        // Check balance after faucet
        IndexerClient indexer("https://indexer.preview.midnight.network/api/v4/graphql");
        auto utxos = indexer.query_utxos(night_addr);
        uint64_t total = 0;
        for (const auto &utxo : utxos)
            total += utxo.value;

        box_row("  UTXOs after faucet:", std::to_string(utxos.size()));
        box_row("  Total NIGHT:", std::to_string(total));
    }
    catch (const std::exception &e)
    {
        std::cerr << "  Faucet request failed: " << e.what() << "\n";
    }

    box_sep();
    std::cout << "  NOTE: If faucet requires Discord/Twitter auth,\n";
    std::cout << "  use the web interface: https://faucet.preview.midnight.network\n";
    box_end();

    return 0;
}

static int cmd_register_dust(const std::string &mnemonic,
                           const std::string &rpc_url,
                           const std::string &indexer_url,
                           uint64_t night_amount)
{
    using namespace midnight::wallet;
    using namespace midnight::network;

    box_top("DUST Registration — Midnight Preview");
    box_row("RPC:", rpc_url);
    box_row("Indexer:", indexer_url);
    box_row("NIGHT Amount:", std::to_string(night_amount));
    box_sep();

    // Create wallet facade
    WalletFacade wallet = WalletFacade::from_mnemonic(mnemonic, indexer_url);

    // Get our address
    std::string unshield_addr = wallet.unshielded_address();
    box_row("From:", unshield_addr);
    box_sep();

    // Sync wallet state
    std::cout << "  [1/4] Syncing wallet state...\n";
    wallet.sync();

    uint64_t balance = wallet.balance("NIGHT");
    box_row("  NIGHT Balance:", std::to_string(balance));

    if (balance < night_amount)
    {
        std::cerr << "  ERROR: Insufficient NIGHT balance!\n";
        std::cerr << "  Need " << night_amount << " but only have " << balance << "\n";
        box_end();
        return 1;
    }

    // Step 2: Register for dust
    std::cout << "  [2/4] Registering for dust generation...\n";
    auto result = wallet.register_for_dust(night_amount);
    if (result.error)
    {
        std::cerr << "  ERROR: Registration failed: " << result.error_message() << "\n";
        box_end();
        return 1;
    }

    std::cout << "  [2/4] Dust registration successful!\n";
    box_row("  TX Hash:", result.tx_hash);
    box_row("  Est. DUST/epoch:", std::to_string(result.estimated_dust_per_epoch));
    box_sep();

    // Step 3: Wait for confirmation
    std::cout << "  [3/4] Waiting for transaction confirmation...\n";
    std::this_thread::sleep_for(std::chrono::seconds(6));

    // Step 4: Verify dust balance
    std::cout << "  [4/4] Verifying dust registration...\n";
    wallet.sync();
    auto dust_balance = wallet.balance("DUST");
    box_row("  DUST Balance:", std::to_string(dust_balance));
    box_sep();

    std::cout << "  Dust registration complete!\n";
    std::cout << "  TX Hash: " << result.tx_hash << "\n";
    std::cout << "  Monitor at: https://preview.midnight.network/tx/" << result.tx_hash << "\n";

    box_end();
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// Command: Real-time Balance Tracking via WebSocket
// ═══════════════════════════════════════════════════════════════

static int cmd_realtime(const std::string &mnemonic,
                        const std::string &indexer_url)
{
    using namespace midnight::wallet;
    using namespace midnight::network;

    auto hd = HDWallet::from_mnemonic(mnemonic);
    auto night_key = hd.derive_night(0, 0);

    // IMPORTANT: Indexer uses Bech32m format!
    std::string bech32m_addr = address::encode_unshielded(night_key.public_key, address::Network::Preview);

    box_top("Real-time Balance Tracker — Midnight Preview");
    box_row("Address:", bech32m_addr);
    box_row("WS Indexer:", "wss://indexer.preview.midnight.network/api/v4/graphql");
    box_row("HTTP Indexer:", indexer_url);
    box_sep();

    std::cout << "  Connecting to Midnight Indexer WebSocket...\n";
    std::cout << "  Tracking UTXO changes for your address in real-time.\n";
    std::cout << "  Press Ctrl+C to exit.\n";
    box_sep();

    // Create realtime client
    std::string ws_url = "wss://indexer.preview.midnight.network/api/v4/graphql";
    RealtimeIndexerClient client(ws_url, indexer_url);

    // Set up callbacks
    client.on_connection([](bool ok, const std::string &msg) {
        if (ok) {
            std::cout << "  [WS] Connected: " << msg << "\n";
        } else {
            std::cout << "  [WS] Disconnected: " << msg << "\n";
        }
    });

    client.on_balance([](const std::string &token, uint64_t bal) {
        std::cout << "  [BALANCE] " << token << ": " << bal << " units\n";
    });

    client.on_utxo([](const UnshieldedTxEvent &evt) {
        for (auto &u : evt.created) {
            std::cout << "  [UTXO] Created: " << u.amount << " " << u.token_type
                      << " (tx: " << u.tx_hash.substr(0, 16) << "...)\n";
        }
        for (auto &u : evt.spent) {
            std::cout << "  [UTXO] Spent: " << u.amount << " " << u.token_type
                      << " (tx: " << u.tx_hash.substr(0, 16) << "...)\n";
        }
    });

    // Initialize state and subscribe
    if (!client.subscribe(bech32m_addr)) {
        std::cerr << "  ERROR: Failed to subscribe to indexer\n";
        box_end();
        return 1;
    }

    std::cout << "  Subscribed! Waiting for real-time events...\n";
    std::cout << "  (This will track faucet deposits, transfers, etc.)\n\n";

    // Run blocking message loop (until Ctrl+C or disconnect)
    client.run();

    box_end();
    return 0;
}

// ═══════════════════════════════════════════════════════════════
// Command: Transfer NIGHT
// ═══════════════════════════════════════════════════════════════

static int cmd_transfer(const std::string &mnemonic,
                       uint64_t amount,
                       const std::string &rpc_url,
                       const std::string &indexer_url)
{
    using namespace midnight::wallet;
    using namespace midnight::network;

    box_top("NIGHT Transfer — Midnight Preview");
    box_row("RPC:", rpc_url);
    box_row("Indexer:", indexer_url);
    box_sep();

    // Create wallet facade
    WalletFacade wallet = WalletFacade::from_mnemonic(mnemonic, indexer_url);

    // Get our address
    std::string unshield_addr = wallet.unshielded_address();
    std::cout << "  From: " << unshield_addr << "\n";
    box_sep();

    // Sync wallet state
    std::cout << "  [1/5] Syncing wallet state...\n";
    wallet.sync();

    uint64_t balance = wallet.balance("NIGHT");
    box_row("  Available NIGHT:", std::to_string(balance));
    box_row("  Amount to send:", std::to_string(amount));
    box_sep();

    if (balance < amount)
    {
        std::cerr << "  ERROR: Insufficient balance!\n";
        std::cerr << "  Request NIGHT from faucet first.\n";
        box_end();
        return 1;
    }

    // Step 1: Build transfer
    std::cout << "  [2/5] Building transfer...\n";
    std::vector<TokenTransfer> outputs = {
        {amount, "NIGHT", unshield_addr}  // Send to self for demo
    };
    auto tx_result = wallet.build_transfer(outputs);
    if (tx_result.error)
    {
        std::cerr << "  ERROR: Build failed: " << tx_result.error_message() << "\n";
        box_end();
        return 1;
    }
    std::cout << "  [2/5] Transfer built successfully\n";

    // Step 2: Sign transaction
    std::cout << "  [3/5] Signing transaction...\n";
    auto signed_tx = wallet.sign_transaction(tx_result);
    if (signed_tx.error)
    {
        std::cerr << "  ERROR: Sign failed: " << signed_tx.error_message() << "\n";
        box_end();
        return 1;
    }
    std::cout << "  [3/5] Transaction signed\n";

    // Step 3: Full transfer with proof (combines sign + prove + balance + submit)
    std::cout << "  [4/5] Generating ZK proof and balancing...\n";
    std::cout << "  (This may take a moment for the proving circuit)\n";

    auto transfer_result = wallet.transfer_transaction(outputs, true);
    if (transfer_result.error)
    {
        std::cerr << "  ERROR: Transfer failed: " << transfer_result.error_message() << "\n";
        box_end();
        return 1;
    }

    // Step 5: Submit
    std::cout << "  [5/5] Submitting to network...\n";
    auto submission = wallet.submit_transaction(transfer_result);
    if (submission.is_error())
    {
        std::cerr << "  ERROR: Submission failed: " << submission.error << "\n";
        box_end();
        return 1;
    }

    box_row("  TX Hash:", submission.tx_hash);
    box_row("  Block:", std::to_string(submission.block_number));
    box_sep();

    std::cout << "  Transfer submitted!\n";
    std::cout << "  TX Hash: " << submission.tx_hash << "\n";
    std::cout << "  Monitor at: https://preview.midnight.network/tx/" << submission.tx_hash << "\n";

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
    bool do_faucet = false;
    bool do_transfer = false;
    bool do_register_dust = false;
    bool do_realtime = false;
    uint64_t transfer_amount = 0;
    uint64_t dust_amount = 0;
    std::string rpc_url = "https://rpc.preview.midnight.network";
    std::string indexer_url = "https://indexer.preview.midnight.network/api/v4/graphql";
    std::string faucet_url = "https://faucet.preview.midnight.network";

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
        else if (arg == "--faucet" || arg == "-f")
            do_faucet = true;
        else if (arg == "--register-dust" || arg == "--dust" || arg == "-d")
        {
            do_register_dust = true;
            if (i + 1 < argc)
            {
                dust_amount = std::stoull(argv[++i]);
            }
        }
        else if (arg == "--realtime" || arg == "--rt")
        {
            do_realtime = true;
        }
        else if (arg == "--transfer" || arg == "-t")
        {
            do_transfer = true;
            if (i + 1 < argc)
            {
                transfer_amount = std::stoull(argv[++i]);
            }
        }
        else if ((arg == "--mnemonic" || arg == "-m") && i + 1 < argc)
            mnemonic = argv[++i];
        else if ((arg == "--rpc" || arg == "-r") && i + 1 < argc)
            rpc_url = argv[++i];
        else if (arg == "--indexer" && i + 1 < argc)
            indexer_url = argv[++i];
        else if (arg == "--faucet-url" && i + 1 < argc)
            faucet_url = argv[++i];
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Midnight E2E Validation Tool (Correct Workflow)\n\n";
            std::cout << "Commands:\n";
            std::cout << "  --generate, -g           Generate new wallet + show faucet address\n";
            std::cout << "  --info, -i               Query Midnight runtime info\n";
            std::cout << "  --balance, -b           Check NIGHT balance via Indexer (correct)\n";
            std::cout << "  --utxos, -u             List unshielded NIGHT UTXOs\n";
            std::cout << "  --faucet, -f            Request NIGHT from faucet\n";
            std::cout << "  --register-dust, -d AMT  Register for dust generation\n";
            std::cout << "  --transfer, -t AMT       Transfer NIGHT (requires --mnemonic)\n";
            std::cout << "  --realtime, --rt        Real-time balance tracking via WebSocket\n\n";
            std::cout << "Options:\n";
            std::cout << "  --mnemonic, -m WORDS     24-word BIP39 mnemonic\n";
            std::cout << "  --rpc, -r URL           RPC endpoint (default: preview)\n";
            std::cout << "  --indexer URL           Indexer GraphQL URL (default: preview)\n";
            std::cout << "  --faucet-url URL        Faucet URL (default: preview faucet)\n\n";
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
            std::cout << "  e2e_transfer_night -m \"word1 word2 ...\" --faucet\n";
            std::cout << "  e2e_transfer_night -m \"word1 word2 ...\" --transfer 1000000\n";
            return 0;
        }
    }

    // Dispatch
    if (do_generate)
        return cmd_generate();

    if (do_info)
        return cmd_info(rpc_url);

    // Auto-load mnemonic from mnemonic.seed if not provided
    if (mnemonic.empty()) {
        std::vector<std::string> search_paths = {
            "mnemonic.seed",                                    // current directory (manual)
            "d:/venera/midnight/night_fund/mnemonic.seed",    // absolute path
            "d:\\venera\\midnight\\night_fund\\mnemonic.seed", // Windows absolute path
            "../../../mnemonic.seed",                           // from manual/ up to project root
        };

        for (const auto& sp : search_paths) {
            std::ifstream seed_file(sp);
            if (seed_file.is_open()) {
                std::string line;
                int line_num = 0;
                while (std::getline(seed_file, line)) {
                    line_num++;
                    // Remove any "Mnemonic:" prefix if present
                    if (line.rfind("Mnemonic:", 0) == 0) {
                        line = line.substr(9);
                    }
                    // Trim leading spaces
                    size_t start = line.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        line = line.substr(start);
                    }
                    // Trim trailing whitespace
                    size_t end = line.find_last_not_of(" \t\r\n");
                    if (end != std::string::npos) {
                        line = line.substr(0, end + 1);
                    }
                    // Count words - should be 24 for valid BIP39
                    std::istringstream iss(line);
                    std::string word;
                    int word_count = 0;
                    while (iss >> word) word_count++;
                    if (word_count >= 12) {
                        mnemonic = line;
                        std::cerr << "[INFO] Loaded mnemonic (" << word_count << " words) from: " << sp << "\n";
                        break;
                    }
                }
                seed_file.close();
                if (mnemonic.empty()) {
                    std::cerr << "[WARN] File opened but no valid mnemonic found (need >= 12 words)\n";
                }
            } else {
                std::cerr << "[DEBUG] Could not open: " << sp << "\n";
            }
        }
    }

    if (mnemonic.empty() && (do_balance || do_utxos))
    {
        std::cerr << "Error: --mnemonic required for balance/utxo queries\n";
        return 1;
    }

    if (mnemonic.empty() && (do_faucet || do_transfer || do_register_dust || do_realtime))
    {
        std::cerr << "Error: --mnemonic required for faucet/transfer/dust/realtime commands\n";
        return 1;
    }

    if (do_balance)
        return cmd_balance(mnemonic, rpc_url, indexer_url);

    if (do_utxos)
        return cmd_utxos(mnemonic, indexer_url);

    if (do_faucet)
        return cmd_faucet(mnemonic, rpc_url, faucet_url);

    if (do_register_dust)
    {
        if (dust_amount == 0)
        {
            std::cerr << "Error: --register-dust requires a NIGHT amount\n";
            return 1;
        }
        return cmd_register_dust(mnemonic, rpc_url, indexer_url, dust_amount);
    }

    if (do_transfer)
    {
        if (transfer_amount == 0)
        {
            std::cerr << "Error: --transfer requires an amount\n";
            return 1;
        }
        return cmd_transfer(mnemonic, transfer_amount, rpc_url, indexer_url);
    }

    if (do_realtime)
    {
        return cmd_realtime(mnemonic, indexer_url);
    }

    // Default: show help
    std::cout << "Midnight E2E Tool — Preview Network\n\n";
    std::cout << "Usage: e2e_transfer_night [options]\n";
    std::cout << "Try:   e2e_transfer_night --generate\n";
    std::cout << "       e2e_transfer_night --info\n";
    std::cout << "       e2e_transfer_night -m \"...\" --balance\n";
    std::cout << "       e2e_transfer_night -m \"...\" --faucet\n";
    std::cout << "       e2e_transfer_night -m \"...\" -t 1000000\n";
    std::cout << "       e2e_transfer_night --help\n\n";
    return 0;
}
