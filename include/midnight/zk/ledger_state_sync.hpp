#pragma once

#include "midnight/zk/private_state.hpp"
#include "midnight/zk/proof_server_resilient_client.hpp"
#include <cstdint>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include <optional>

namespace midnight::zk
{

    // Forward declarations
    struct BlockEvent;
    struct StateSnapshot;
    struct SyncStatistics;

    /**
     * @brief Represents a block on the Midnight blockchain
     */
    struct Block
    {
        uint64_t height;                                 ///< Block number
        std::string block_hash;                          ///< Block hash (hex)
        std::chrono::system_clock::time_point timestamp; ///< Block creation time
        std::vector<std::string> transaction_ids;        ///< Transactions in block
        std::string previous_block_hash;                 ///< Hash of parent block
        uint32_t confirmation_count = 0;                 ///< Number of confirmations
    };

    /**
     * @brief Smart contract state on the ledger
     */
    struct ContractState
    {
        std::string contract_address;                     ///< Contract identity
        json public_state;                                ///< Public state (on-chain)
        json private_state_commitment;                    ///< ZK commitment (revealed)
        uint64_t block_height;                            ///< Block where last updated
        std::string transaction_id;                       ///< Transaction that updated it
        std::chrono::system_clock::time_point updated_at; ///< When updated
        uint32_t confirmations = 0;                       ///< Confirmation count
    };

    /**
     * @brief Event emitted when a block is added to the chain
     */
    struct BlockEvent
    {
        enum class EventType
        {
            BLOCK_ADDED,
            BLOCK_REORG,
            STATE_CHANGED
        };

        EventType type;                              ///< Type of event
        Block block;                                 ///< Block information
        std::vector<ContractState> state_changes;    ///< States changed in block
        std::vector<std::string> affected_contracts; ///< Contract addresses affected
        uint64_t reorg_depth = 0;                    ///< Reorg depth (if reorg)
    };

    /**
     * @brief Callback for block events
     */
    using BlockEventCallback = std::function<void(const BlockEvent &)>;

    /**
     * @brief Cache configuration for local state
     */
    struct StateCacheConfig
    {
        bool enable_caching = true;                    ///< Enable local caching
        std::chrono::seconds cache_ttl{300};           ///< Cache time-to-live (5 min)
        size_t max_cached_states = 1000;               ///< Max states in cache
        std::chrono::seconds stale_state_warning{600}; ///< Warn if state older than this
        bool enable_state_validation = true;           ///< Validate cached vs. on-chain
    };

    /**
     * @brief Result of state synchronization
     */
    struct SyncResult
    {
        enum class Status
        {
            SUCCESS,
            PARTIAL,
            DESYNCED,
            REORG_DETECTED,
            ERROR
        };

        Status status;                             ///< Sync status
        uint64_t on_chain_height;                  ///< Latest on-chain block height
        uint64_t local_height;                     ///< Latest locally tracked height
        json state_diff;                           ///< Changes from last sync
        std::vector<ContractState> updated_states; ///< Updated contract states
        uint32_t sync_time_ms = 0;                 ///< How long synchronization took
        std::string error_message;                 ///< Error if status != SUCCESS
    };

    /**
     * @brief Statistics about sync performance
     */
    struct SyncStatistics
    {
        uint64_t total_syncs = 0;                             ///< Total sync operations
        uint64_t successful_syncs = 0;                        ///< Successful syncs
        uint64_t failed_syncs = 0;                            ///< Failed syncs
        uint64_t reorgs_detected = 0;                         ///< Reorg events detected
        uint64_t max_reorg_depth = 0;                         ///< Largest reorg experienced
        uint64_t states_synchronized = 0;                     ///< Total state updates
        double average_sync_time_ms = 0.0;                    ///< Average sync duration
        std::chrono::system_clock::time_point last_sync_time; ///< Last sync timestamp
        json as_json() const;                                 ///< Export as JSON
    };

    /**
     * @brief Manages synchronization between on-chain and off-chain state
     *
     * Responsibilities:
     * - Monitor blockchain for new blocks
     * - Track state changes in contracts
     * - Detect and handle reorganizations
     * - Cache local state for performance
     * - Validate state consistency
     * - Provide callbacks for state changes
     *
     * Integration Points:
     * - Works with PrivateStateManager for off-chain state
     * - Works with ProofServerResilientClient for blockchain queries
     * - Coordinates state updates from multiple sources
     * - Ensures consistency between proof server and blockchain
     */
    class LedgerStateSyncManager
    {
    public:
        /**
         * @brief Construct ledger sync manager
         * @param rpc_endpoint RPC endpoint URL (e.g., http://localhost:3030)
         * @param poll_interval How often to check for new blocks (milliseconds)
         */
        LedgerStateSyncManager(
            const std::string &rpc_endpoint,
            std::chrono::milliseconds poll_interval = std::chrono::milliseconds(5000));

        /**
         * @brief Construct with custom cache configuration
         * @param rpc_endpoint RPC endpoint URL
         * @param poll_interval Poll interval
         * @param cache_config Cache configuration
         */
        LedgerStateSyncManager(
            const std::string &rpc_endpoint,
            std::chrono::milliseconds poll_interval,
            const StateCacheConfig &cache_config);

        /**
         * @brief Virtual destructor for subclassing
         */
        virtual ~LedgerStateSyncManager() = default;

        // ============ Lifecycle control ============

        /**
         * @brief Start monitoring the blockchain
         * @return true if started successfully
         */
        bool start_monitoring();

        /**
         * @brief Stop monitoring the blockchain
         */
        void stop_monitoring();

        /**
         * @brief Check if currently monitoring
         * @return true if monitoring is active
         */
        bool is_monitoring() const;

        // ============ State synchronization ============

        /**
         * @brief Synchronize specific contract state with on-chain version
         * @param contract_address Address of contract to sync
         * @return Sync result with status and changes
         */
        SyncResult sync_contract_state(const std::string &contract_address);

        /**
         * @brief Synchronize all tracked contracts
         * @return Vector of sync results per contract
         */
        std::vector<SyncResult> sync_all_contracts();

        /**
         * @brief Manually trigger full block synchronization
         * @return true if sync successful
         */
        bool trigger_full_sync();

        /**
         * @brief Get current synchronized block height
         * @return Latest block height we've processed
         */
        uint64_t get_local_block_height() const;

        /**
         * @brief Get on-chain block height via RPC
         * @return Latest on-chain block height
         */
        uint64_t get_on_chain_block_height() const;

        // ============ State queries ============

        /**
         * @brief Get contract state from local cache
         * @param contract_address Address to query
         * @return Contract state if found in cache
         */
        std::optional<ContractState> get_cached_state(const std::string &contract_address) const;

        /**
         * @brief Get all cached contract states
         * @return Map of address → state
         */
        std::map<std::string, ContractState> get_all_cached_states() const;

        /**
         * @brief Query on-chain state directly (bypasses cache)
         * @param contract_address Address to query
         * @return Contract state from blockchain
         */
        ContractState query_on_chain_state(const std::string &contract_address) const;

        /**
         * @brief Check if local cache is stale
         * @param contract_address Address to check
         * @return Age in seconds since last update
         */
        std::chrono::seconds get_cache_age(const std::string &contract_address) const;

        /**
         * @brief Validate that cached state matches on-chain state
         * @param contract_address Address to validate
         * @return true if states match
         */
        bool validate_state(const std::string &contract_address) const;

        // ============ Event handling ============

        /**
         * @brief Register callback for block events
         * @param callback Function to call on block events
         */
        void register_block_event_listener(BlockEventCallback callback);

        /**
         * @brief Get last received block event
         * @return Last block event if any
         */
        std::optional<BlockEvent> get_last_block_event() const;

        /**
         * @brief Get event history (last N events)
         * @param limit Maximum events to return
         * @return Recent block events in chronological order
         */
        std::vector<BlockEvent> get_event_history(size_t limit = 100) const;

        // ============ Conflict resolution ============

        /**
         * @brief Detect if state conflict exists
         * @param contract_address Address to check
         * @return true if on-chain and cached state differ
         */
        bool has_state_conflict(const std::string &contract_address) const;

        /**
         * @brief Resolve state conflict (prefer on-chain state)
         * @param contract_address Address to resolve
         * @return Resolved state
         */
        ContractState resolve_state_conflict(const std::string &contract_address);

        /**
         * @brief Get state change history for contract
         * @param contract_address Address to query
         * @param limit Maximum historical entries
         * @return States in chronological order
         */
        std::vector<ContractState> get_state_history(
            const std::string &contract_address,
            size_t limit = 50) const;

        // ============ Reorganization handling ============

        /**
         * @brief Detect blockchain reorganization
         * @return Reorg depth (0 if no reorg)
         */
        uint64_t detect_reorg();

        /**
         * @brief Handle blockchain reorganization
         * @param reorg_depth Depth of reorganization
         * @return true if reorg handled successfully
         */
        bool handle_reorg(uint64_t reorg_depth);

        /**
         * @brief Check if reorg is currently in progress
         * @return true if reorg being handled
         */
        bool is_reorg_in_progress() const;

        /**
         * @brief Get last reorg information
         * @return Block height where last reorg occurred
         */
        uint64_t get_last_reorg_height() const;

        // ============ Cache management ============

        /**
         * @brief Clear state cache
         */
        void clear_cache();

        /**
         * @brief Get cache size (number of cached states)
         * @return Number of states currently cached
         */
        size_t get_cache_size() const;

        /**
         * @brief Invalidate cache entry
         * @param contract_address Address to invalidate
         */
        void invalidate_cache_entry(const std::string &contract_address);

        /**
         * @brief Update cache configuration at runtime
         * @param config New cache configuration
         */
        void set_cache_config(const StateCacheConfig &config);

        /**
         * @brief Get current cache configuration
         * @return Cache configuration
         */
        StateCacheConfig get_cache_config() const;

        // ============ Monitoring and diagnostics ============

        /**
         * @brief Get synchronization statistics
         * @return Detailed sync statistics
         */
        SyncStatistics get_sync_statistics() const;

        /**
         * @brief Reset synchronization statistics
         */
        void reset_sync_statistics();

        /**
         * @brief Check health of sync manager
         * @return true if operational and synchronized
         */
        bool is_healthy() const;

        /**
         * @brief Get diagnostic information as JSON
         * @return Diagnostic data
         */
        json get_diagnostics_json() const;

        /**
         * @brief Register contract for monitoring
         * @param contract_address Contract to monitor
         */
        void register_contract(const std::string &contract_address);

        /**
         * @brief Unregister contract from monitoring
         * @param contract_address Contract to stop monitoring
         */
        void unregister_contract(const std::string &contract_address);

        /**
         * @brief Get list of registered contracts
         * @return Contract addresses being monitored
         */
        std::set<std::string> get_registered_contracts() const;

        /**
         * @brief Update sync configuration
         * @param poll_interval New poll interval
         */
        void set_poll_interval(std::chrono::milliseconds poll_interval);

    private:
        // ============ Internal state ============

        std::string rpc_endpoint_;                ///< RPC endpoint URL
        std::chrono::milliseconds poll_interval_; ///< How often to poll
        StateCacheConfig cache_config_;           ///< Cache configuration

        bool monitoring_active_ = false;     ///< Is monitoring running
        uint64_t local_block_height_ = 0;    ///< Latest block we processed
        uint64_t last_confirmed_height_ = 0; ///< Last confirmed block

        // State tracking
        std::map<std::string, ContractState> state_cache_;                ///< Cached states
        std::map<std::string, std::vector<ContractState>> state_history_; ///< State history

        // Event tracking
        std::vector<BlockEvent> event_history_; ///< Recent events
        BlockEventCallback event_callback_;     ///< User callback

        // Reorg handling
        bool reorg_in_progress_ = false;  ///< Reorg currently happening
        uint64_t last_reorg_height_ = 0;  ///< Where last reorg occurred
        uint64_t last_stable_height_ = 0; ///< Last confirmed stable height

        // Statistics
        SyncStatistics sync_statistics_; ///< Performance statistics

        // Contract tracking
        std::set<std::string> registered_contracts_; ///< Contracts to monitor

        // ============ Internal helper methods ============

        /**
         * @brief Poll for new blocks (runs in background thread conceptually)
         */
        void poll_blocks();

        /**
         * @brief Process a newly discovered block
         * @param block Block to process
         */
        void process_block(const Block &block);

        /**
         * @brief Fetch block by height from RPC
         * @param height Block height
         * @return Block information
         */
        Block fetch_block(uint64_t height) const;

        /**
         * @brief Extract state changes from block
         * @param block Block to analyze
         * @return State changes in block
         */
        std::vector<ContractState> extract_state_changes(const Block &block) const;

        /**
         * @brief Update local cache with new state
         * @param state New state to cache
         */
        void update_cache(const ContractState &state);

        /**
         * @brief Check for blockchain reorganization
         * @return Reorg depth or 0 if no reorg
         */
        uint64_t check_reorg() const;

        /**
         * @brief Emit block event to registered listeners
         * @param event Event to emit
         */
        void emit_block_event(const BlockEvent &event);
    };

    // ============ Utility namespace for ledger helpers ============

    namespace ledger_util
    {
        /**
         * @brief Create default sync manager for development
         * @return Configured sync manager
         */
        std::shared_ptr<LedgerStateSyncManager> create_dev_sync_manager(
            const std::string &rpc_endpoint = "http://localhost:3030");

        /**
         * @brief Create default sync manager for production
         * @return Configured sync manager
         */
        std::shared_ptr<LedgerStateSyncManager> create_production_sync_manager(
            const std::string &rpc_endpoint);

        /**
         * @brief Create default sync manager for testing
         * @return Configured sync manager
         */
        std::shared_ptr<LedgerStateSyncManager> create_test_sync_manager(
            const std::string &rpc_endpoint = "http://localhost:3030");

        /**
         * @brief Check if state is likely stale
         * @param state State to check
         * @param max_age Maximum acceptable age in seconds
         * @return true if state might be stale
         */
        bool is_state_stale(const ContractState &state, std::chrono::seconds max_age);

        /**
         * @brief Compare two states for differences
         * @param state1 First state
         * @param state2 Second state
         * @return JSON diff showing differences
         */
        json state_diff(const ContractState &state1, const ContractState &state2);

        /**
         * @brief Merge two conflicting states (prefer newer)
         * @param state1 First state
         * @param state2 Second state
         * @return Merged state
         */
        ContractState merge_states(const ContractState &state1, const ContractState &state2);
    }

} // namespace midnight::zk
