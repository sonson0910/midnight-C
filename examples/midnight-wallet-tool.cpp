/**
 * midnight-wallet-tool.cpp
 * Full wallet workflow in C++ using the Midnight SDK:
 *   1. Create wallet from mnemonic
 *   2. Sync wallet state (HTTP)
 *   3. Connect WebSocket for real-time UTXO tracking
 *   4. Query NIGHT balance + DUST registration status
 *   5. (Optionally) Register DUST generation
 *
 * Build: from .cmake-build/manual/ run:
 *   ninja midnight-wallet-tool
 *
 * Usage:
 *   ./midnight-wallet-tool [--tx-hash HASH] [--register-dust]
 *   ./midnight-wallet-tool --no-register    # just track, skip dust registration
 *
 * Environment variables:
 *   MIDNIGHT_MNEMONIC  - wallet mnemonic phrase
 *
 * NOTE: Private keys are NEVER saved to disk for security.
 */

#include "midnight/wallet/wallet_facade.hpp"
#include "midnight/network/graphql_subscription.hpp"
#include "midnight/network/indexer_client.hpp"
#include "midnight/core/logger.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <csignal>
#include <atomic>

using json = nlohmann::json;

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

static std::string to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    return oss.str();
}

// ─── WebSocket Realtime UTXO Tracker ──────────────────────────────────────────

static void track_realtime(midnight::wallet::WalletFacade& wallet,
                           const std::string& unshielded_addr,
                           int watch_seconds = 5) {
    std::cout << "\n=== [REAL-TIME] WebSocket UTXO Subscription ===\n";
    std::cout << "  Address: " << unshielded_addr << "\n";
    std::cout << "  Will watch for " << watch_seconds << "s (or Ctrl+C to stop)...\n";

    const std::string ws_url  = "wss://indexer.preprod.midnight.network/api/v4/graphql";
    const std::string http_url = "https://indexer.preprod.midnight.network/api/v4/graphql";

    midnight::network::RealtimeIndexerClient client(ws_url, http_url);

    client.on_connection([](bool ok, const std::string& msg) {
        if (ok) {
            std::cout << "  [WS] Connected to indexer WebSocket: " << msg << "\n";
        } else {
            std::cout << "  [WS] Disconnected: " << msg << "\n";
        }
    });

    client.on_balance([](const std::string& token, uint64_t bal) {
        std::cout << "  [WS-BAL] " << token << " balance updated: " << bal << "\n";
    });

    client.on_utxo([](const midnight::network::UnshieldedTxEvent& evt) {
        for (const auto& u : evt.created) {
            std::cout << "  [WS-UTXO] + " << u.amount << " " << u.token_type
                      << " (tx: " << u.tx_hash.substr(0, 16) << "...)\n";
        }
        for (const auto& u : evt.spent) {
            std::cout << "  [WS-UTXO] - " << u.amount << " " << u.token_type
                      << " (tx: " << u.tx_hash.substr(0, 16) << "...)\n";
        }
    });

    if (!client.subscribe(unshielded_addr)) {
        std::cout << "  [WS] Failed to subscribe — WebSocket may not be available.\n";
        std::cout << "  (This is non-fatal; continuing with HTTP-only state.)\n";
        return;
    }

    std::cout << "  [WS] Subscribed! Real-time UTXO tracking active.\n\n";

    // Run WebSocket loop with timeout
    std::thread loop([&]() {
        client.run();
    });

    auto start = std::chrono::steady_clock::now();
    while (g_running.load()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= watch_seconds) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    client.stop();
    if (loop.joinable()) loop.join();

    auto final_state = client.last_state();
    std::cout << "\n  [WS] Final state via WebSocket:\n";
    std::cout << "       NIGHT balance: " << final_state.unshielded_balance << "\n";
    std::cout << "       UTXOs: " << final_state.available_utxo_count << "\n";
}

// ─── Dust Status via HTTP ─────────────────────────────────────────────────────

static void check_dust_status(midnight::network::IndexerClient& indexer,
                               const std::string& dust_addr) {
    std::cout << "\n=== [DUST STATUS] ===\n";
    std::cout << "  Dust Address: " << dust_addr << "\n";

    auto status = indexer.query_dust_status(dust_addr);
    std::string status_str;
    switch (status) {
        case midnight::network::DustRegistrationStatus::REGISTERED:   status_str = "REGISTERED"; break;
        case midnight::network::DustRegistrationStatus::PENDING:       status_str = "PENDING";   break;
        case midnight::network::DustRegistrationStatus::NOT_REGISTERED: status_str = "NOT REGISTERED"; break;
        case midnight::network::DustRegistrationStatus::EXPIRED:     status_str = "EXPIRED";   break;
        default:                                                        status_str = "UNKNOWN";   break;
    }
    std::cout << "  Registration: " << status_str << "\n";
}

// ─── Register DUST ────────────────────────────────────────────────────────────

static bool register_dust(midnight::wallet::WalletFacade& wallet,
                         uint64_t night_balance,
                         bool force = false) {
    std::cout << "\n=== [DUST REGISTRATION] ===\n";

    auto state = wallet.state();

    if (state.dust.registered && !force) {
        std::cout << "  Already registered for " << state.dust.registered_night_amount
                  << " NIGHT.\n";
        std::cout << "  No action needed.\n";
        return true;
    }

    if (night_balance == 0) {
        std::cout << "  No NIGHT balance — cannot register.\n";
        std::cout << "  Request NIGHT from faucet first.\n";
        return false;
    }

    std::cout << "  Registering " << night_balance << " NIGHT for DUST generation...\n";

    midnight::wallet::DustRegistrationResult result;
    try {
        result = wallet.register_for_dust(night_balance);
    } catch (const std::exception& e) {
        std::cout << "  [EXCEPTION] " << e.what() << "\n";
        return false;
    }

    if (result.success) {
        std::cout << "  [OK] DUST registration submitted!\n";
        std::cout << "  TX Hash: " << result.tx_hash << "\n";
        std::cout << "  Est. DUST/epoch: " << result.estimated_dust_per_epoch << "\n";
        std::cout << "\n  Wallet is now registered for DUST generation.\n";
        std::cout << "  DUST rewards will start accruing in the next epoch.\n";
        return true;
    } else {
        std::cout << "  [FAIL] " << result.error_message() << "\n";
        return false;
    }
}

// ─── UTXO Detail ─────────────────────────────────────────────────────────────

static void show_utxos(midnight::network::IndexerClient& indexer,
                        const std::string& addr) {
    std::cout << "\n=== [UTXO DETAIL] ===\n";

    auto utxos = indexer.query_utxos(addr);
    std::cout << "  Total UTXOs: " << utxos.size() << "\n";

    for (size_t i = 0; i < utxos.size() && i < 10; ++i) {
        const auto& u = utxos[i];
        std::string tx_short = u.tx_hash.size() > 16 ? u.tx_hash.substr(0, 16) + "..." : u.tx_hash;
        std::cout << "  UTXO[" << i << "]: " << std::setw(12) << u.value
                  << " NIGHT  (tx: " << tx_short << ", idx: " << u.output_index << ")\n";
    }
    if (utxos.size() > 10) {
        std::cout << "  ... and " << (utxos.size() - 10) << " more UTXOs\n";
    }
}

// ─── Wallet Summary ───────────────────────────────────────────────────────────

static void show_wallet_summary(midnight::wallet::WalletFacade& wallet,
                                const std::string& unshielded_addr,
                                const std::string& dust_addr) {
    std::cout << "\n=== [WALLET SUMMARY] ===\n";
    std::string shielded_addr = wallet.shielded_address();

    std::cout << "  Unshielded: " << unshielded_addr << "\n";
    std::cout << "  Dust:       " << dust_addr << "\n";
    std::cout << "  Shielded:   " << shielded_addr << "\n";
    std::cout << "\n  NOTE: Private keys are NEVER saved to disk.\n";
    std::cout << "  Store your mnemonic securely.\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────

static std::string get_env_or(const char* key, const std::string& fallback) {
    const char* val = std::getenv(key);
    return val ? std::string(val) : fallback;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    bool do_register_dust = true;
    bool do_track = true;
    int watch_seconds = 5;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--register-dust") {
            do_register_dust = true;
        } else if (arg == "--no-register" || arg == "--no-dust") {
            do_register_dust = false;
        } else if (arg == "--dust-only") {
            // Skip WebSocket tracking and UTXO detail — go straight to DUST
            do_register_dust = true;
            do_track = false;
        } else if (arg == "--watch" && i + 1 < argc) {
            watch_seconds = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: midnight-wallet-tool [OPTIONS]\n";
            std::cout << "  --dust-only       Fast mode: sync + DUST only (skip tracking)\n";
            std::cout << "  --register-dust    Register for DUST generation (default)\n";
            std::cout << "  --no-register      Skip DUST registration (just track)\n";
            std::cout << "  --watch SECONDS    How long to watch WebSocket (default: 5)\n";
            std::cout << "  --help             Show this help\n";
            std::cout << "\nEnvironment: MIDNIGHT_MNEMONIC\n";
            return 0;
        }
    }

    // Get mnemonic from env
    std::string mnemonic = get_env_or("MIDNIGHT_MNEMONIC", "");
    if (mnemonic.empty()) {
        std::cerr << "ERROR: Mnemonic required. Set MIDNIGHT_MNEMONIC env var.\n";
        return 1;
    }

    std::cout << "=================================================\n";
    std::cout << "  Midnight SDK — Wallet Tool (WebSocket Edition)\n";
    std::cout << "=================================================\n";

    // ── Wallet setup ─────────────────────────────────────────────────────────
    const std::string indexer_url = "https://indexer.preprod.midnight.network/api/v4/graphql";

    midnight::wallet::WalletFacadeConfig cfg;
    cfg.indexer_http_url    = indexer_url;
    cfg.indexer_ws_url      = "wss://indexer.preprod.midnight.network/api/v4/graphql";
    cfg.relay_url          = "https://rpc.preprod.midnight.network";
    cfg.proving_server_url = "http://127.0.0.1:6300";
    cfg.network            = midnight::wallet::address::Network::PreProd;
    cfg.coin_selection     = midnight::wallet::CoinSelectionStrategy::LargestFirst;

    std::cout << "\n[1/6] Creating wallet from mnemonic...\n";
    auto wallet = midnight::wallet::WalletFacade::from_mnemonic_with_config(mnemonic, cfg);

    std::string unshielded_addr = wallet.unshielded_address();
    std::string dust_addr      = wallet.dust_address();
    std::string shielded_addr   = wallet.shielded_address();

    std::cout << "  Unshielded: " << unshielded_addr << "\n";
    std::cout << "  Dust:       " << dust_addr << "\n";
    std::cout << "  Shielded:   " << shielded_addr << "\n";

    // ── Sync state (HTTP) ────────────────────────────────────────────────────
    std::cout << "\n[2/6] Syncing wallet state (HTTP)...\n";
    wallet.sync();

    auto state = wallet.state();
    std::cout << "  Synced: " << (state.is_synced() ? "YES" : "NO") << "\n";
    std::cout << "  NIGHT:  " << state.night_balance() << "\n";
    std::cout << "  UTXOs:  " << state.unshielded.available_coins.size() << "\n";
    std::cout << "  Dust:   " << (state.dust.registered ? "REGISTERED" : "not registered") << "\n";

    uint64_t night_balance = state.night_balance();

    // ── Real-time WebSocket tracking ─────────────────────────────────────────
    if (do_track) {
        std::cout << "\n[3/6] Starting real-time WebSocket tracker...\n";
        track_realtime(wallet, unshielded_addr, watch_seconds);
    } else {
        std::cout << "\n[3/6] Skipping WebSocket tracker (--dust-only mode)\n";
    }

    // ── UTXO detail (HTTP) ──────────────────────────────────────────────────
    if (do_track) {
        std::cout << "\n[4/6] Querying UTXO detail (HTTP)...\n";
        midnight::network::IndexerClient indexer(indexer_url);
        show_utxos(indexer, unshielded_addr);

        // ── DUST status ─────────────────────────────────────────────────────────
        std::cout << "\n[5/6] Checking DUST registration status...\n";
        check_dust_status(indexer, dust_addr);
    } else {
        std::cout << "\n[4/6] Skipping UTXO detail (--dust-only mode)\n";
        std::cout << "\n[5/6] Skipping DUST status check (--dust-only mode)\n";
    }

    // ── Register DUST ───────────────────────────────────────────────────────
    std::cout << "\n[6/6] DUST Registration decision...\n";
    if (do_register_dust) {
        if (state.dust.registered) {
            std::cout << "  Already registered — skipping.\n";
        } else if (night_balance > 0) {
            std::cout << "  Balance OK (" << night_balance << " NIGHT) — proceeding.\n";
            register_dust(wallet, night_balance);
        } else {
            std::cout << "  No NIGHT balance — cannot register.\n";
            std::cout << "  Request NIGHT from faucet: https://faucet.preprod.midnight.network/\n";
        }
    } else {
        std::cout << "  Skipped (--no-register flag).\n";
    }

    // ── Final summary ────────────────────────────────────────────────────────
    show_wallet_summary(wallet, unshielded_addr, dust_addr);

    std::cout << "\n=================================================\n";
    return 0;
}
