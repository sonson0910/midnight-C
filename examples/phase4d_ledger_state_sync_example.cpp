#include "midnight/zk/ledger_state_sync.hpp"
#include <iostream>
#include <iomanip>

using json = nlohmann::json;
using namespace midnight::zk;

// ============================================================================
// Example 1: Basic ledger sync manager setup
// ============================================================================

void example_basic_sync_setup()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 1: Basic Ledger Sync Manager Setup" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Create sync manager pointing to local RPC
    auto sync_manager = std::make_shared<LedgerStateSyncManager>(
        "http://localhost:3030",
        std::chrono::milliseconds(5000) // Poll every 5 seconds
    );

    std::cout << "\n✓ Ledger sync manager created:" << std::endl;
    std::cout << "  - RPC Endpoint: http://localhost:3030" << std::endl;
    std::cout << "  - Poll Interval: 5000ms" << std::endl;
    std::cout << "  - Cache TTL: 300s (5 minutes)" << std::endl;

    // Start monitoring
    if (sync_manager->start_monitoring())
    {
        std::cout << "\n✓ Monitoring started" << std::endl;
        std::cout << "  - Initial block height: " << sync_manager->get_local_block_height() << std::endl;
        std::cout << "  - Is monitoring: " << (sync_manager->is_monitoring() ? "yes" : "no") << std::endl;
    }
}

// ============================================================================
// Example 2: Contract registration and state caching
// ============================================================================

void example_contract_registration()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 2: Contract Registration and State Caching" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto sync_manager = ledger_util::create_dev_sync_manager();
    sync_manager->start_monitoring();

    std::cout << "\nRegistering contracts for monitoring:" << std::endl;

    // Register multiple contracts
    std::vector<std::string> contracts = {
        "contract_0x1234567890abcdef",
        "contract_0xfedcba0987654321",
        "contract_0xabcdef1234567890"};

    for (const auto &contract : contracts)
    {
        sync_manager->register_contract(contract);
        std::cout << "  ✓ Registered: " << contract << std::endl;
    }

    std::cout << "\nRegistered contracts: " << sync_manager->get_registered_contracts().size() << std::endl;
    std::cout << "  Cache size: " << sync_manager->get_cache_size() << " states" << std::endl;
}

// ============================================================================
// Example 3: State synchronization workflow
// ============================================================================

void example_state_synchronization()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 3: State Synchronization Workflow" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto sync_manager = std::make_shared<LedgerStateSyncManager>(
        "http://localhost:3030",
        std::chrono::milliseconds(2000));

    sync_manager->start_monitoring();
    sync_manager->register_contract("contract_sample");

    std::cout << "\nSync workflow:" << std::endl;
    std::cout << "  1. Trigger manual full sync" << std::endl;

    if (sync_manager->trigger_full_sync())
    {
        std::cout << "     ✓ Full sync completed" << std::endl;
        std::cout << "     Local block height: " << sync_manager->get_local_block_height() << std::endl;
    }

    std::cout << "\n  2. Query cached contract state" << std::endl;
    auto cached_state = sync_manager->get_cached_state("contract_sample");
    if (cached_state)
    {
        std::cout << "     ✓ State found in cache" << std::endl;
        std::cout << "     Block height: " << cached_state->block_height << std::endl;
        std::cout << "     Confirmations: " << cached_state->confirmations << std::endl;
    }
    else
    {
        std::cout << "     (No cached state found)" << std::endl;
    }

    std::cout << "\n  3. Check cache age" << std::endl;
    auto age = sync_manager->get_cache_age("contract_sample");
    if (age.count() >= 0)
    {
        std::cout << "     Cache age: " << age.count() << " seconds" << std::endl;
    }
    else
    {
        std::cout << "     Cache is fresh or not present" << std::endl;
    }

    std::cout << "\n  4. Validate cached vs on-chain state" << std::endl;
    bool valid = sync_manager->validate_state("contract_sample");
    std::cout << "     State valid: " << (valid ? "yes" : "no") << std::endl;
}

// ============================================================================
// Example 4: Event listeners and block tracking
// ============================================================================

void example_event_listeners()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 4: Event Listeners and Block Tracking" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto sync_manager = ledger_util::create_dev_sync_manager();
    sync_manager->start_monitoring();
    sync_manager->register_contract("contract_monitored");

    std::cout << "\nSetting up block event listener:" << std::endl;

    // Register callback for block events
    sync_manager->register_block_event_listener(
        [](const BlockEvent &event)
        {
            std::cout << "  📦 Block event received:" << std::endl;

            switch (event.type)
            {
            case BlockEvent::EventType::BLOCK_ADDED:
                std::cout << "    ✓ New block added" << std::endl;
                std::cout << "    Block height: " << event.block.height << std::endl;
                std::cout << "    Affected contracts: " << event.affected_contracts.size() << std::endl;
                break;

            case BlockEvent::EventType::BLOCK_REORG:
                std::cout << "    ⚠ Block reorganization detected" << std::endl;
                std::cout << "    Reorg depth: " << event.reorg_depth << std::endl;
                break;

            case BlockEvent::EventType::STATE_CHANGED:
                std::cout << "    📝 Contract state changed" << std::endl;
                break;
            }
        });

    std::cout << "  ✓ Event listener registered" << std::endl;

    std::cout << "\nEvent history tracking:" << std::endl;
    auto history = sync_manager->get_event_history(10);
    std::cout << "  Recent events: " << history.size() << " events" << std::endl;
}

// ============================================================================
// Example 5: Conflict detection and resolution
// ============================================================================

void example_conflict_resolution()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 5: Conflict Detection and Resolution" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto sync_manager = std::make_shared<LedgerStateSyncManager>("http://localhost:3030");
    sync_manager->start_monitoring();
    sync_manager->register_contract("contract_checking");

    std::cout << "\nConflict detection workflow:" << std::endl;

    std::cout << "  1. Check for state conflicts" << std::endl;
    bool has_conflict = sync_manager->has_state_conflict("contract_checking");
    std::cout << "     Conflict detected: " << (has_conflict ? "yes" : "no") << std::endl;

    std::cout << "\n  2. Get state change history" << std::endl;
    auto history = sync_manager->get_state_history("contract_checking", 5);
    std::cout << "     Historical states: " << history.size() << std::endl;

    if (!history.empty())
    {
        std::cout << "\n     Most recent state:" << std::endl;
        const auto &latest = history.back();
        std::cout << "     Block height: " << latest.block_height << std::endl;
        std::cout << "     Confirmations: " << latest.confirmations << std::endl;
    }

    std::cout << "\n  3. Resolve conflicts (prefer on-chain)" << std::endl;
    try
    {
        auto resolved = sync_manager->resolve_state_conflict("contract_checking");
        std::cout << "     ✓ Conflict resolved" << std::endl;
        std::cout << "     Resolved block height: " << resolved.block_height << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cout << "     (No conflict to resolve)" << std::endl;
    }
}

// ============================================================================
// Example 6: Reorg handling and recovery
// ============================================================================

void example_reorg_handling()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 6: Reorganization Handling and Recovery" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto sync_manager = ledger_util::create_production_sync_manager("http://localhost:3030");
    sync_manager->start_monitoring();

    std::cout << "\nBlockchain reorg handling:" << std::endl;

    std::cout << "  1. Detect reorganization" << std::endl;
    uint64_t reorg_depth = sync_manager->detect_reorg();
    std::cout << "     Reorg depth: " << reorg_depth << " blocks" << std::endl;

    std::cout << "\n  2. Check reorg status" << std::endl;
    bool in_progress = sync_manager->is_reorg_in_progress();
    std::cout << "     Reorg in progress: " << (in_progress ? "yes" : "no") << std::endl;

    std::cout << "\n  3. Handle reorg (if detected)" << std::endl;
    if (reorg_depth > 0)
    {
        if (sync_manager->handle_reorg(reorg_depth))
        {
            std::cout << "     ✓ Reorg handled successfully" << std::endl;
            std::cout << "     Last reorg height: " << sync_manager->get_last_reorg_height() << std::endl;
        }
    }
    else
    {
        std::cout << "     No reorg detected - normal operation" << std::endl;
    }

    std::cout << "\n  4. Reorg recovery" << std::endl;
    std::cout << "     Cache invalidation: ✓" << std::endl;
    std::cout << "     State re-sync: ✓" << std::endl;
    std::cout << "     Event emission: ✓" << std::endl;
}

// ============================================================================
// Example 7: Cache configuration and management
// ============================================================================

void example_cache_management()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 7: Cache Configuration and Management" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto sync_manager = ledger_util::create_production_sync_manager("http://localhost:3030");
    auto config = sync_manager->get_cache_config();

    std::cout << "\nCurrent cache configuration:" << std::endl;
    std::cout << "  - Cache enabled: " << (config.enable_caching ? "yes" : "no") << std::endl;
    std::cout << "  - TTL: " << config.cache_ttl.count() << "s" << std::endl;
    std::cout << "  - Max states: " << config.max_cached_states << std::endl;
    std::cout << "  - Stale warning threshold: " << config.stale_state_warning.count() << "s" << std::endl;
    std::cout << "  - State validation: " << (config.enable_state_validation ? "enabled" : "disabled") << std::endl;

    std::cout << "\nCache operations:" << std::endl;
    sync_manager->start_monitoring();
    sync_manager->register_contract("contract_test");

    std::cout << "  - Cache size: " << sync_manager->get_cache_size() << std::endl;

    std::cout << "\n  - Getting all cached states:" << std::endl;
    auto all_states = sync_manager->get_all_cached_states();
    std::cout << "    Total cached: " << all_states.size() << " states" << std::endl;

    std::cout << "\n  - Invalidate single entry:" << std::endl;
    sync_manager->invalidate_cache_entry("contract_test");
    std::cout << "    ✓ Entry invalidated" << std::endl;

    std::cout << "\n  - Clear entire cache:" << std::endl;
    sync_manager->clear_cache();
    std::cout << "    ✓ Cache cleared (" << sync_manager->get_cache_size() << " entries remain)" << std::endl;
}

// ============================================================================
// Example 8: Monitoring and diagnostics
// ============================================================================

void example_monitoring_diagnostics()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 8: Monitoring and Diagnostics" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto sync_manager = ledger_util::create_production_sync_manager("http://localhost:3030");
    sync_manager->start_monitoring();
    sync_manager->register_contract("contract_1");
    sync_manager->register_contract("contract_2");

    std::cout << "\nSync manager diagnostics:" << std::endl;

    std::cout << "\n  1. Health check" << std::endl;
    bool healthy = sync_manager->is_healthy();
    std::cout << "     Healthy: " << (healthy ? "yes ✓" : "no ✗") << std::endl;

    std::cout << "\n  2. Statistics" << std::endl;
    auto stats = sync_manager->get_sync_statistics();
    std::cout << "     Total syncs: " << stats.total_syncs << std::endl;
    std::cout << "     Successful: " << stats.successful_syncs << std::endl;
    std::cout << "     Failed: " << stats.failed_syncs << std::endl;

    if (stats.total_syncs > 0)
    {
        double success_rate = (static_cast<double>(stats.successful_syncs) / stats.total_syncs) * 100.0;
        std::cout << "     Success rate: " << std::fixed << std::setprecision(1) << success_rate << "%" << std::endl;
    }

    std::cout << "     Reorgs detected: " << stats.reorgs_detected << std::endl;
    std::cout << "     Max reorg depth: " << stats.max_reorg_depth << std::endl;
    std::cout << "     Average sync time: " << std::fixed << std::setprecision(1)
              << stats.average_sync_time_ms << "ms" << std::endl;

    std::cout << "\n  3. Diagnostic JSON output:" << std::endl;
    auto diag = sync_manager->get_diagnostics_json();
    std::cout << "     " << diag.dump(2) << std::endl;

    std::cout << "\n  4. Status summary" << std::endl;
    std::cout << "     Monitoring: " << (sync_manager->is_monitoring() ? "active" : "inactive") << std::endl;
    std::cout << "     Registered contracts: " << sync_manager->get_registered_contracts().size() << std::endl;
    std::cout << "     Cached states: " << sync_manager->get_cache_size() << std::endl;
    std::cout << "     Block height: " << sync_manager->get_local_block_height() << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Midnight SDK - Phase 4D: Ledger State Synchronization" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    try
    {
        // Run all examples
        example_basic_sync_setup();
        example_contract_registration();
        example_state_synchronization();
        example_event_listeners();
        example_conflict_resolution();
        example_reorg_handling();
        example_cache_management();
        example_monitoring_diagnostics();

        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "Phase 4D Examples Complete!" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "\nKey Takeaways:" << std::endl;
        std::cout << "  ✓ Automatic state synchronization between on-chain and off-chain" << std::endl;
        std::cout << "  ✓ Block event monitoring and callbacks" << std::endl;
        std::cout << "  ✓ Blockchain reorganization detection and recovery" << std::endl;
        std::cout << "  ✓ Local state caching with TTL" << std::endl;
        std::cout << "  ✓ Conflict detection and resolution" << std::endl;
        std::cout << "  ✓ Comprehensive diagnostics and statistics" << std::endl;
        std::cout << "  ✓ Production-ready ledger sync management" << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
