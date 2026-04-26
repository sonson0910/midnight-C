#include "midnight/zk/ledger_state_sync.hpp"
#include "midnight/core/logger.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

namespace midnight::zk
{

    // ============ Block and State implementations ============

    // ============ Constructor ============

    LedgerStateSyncManager::LedgerStateSyncManager(
        const std::string &rpc_endpoint,
        std::chrono::milliseconds poll_interval)
        : rpc_endpoint_(rpc_endpoint),
          poll_interval_(poll_interval),
          cache_config_(StateCacheConfig()) {}

    LedgerStateSyncManager::LedgerStateSyncManager(
        const std::string &rpc_endpoint,
        std::chrono::milliseconds poll_interval,
        const StateCacheConfig &cache_config)
        : rpc_endpoint_(rpc_endpoint),
          poll_interval_(poll_interval),
          cache_config_(cache_config) {}

    // ============ Lifecycle control ============

    bool LedgerStateSyncManager::start_monitoring()
    {
        if (monitoring_active_)
        {
            return false; // Already monitoring
        }

        monitoring_active_ = true;
        local_block_height_ = 0;
        last_confirmed_height_ = 0;

        // In production, would spawn background thread here
        // For now, monitoring can be triggered manually via trigger_full_sync()
        g_logger->info(fmt::format("Started monitoring ledger at {}", rpc_endpoint_));

        return true;
    }

    void LedgerStateSyncManager::stop_monitoring()
    {
        monitoring_active_ = false;
        g_logger->info("Stopped monitoring ledger");
    }

    bool LedgerStateSyncManager::is_monitoring() const
    {
        return monitoring_active_;
    }

    // ============ State synchronization ============

    SyncResult LedgerStateSyncManager::sync_contract_state(const std::string &contract_address)
    {
        auto start_time = std::chrono::high_resolution_clock::now();
        SyncResult result;
        result.status = SyncResult::Status::SUCCESS;

        try
        {
            // Query on-chain state
            auto on_chain_state = query_on_chain_state(contract_address);
            result.on_chain_height = on_chain_state.block_height;

            // Check if we have cached version
            auto cached = get_cached_state(contract_address);
            if (cached && cached->block_height == on_chain_state.block_height)
            {
                result.status = SyncResult::Status::SUCCESS;
                result.local_height = local_block_height_;
            }
            else
            {
                // State changed, update cache
                update_cache(on_chain_state);
                result.updated_states.push_back(on_chain_state);
                result.local_height = local_block_height_;
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            result.sync_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      end_time - start_time)
                                      .count();

            sync_statistics_.total_syncs++;
            sync_statistics_.successful_syncs++;
        }
        catch (const std::exception &e)
        {
            result.status = SyncResult::Status::ERROR;
            result.error_message = e.what();
            sync_statistics_.total_syncs++;
            sync_statistics_.failed_syncs++;
        }

        return result;
    }

    std::vector<SyncResult> LedgerStateSyncManager::sync_all_contracts()
    {
        std::vector<SyncResult> results;

        for (const auto &contract_address : registered_contracts_)
        {
            results.push_back(sync_contract_state(contract_address));
        }

        return results;
    }

    bool LedgerStateSyncManager::trigger_full_sync()
    {
        try
        {
            auto on_chain_height = get_on_chain_block_height();

            // Process any new blocks
            for (uint64_t height = local_block_height_ + 1; height <= on_chain_height; ++height)
            {
                auto block = fetch_block(height);
                process_block(block);
                local_block_height_ = height;
            }

            last_confirmed_height_ = on_chain_height;
            return true;
        }
        catch (const std::exception &e)
        {
            g_logger->error(fmt::format("Sync failed: {}", e.what()));
            return false;
        }
    }

    uint64_t LedgerStateSyncManager::get_local_block_height() const
    {
        return local_block_height_;
    }

    uint64_t LedgerStateSyncManager::get_on_chain_block_height() const
    {
        // In production, this would query RPC endpoint
        // For now, return mock value
        return local_block_height_;
    }

    // ============ State queries ============

    std::optional<ContractState> LedgerStateSyncManager::get_cached_state(
        const std::string &contract_address) const
    {

        auto it = state_cache_.find(contract_address);
        if (it != state_cache_.end())
        {
            const auto &state = it->second;

            // Check if state is still fresh
            auto now = std::chrono::system_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - state.updated_at);

            if (age <= cache_config_.cache_ttl)
            {
                return state;
            }
        }

        return std::nullopt;
    }

    std::map<std::string, ContractState> LedgerStateSyncManager::get_all_cached_states() const
    {
        return state_cache_;
    }

    ContractState LedgerStateSyncManager::query_on_chain_state(const std::string &contract_address) const
    {
        // In production, this would make RPC call
        // For mock implementation, try cache first
        auto cached = get_cached_state(contract_address);
        if (cached)
        {
            return *cached;
        }

        // Return empty state if not cached
        ContractState state;
        state.contract_address = contract_address;
        state.block_height = 0;
        return state;
    }

    std::chrono::seconds LedgerStateSyncManager::get_cache_age(const std::string &contract_address) const
    {
        auto state = get_cached_state(contract_address);
        if (!state)
        {
            return std::chrono::seconds(-1); // Not cached
        }

        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - state->updated_at);
    }

    bool LedgerStateSyncManager::validate_state(const std::string &contract_address) const
    {
        auto cached = get_cached_state(contract_address);
        if (!cached)
        {
            return false; // No cached state
        }

        auto on_chain = query_on_chain_state(contract_address);

        // Compare states
        return (cached->public_state == on_chain.public_state) &&
               (cached->block_height == on_chain.block_height);
    }

    // ============ Event handling ============

    void LedgerStateSyncManager::register_block_event_listener(BlockEventCallback callback)
    {
        event_callback_ = callback;
    }

    std::optional<BlockEvent> LedgerStateSyncManager::get_last_block_event() const
    {
        if (event_history_.empty())
        {
            return std::nullopt;
        }
        return event_history_.back();
    }

    std::vector<BlockEvent> LedgerStateSyncManager::get_event_history(size_t limit) const
    {
        if (event_history_.size() <= limit)
        {
            return event_history_;
        }

        return std::vector<BlockEvent>(
            event_history_.end() - limit,
            event_history_.end());
    }

    // ============ Conflict resolution ============

    bool LedgerStateSyncManager::has_state_conflict(const std::string &contract_address) const
    {
        return !validate_state(contract_address);
    }

    ContractState LedgerStateSyncManager::resolve_state_conflict(const std::string &contract_address)
    {
        // Prefer on-chain state (authority)
        auto on_chain = query_on_chain_state(contract_address);
        update_cache(on_chain);
        return on_chain;
    }

    std::vector<ContractState> LedgerStateSyncManager::get_state_history(
        const std::string &contract_address,
        size_t limit) const
    {

        auto it = state_history_.find(contract_address);
        if (it == state_history_.end())
        {
            return {};
        }

        const auto &history = it->second;
        if (history.size() <= limit)
        {
            return history;
        }

        return std::vector<ContractState>(
            history.end() - limit,
            history.end());
    }

    // ============ Reorganization handling ============

    uint64_t LedgerStateSyncManager::detect_reorg()
    {
        return check_reorg();
    }

    bool LedgerStateSyncManager::handle_reorg(uint64_t reorg_depth)
    {
        if (reorg_depth == 0)
        {
            return true; // No reorg
        }

        reorg_in_progress_ = true;
        last_reorg_height_ = local_block_height_ - reorg_depth;

        // Invalidate cache entries affected by reorg
        std::vector<std::string> affected;
        for (auto &[addr, state] : state_cache_)
        {
            if (state.block_height > last_reorg_height_)
            {
                affected.push_back(addr);
                invalidate_cache_entry(addr);
            }
        }

        // Re-sync affected contracts
        for (const auto &addr : affected)
        {
            sync_contract_state(addr);
        }

        sync_statistics_.reorgs_detected++;
        sync_statistics_.max_reorg_depth = std::max(sync_statistics_.max_reorg_depth, reorg_depth);

        reorg_in_progress_ = false;
        return true;
    }

    bool LedgerStateSyncManager::is_reorg_in_progress() const
    {
        return reorg_in_progress_;
    }

    uint64_t LedgerStateSyncManager::get_last_reorg_height() const
    {
        return last_reorg_height_;
    }

    // ============ Cache management ============

    void LedgerStateSyncManager::clear_cache()
    {
        state_cache_.clear();
        state_history_.clear();
    }

    size_t LedgerStateSyncManager::get_cache_size() const
    {
        return state_cache_.size();
    }

    void LedgerStateSyncManager::invalidate_cache_entry(const std::string &contract_address)
    {
        state_cache_.erase(contract_address);
    }

    void LedgerStateSyncManager::set_cache_config(const StateCacheConfig &config)
    {
        cache_config_ = config;
    }

    StateCacheConfig LedgerStateSyncManager::get_cache_config() const
    {
        return cache_config_;
    }

    // ============ Monitoring and diagnostics ============

    SyncStatistics LedgerStateSyncManager::get_sync_statistics() const
    {
        return sync_statistics_;
    }

    void LedgerStateSyncManager::reset_sync_statistics()
    {
        sync_statistics_ = SyncStatistics();
    }

    bool LedgerStateSyncManager::is_healthy() const
    {
        return monitoring_active_ && !reorg_in_progress_;
    }

    json LedgerStateSyncManager::get_diagnostics_json() const
    {
        json diag;
        diag["monitoring_active"] = monitoring_active_;
        diag["local_block_height"] = local_block_height_;
        diag["last_confirmed_height"] = last_confirmed_height_;
        diag["cache_size"] = state_cache_.size();
        diag["registered_contracts"] = registered_contracts_.size();
        diag["reorg_in_progress"] = reorg_in_progress_;
        diag["last_reorg_height"] = last_reorg_height_;
        diag["is_healthy"] = is_healthy();

        // Add statistics
        auto stats = get_sync_statistics();
        diag["statistics"] = stats.as_json();

        return diag;
    }

    void LedgerStateSyncManager::register_contract(const std::string &contract_address)
    {
        registered_contracts_.insert(contract_address);
    }

    void LedgerStateSyncManager::unregister_contract(const std::string &contract_address)
    {
        registered_contracts_.erase(contract_address);
        invalidate_cache_entry(contract_address);
    }

    std::set<std::string> LedgerStateSyncManager::get_registered_contracts() const
    {
        return registered_contracts_;
    }

    void LedgerStateSyncManager::set_poll_interval(std::chrono::milliseconds poll_interval)
    {
        poll_interval_ = poll_interval;
    }

    // ============ Internal helper implementations ============

    void LedgerStateSyncManager::poll_blocks()
    {
        if (!monitoring_active_)
        {
            return;
        }

        trigger_full_sync();
    }

    void LedgerStateSyncManager::process_block(const Block &block)
    {
        auto state_changes = extract_state_changes(block);

        BlockEvent event;
        event.type = BlockEvent::EventType::BLOCK_ADDED;
        event.block = block;
        event.state_changes = state_changes;

        for (const auto &state : state_changes)
        {
            event.affected_contracts.push_back(state.contract_address);
            update_cache(state);
        }

        emit_block_event(event);
    }

    Block LedgerStateSyncManager::fetch_block(uint64_t height) const
    {
        // In production, would make RPC call
        Block block;
        block.height = height;
        block.timestamp = std::chrono::system_clock::now();
        return block;
    }

    std::vector<ContractState> LedgerStateSyncManager::extract_state_changes(const Block &block) const
    {
        std::vector<ContractState> changes;
        // In production, would parse block transactions
        return changes;
    }

    void LedgerStateSyncManager::update_cache(const ContractState &state)
    {
        // Add to history
        state_history_[state.contract_address].push_back(state);

        // Keep history size reasonable
        auto &history = state_history_[state.contract_address];
        if (history.size() > 1000)
        {
            history.erase(history.begin());
        }

        // Update cache
        if (state_cache_.size() >= cache_config_.max_cached_states)
        {
            // Evict oldest entry
            auto oldest = state_cache_.begin();
            for (auto it = state_cache_.begin(); it != state_cache_.end(); ++it)
            {
                if (it->second.updated_at < oldest->second.updated_at)
                {
                    oldest = it;
                }
            }
            state_cache_.erase(oldest);
        }

        state_cache_[state.contract_address] = state;
    }

    uint64_t LedgerStateSyncManager::check_reorg() const
    {
        // In production, would compare chain history
        return 0; // No reorg detected
    }

    void LedgerStateSyncManager::emit_block_event(const BlockEvent &event)
    {
        event_history_.push_back(event);

        // Keep history reasonable
        if (event_history_.size() > 10000)
        {
            event_history_.erase(event_history_.begin());
        }

        // Call user callback
        if (event_callback_)
        {
            event_callback_(event);
        }
    }

    // ============ SyncStatistics implementation ============

    json SyncStatistics::as_json() const
    {
        json j;
        j["total_syncs"] = total_syncs;
        j["successful_syncs"] = successful_syncs;
        j["failed_syncs"] = failed_syncs;
        j["success_rate"] = total_syncs > 0
                                ? (static_cast<double>(successful_syncs) / total_syncs)
                                : 0.0;
        j["reorgs_detected"] = reorgs_detected;
        j["max_reorg_depth"] = max_reorg_depth;
        j["states_synchronized"] = states_synchronized;
        j["average_sync_time_ms"] = average_sync_time_ms;
        return j;
    }

    // ============ Utility namespace implementations ============

    namespace ledger_util
    {

        std::shared_ptr<LedgerStateSyncManager> create_dev_sync_manager(const std::string &rpc_endpoint)
        {
            StateCacheConfig config;
            config.cache_ttl = std::chrono::seconds(600);            // 10 min
            config.enable_state_validation = false;                  // Less strict in dev
            config.stale_state_warning = std::chrono::seconds(1200); // 20 min

            return std::make_shared<LedgerStateSyncManager>(
                rpc_endpoint,
                std::chrono::milliseconds(5000), // 5 second poll
                config);
        }

        std::shared_ptr<LedgerStateSyncManager> create_production_sync_manager(const std::string &rpc_endpoint)
        {
            StateCacheConfig config;
            config.cache_ttl = std::chrono::seconds(300);           // 5 min
            config.enable_state_validation = true;                  // Strict validation
            config.stale_state_warning = std::chrono::seconds(600); // 10 min
            config.max_cached_states = 10000;                       // Large cache

            return std::make_shared<LedgerStateSyncManager>(
                rpc_endpoint,
                std::chrono::milliseconds(2000), // 2 second poll
                config);
        }

        std::shared_ptr<LedgerStateSyncManager> create_test_sync_manager(const std::string &rpc_endpoint)
        {
            StateCacheConfig config;
            config.cache_ttl = std::chrono::seconds(30); // 30 sec
            config.enable_state_validation = true;
            config.stale_state_warning = std::chrono::seconds(60); // 1 min
            config.max_cached_states = 100;                        // Small cache

            return std::make_shared<LedgerStateSyncManager>(
                rpc_endpoint,
                std::chrono::milliseconds(1000), // 1 second poll
                config);
        }

        bool is_state_stale(const ContractState &state, std::chrono::seconds max_age)
        {
            auto now = std::chrono::system_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - state.updated_at);
            return age > max_age;
        }

        json state_diff(const ContractState &state1, const ContractState &state2)
        {
            json diff;
            diff["height_changed"] = (state1.block_height != state2.block_height);
            diff["state_changed"] = (state1.public_state != state2.public_state);

            if (state1.public_state != state2.public_state)
            {
                diff["prev_state"] = state1.public_state;
                diff["new_state"] = state2.public_state;
            }

            diff["old_height"] = state1.block_height;
            diff["new_height"] = state2.block_height;

            return diff;
        }

        ContractState merge_states(const ContractState &state1, const ContractState &state2)
        {
            // Prefer state with higher block height (more recent)
            if (state1.block_height > state2.block_height)
            {
                return state1;
            }
            return state2;
        }

    } // namespace ledger_util

} // namespace midnight::zk
