/**
 * Phase 1: Midnight Node Connection
 * Official sr25519 + AURA + GRANDPA consensus
 * 
 * This module handles:
 * - Connection to Midnight Node RPC (wss://)
 * - AURA block production tracking (6-second blocks)
 * - GRANDPA finality verification
 * - blake2_256 hash validation
 */

#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <json/json.h>

namespace midnight::phase1 {

/**
 * Official Block Structure
 * From Midnight Ledger (Polkadot SDK based)
 */
struct Block {
    uint64_t number;                           // Block height
    std::string hash;                          // blake2_256 hash (64 hex chars)
    std::string parent_hash;                   // Parent block hash
    uint64_t timestamp;                        // Unix timestamp
    std::string author;                        // sr25519 block producer address
    std::vector<std::string> transaction_ids;  // Tx hashes in this block
    uint64_t extrinsics_count;                 // Extrinsics in block
    
    // GRANDPA finality data
    bool is_finalized = false;                 // Has GRANDPA finality? 
    uint32_t finality_depth = 0;              // How many descendants finalized
};

/**
 * Transaction on Midnight (UTXO-based)
 */
struct Transaction {
    std::string hash;              // blake2_256 transaction hash
    uint32_t block_height;         // Which block contains this TX
    uint32_t index_in_block;       // Position in block
    uint64_t timestamp;            // When included
    
    // UTXO inputs
    std::vector<std::string> input_addresses;
    
    // UTXO outputs  
    std::vector<std::string> output_addresses;
    
    // Privacy: commitments instead of amounts
    std::vector<std::string> commitments;     // ZK commitments for amounts
    std::vector<std::string> proofs;          // zk-SNARKs proving commitments
};

/**
 * Network Status
 */
struct NetworkStatus {
    uint64_t best_block_number;                // Latest block height
    std::string best_block_hash;               // Latest block hash
    uint64_t finalized_block_number;          // GRANDPA finalized height
    std::string finalized_block_hash;         // GRANDPA finalized hash
    uint32_t peers_count;                     // Connected peer count
    bool is_syncing = false;                   // Currently syncing?
    double sync_progress = 0.0;               // 0.0 to 1.0
};

/**
 * Connection Result
 */
enum class ConnectionStatus {
    SUCCESS = 0,
    INVALID_ENDPOINT = 1,
    NETWORK_ERROR = 2,
    TLS_ERROR = 3,
    RPC_ERROR = 4,
    UNKNOWN_ERROR = 5,
};

/**
 * Phase 1: Node Connection Manager
 * 
 * Handles:
 * - WebSocket connection to Midnight Node
 * - AURA consensus tracking (6-second blocks)
 * - GRANDPA finality monitoring
 * - Block and transaction queries
 */
class NodeConnection {
public:
    /**
     * Constructor
     * @param rpc_endpoint: WebSocket RPC endpoint (e.g., "wss://rpc.preprod.midnight.network")
     */
    explicit NodeConnection(const std::string& rpc_endpoint);
    
    ~NodeConnection();
    
    /**
     * Connect to Midnight Node
     * @return ConnectionStatus indicating success/failure
     */
    ConnectionStatus connect();
    
    /**
     * Disconnect from node
     */
    void disconnect();
    
    /**
     * Check if connected
     */
    bool is_connected() const;
    
    /**
     * Get current network status
     * @return NetworkStatus with chain info
     */
    std::optional<NetworkStatus> get_network_status();
    
    /**
     * Get block by number
     * @param block_number: Block height
     * @return Block data or empty optional if not found
     */
    std::optional<Block> get_block(uint64_t block_number);
    
    /**
     * Get latest block
     */
    std::optional<Block> get_latest_block();
    
    /**
     * Get block by hash
     */
    std::optional<Block> get_block_by_hash(const std::string& block_hash);
    
    /**
     * Get transaction
     */
    std::optional<Transaction> get_transaction(const std::string& tx_hash);
    
    /**
     * Get GRANDPA finalized block height
     * This is the last block with finality guarantee
     */
    uint64_t get_finalized_block_height();
    
    /**
     * Verify if block is finalized
     * @param block_height: Height to check
     * @return true if block has GRANDPA finality
     */
    bool is_block_finalized(uint64_t block_height);
    
    /**
     * Monitor new blocks
     * Calls callback for each new block (6-second interval)
     * @param callback: Function to call with new Block
     */
    void monitor_blocks(std::function<void(const Block&)> callback);
    
    /**
     * Stop monitoring blocks
     */
    void stop_monitoring();
    
    /**
     * Get SR25519 key for block authorship validation
     */
    std::string get_aura_authority_key();
    
    /**
     * Verify block signature (sr25519)
     * @param block: Block to verify
     * @param signature: Author's sr25519 signature
     * @return true if signature valid
     */
    bool verify_block_signature(const Block& block, const std::string& signature);
    
    /**
     * Get consensus mechanism info
     * Returns: "AURA + GRANDPA" 
     */
    std::string get_consensus_mechanism();
    
    /**
     * Get block time (should be 6 seconds)
     */
    uint32_t get_block_time_seconds();
    
    /**
     * RPC call (low-level)
     * @param method: JSON-RPC method name
     * @param params: JSON parameters
     * @return JSON response
     */
    Json::Value rpc_call(const std::string& method, const Json::Value& params);
    
private:
    std::string rpc_endpoint_;
    bool connected_ = false;
    std::string connection_id_;
    
    // WebSocket connection details
    void* ws_handle_ = nullptr;
    
    // Monitoring state
    bool monitoring_ = false;
    std::function<void(const Block&)> block_callback_;
};

/**
 * Consensus Verifier
 * Verifies AURA + GRANDPA requirements
 */
class ConsensusVerifier {
public:
    /**
     * Verify AURA block authorship (sr25519 signature)
     */
    static bool verify_aura_block(const Block& block);
    
    /**
     * Verify GRANDPA finality
     * A block is finalized when validators reach consensus
     */
    static bool verify_grandpa_finality(const Block& block, uint32_t minimum_depth);
    
    /**
     * Verify blake2_256 hash
     */
    static bool verify_hash(const std::string& data, const std::string& expected_hash);
};

} // namespace midnight::phase1
