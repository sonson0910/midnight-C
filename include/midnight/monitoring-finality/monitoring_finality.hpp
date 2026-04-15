/**
 * Phase 6: Monitoring & Finality (GRANDPA)
 * Handles block monitoring, state tracking, and GRANDPA finality verification
 * 
 * Key components:
 * - Block monitoring (6-second intervals, AURA)
 * - Reorg detection and recovery
 * - GRANDPA finality confirmation
 * - Transaction finality waiting
 * - State change tracking
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <functional>
#include <json/json.h>
#include <cstdint>

namespace midnight::phase6 {

/**
 * Block Status
 */
enum class BlockStatus {
    IMPORTED,           // Seen by node but not best
    BEST,              // Best (latest) block
    FINALIZED,         // GRANDPA finalized
    ABANDONED,         // Chain reorg, block abandoned
};

/**
 * Reorg Information
 */
struct ReorgEvent {
    uint32_t fork_height;               // Height where reorg happened
    std::string old_block_hash;         // Block before reorg
    std::string new_block_hash;         // Block after reorg
    uint32_t blocks_reorganized;        // How many blocks affected
    uint64_t timestamp;
};

/**
 * Transaction Status Update
 */
struct TransactionStatusUpdate {
    std::string transaction_hash;
    uint32_t block_height = 0;
    enum Status {
        IN_MEMPOOL,
        IN_BLOCK,
        FINALIZED_CONFIRMED,
        REORGANIZED_OUT,
    } status = Status::IN_MEMPOOL;
    
    uint64_t timestamp;
};

/**
 * State Change Event
 */
struct StateChangeEvent {
    std::string contract_address;
    std::string state_key;
    std::string old_value;
    std::string new_value;
    uint32_t block_height;
    uint64_t timestamp;
};

/**
 * Block Monitor
 * Monitors new blocks and chain reorganizations
 */
class BlockMonitor {
public:
    /**
     * Constructor
     * @param node_rpc_url: Node RPC endpoint
     * @param indexer_url: Indexer GraphQL endpoint
     */
    BlockMonitor(const std::string& node_rpc_url, const std::string& indexer_url);
    
    /**
     * Subscribe to new blocks
     * Callback called every 6 seconds for new block
     */
    void subscribe_new_blocks(std::function<void(const std::string&)> callback);
    
    /**
     * Get block history
     * @param start_height: Starting block height
     * @param end_height: Ending block height (0 = latest)
     */
    std::optional<std::vector<std::string>> get_block_history(
        uint32_t start_height, uint32_t end_height = 0);
    
    /**
     * Detect chain reorganization
     * Returns ReorgEvent if reorg detected
     */
    std::optional<ReorgEvent> detect_reorg();
    
    /**
     * Stop monitoring blocks
     */
    void stop_monitoring();
    
    /**
     * Get current best block
     */
    std::string get_best_block_hash();
    
    /**
     * Get best block height
     */
    uint32_t get_best_block_height();
    
private:
    std::string rpc_url_;
    std::string indexer_url_;
    bool monitoring_ = false;
    std::string last_best_block_;
    uint32_t last_best_height_ = 0;
};

/**
 * State Monitor
 * Monitors on-chain state changes
 */
class StateMonitor {
public:
    /**
     * Constructor
     * @param indexer_url: Indexer GraphQL endpoint
     */
    explicit StateMonitor(const std::string& indexer_url);
    
    /**
     * Subscribe to state changes
     */
    void subscribe_state_changes(
        std::function<void(const StateChangeEvent&)> callback);
    
    /**
     * Track balance of an address
     */
    void track_balance(const std::string& address,
                      std::function<void(uint64_t)> callback);
    
    /**
     * Track contract state
     */
    void track_contract_state(
        const std::string& contract_address,
        const std::string& state_key,
        std::function<void(const std::string&)> callback);
    
    /**
     * Stop monitoring
     */
    void stop_monitoring();
    
private:
    std::string indexer_url_;
    bool monitoring_ = false;
};

/**
 * Finalization Monitor
 * Monitors GRANDPA finality
 */
class FinalizationMonitor {
public:
    /**
     * Constructor
     * @param node_rpc_url: Node RPC endpoint
     */
    explicit FinalizationMonitor(const std::string& node_rpc_url);
    
    /**
     * Monitor finalization progress
     * Callback called when new block is finalized
     */
    void monitor_finalization(std::function<void(uint32_t)> callback);
    
    /**
     * Estimate time until finality
     * @param block_height: Block to check
     * @return Estimated seconds until finalized
     */
    uint32_t estimate_finality_time(uint32_t block_height);
    
    /**
     * Wait for block to be finalized (non-blocking)
     */
    void wait_for_finality(uint32_t block_height,
                          std::function<void(bool)> callback);
    
    /**
     * Get currently finalized block height
     */
    uint32_t get_finalized_block_height();
    
    /**
     * Stop monitoring
     */
    void stop_monitoring();
    
private:
    std::string rpc_url_;
    bool monitoring_ = false;
    uint32_t last_finalized_height_ = 0;
};

/**
 * Transaction Monitor
 * Monitors transaction status through mempool -> block -> finality
 */
class TransactionMonitor {
public:
    /**
     * Constructor
     * @param node_rpc_url: Node RPC endpoint
     * @param indexer_url: Indexer endpoint
     */
    TransactionMonitor(const std::string& node_rpc_url,
                      const std::string& indexer_url);
    
    /**
     * Monitor single transaction
     * Calls callback with status updates
     */
    void monitor_transaction(const std::string& tx_hash,
                            std::function<void(const TransactionStatusUpdate&)> callback);
    
    /**
     * Monitor multiple transactions
     */
    void monitor_batch(const std::vector<std::string>& tx_hashes,
                      std::function<void(const TransactionStatusUpdate&)> callback);
    
    /**
     * Get transaction inclusion block height
     * @return Block height or 0 if not included
     */
    uint32_t get_tx_confirmation_height(const std::string& tx_hash);
    
    /**
     * Check if transaction is finalized
     */
    bool is_transaction_finalized(const std::string& tx_hash);
    
    /**
     * Wait for transaction to be included
     * Non-blocking, calls callback when included
     */
    void wait_for_tx_inclusion(const std::string& tx_hash,
                              std::function<void(bool)> callback);
    
    /**
     * Stop monitoring
     */
    void stop_monitoring();
    
private:
    std::string rpc_url_;
    std::string indexer_url_;
    bool monitoring_ = false;
    std::map<std::string, TransactionStatusUpdate> tx_status_;
};

/**
 * Reorg Handler
 * Handles blockchain reorganizations
 */
class ReorgHandler {
public:
    /**
     * Detect if reorg has occurred
     * Compares block hashes at known heights
     */
    static std::optional<ReorgEvent> detect_reorg(const std::string& rpc_url);
    
    /**
     * Handle reorg recovery
     * Resubmits transactions if they were dropped
     */
    static bool handle_reorg_recovery(const ReorgEvent& reorg,
                                     const std::string& rpc_url);
    
    /**
     * Get blocks that were abandoned in reorg
     */
    static std::optional<std::vector<std::string>> get_abandoned_blocks(
        const ReorgEvent& reorg, const std::string& rpc_url);
    
    /**
     * Resubmit transaction after reorg
     */
    static bool resubmit_transaction(const std::string& tx_hash,
                                    const std::string& rpc_url);
};

/**
 * Finality Awaiter
 * Waits for transaction to reach GRANDPA finality
 */
class FinalityAwaiter {
public:
    /**
     * Constructor
     * @param node_rpc_url: Node RPC endpoint
     */
    explicit FinalityAwaiter(const std::string& node_rpc_url);
    
    /**
     * Wait for transaction finality (blocking)
     * @param tx_hash: Transaction to wait for
     * @param timeout_ms: Max wait time (0 = infinite)
     * @return true if finalized within timeout
     */
    bool wait_for_tx_finality(const std::string& tx_hash, uint64_t timeout_ms = 0);
    
    /**
     * Wait for block finality (blocking)
     * @param block_height: Block to wait for
     * @param timeout_ms: Max wait time
     * @return true if finalized
     */
    bool wait_for_block_finality(uint32_t block_height, uint64_t timeout_ms = 0);
    
    /**
     * Estimate blocks until finality
     * GRANDPA typically finalizes 2-3 blocks behind best
     * Depends on validator participation
     */
    uint32_t estimate_blocks_to_finality();
    
    /**
     * Get finality time estimate in seconds
     * Blocks to finality * 6 seconds per block
     */
    uint32_t estimate_finality_seconds();
    
    /**
     * Check if block is finalized
     */
    bool is_finalized(uint32_t block_height);
    
private:
    std::string rpc_url_;
};

/**
 * Chain Monitoring Statistics
 */
struct MonitoringStats {
    uint32_t total_blocks_monitored = 0;
    uint32_t total_transactions_monitored = 0;
    uint32_t reorg_count = 0;
    uint32_t max_reorg_depth = 0;
    uint64_t average_finality_time_ms = 0;
    uint32_t failed_inclusions = 0;
};

/**
 * Monitoring Manager
 * Coordinates all monitoring activities
 */
class MonitoringManager {
public:
    /**
     * Constructor
     */
    MonitoringManager(const std::string& node_rpc_url,
                     const std::string& indexer_url);
    
    /**
     * Start comprehensive monitoring
     */
    void start_monitoring();
    
    /**
     * Stop all monitoring
     */
    void stop_monitoring();
    
    /**
     * Get statistics
     */
    MonitoringStats get_statistics();
    
    /**
     * Register event handlers
     */
    void on_new_block(std::function<void(const std::string&)> callback);
    void on_reorg(std::function<void(const ReorgEvent&)> callback);
    void on_finality(std::function<void(uint32_t)> callback);
    
private:
    std::string rpc_url_;
    std::string indexer_url_;
    
    BlockMonitor block_monitor_;
    StateMonitor state_monitor_;
    FinalizationMonitor finality_monitor_;
    
    MonitoringStats stats_;
};

} // namespace midnight::phase6
