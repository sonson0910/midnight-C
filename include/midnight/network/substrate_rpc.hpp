#pragma once

#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/codec/scale_codec.hpp"
#include "midnight/tx/extrinsic_builder.hpp"
#include "midnight/wallet/hd_wallet.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>

namespace midnight::network
{

    using json = nlohmann::json;

    /**
     * @brief Runtime version information from the Substrate node
     */
    struct RuntimeVersion
    {
        std::string spec_name;
        std::string impl_name;
        uint32_t spec_version = 0;
        uint32_t impl_version = 0;
        uint32_t tx_version = 0;
        uint32_t authoring_version = 0;
    };

    /**
     * @brief Account info from on-chain storage
     */
    struct AccountInfo
    {
        uint32_t nonce = 0;        ///< Number of transactions sent
        uint32_t consumers = 0;
        uint32_t providers = 0;
        uint32_t sufficients = 0;
        uint64_t free = 0;         ///< Free balance
        uint64_t reserved = 0;     ///< Reserved balance
        uint64_t frozen = 0;       ///< Frozen balance
    };

    /**
     * @brief Transaction submission result
     */
    struct SubmitResult
    {
        bool success = false;
        std::string tx_hash;       ///< Transaction hash (0x...)
        std::string error;         ///< Error message if failed
    };

    /**
     * @brief Extended Substrate RPC client for Midnight
     *
     * Builds on MidnightNodeRPC with native Substrate methods:
     *   - state_getMetadata
     *   - state_getRuntimeVersion
     *   - state_getStorage
     *   - system_accountNextIndex
     *   - author_submitExtrinsic
     *   - chain_getBlockHash
     *   - chain_getFinalizedHead
     *
     * Integrates with ExtrinsicBuilder + HDWallet for complete native
     * transaction construction and submission pipeline.
     *
     * Example:
     * ```cpp
     * SubstrateRPC rpc("https://rpc.preview.midnight.network");
     * auto version = rpc.get_runtime_version();
     * auto nonce = rpc.get_account_nonce(public_key_hex);
     * auto result = rpc.submit_extrinsic(signed_bytes);
     * ```
     */
    class SubstrateRPC
    {
    public:
        /**
         * @brief Construct Substrate RPC client
         * @param node_url URL of the Midnight node RPC endpoint
         * @param timeout_ms Request timeout in milliseconds
         */
        explicit SubstrateRPC(const std::string &node_url,
                              uint32_t timeout_ms = 10000);

        // ─── Runtime Info ─────────────────────────────────────

        /**
         * @brief Get runtime version (spec_version, tx_version, etc.)
         */
        RuntimeVersion get_runtime_version();

        /**
         * @brief Get runtime metadata (raw SCALE-encoded hex)
         * @return Hex-encoded metadata blob
         */
        std::string get_metadata_hex();

        /**
         * @brief Get genesis block hash
         * @return 32-byte genesis hash as hex string
         */
        std::string get_genesis_hash();

        /**
         * @brief Get finalized head block hash
         * @return Block hash as hex string
         */
        std::string get_finalized_head();

        /**
         * @brief Get block hash at specific height
         * @param block_number Block number (0 = genesis)
         * @return Block hash as hex string
         */
        std::string get_block_hash(uint64_t block_number);

        /**
         * @brief Get latest block header
         * @return JSON with number, parentHash, stateRoot, extrinsicsRoot
         */
        json get_header(const std::string &block_hash = "");

        // ─── Account State ────────────────────────────────────

        /**
         * @brief Get account nonce (system_accountNextIndex)
         * @param account_id_hex Hex-encoded 32-byte AccountId
         * @return Next nonce for transaction signing
         */
        uint32_t get_account_nonce(const std::string &account_id_hex);

        /**
         * @brief Get full account info from storage
         * @param account_id_hex Hex-encoded 32-byte AccountId
         * @return AccountInfo with nonce, balances, etc.
         */
        AccountInfo get_account_info(const std::string &account_id_hex);

        /**
         * @brief Get free balance
         * @param account_id_hex Hex-encoded 32-byte AccountId
         * @return Free balance in base units
         */
        uint64_t get_free_balance(const std::string &account_id_hex);

        /**
         * @brief Query raw storage by key
         * @param storage_key Hex-encoded storage key
         * @return Hex-encoded storage value, or empty if not found
         */
        std::string get_storage(const std::string &storage_key);

        // ─── Transaction Submission ───────────────────────────

        /**
         * @brief Submit a SCALE-encoded signed extrinsic
         * @param extrinsic_hex Hex-encoded extrinsic bytes (0x...)
         * @return SubmitResult with tx_hash or error
         */
        SubmitResult submit_extrinsic(const std::string &extrinsic_hex);

        /**
         * @brief Build, sign, and submit an extrinsic in one step
         *
         * Auto-fetches: runtime version, genesis hash, account nonce.
         * Signs with the provided key pair.
         *
         * @param call PalletCall to execute
         * @param key KeyPair for signing (Ed25519)
         * @param tip Optional tip amount
         * @return SubmitResult with tx_hash or error
         */
        SubmitResult build_and_submit(
            const tx::PalletCall &call,
            const wallet::KeyPair &key,
            uint64_t tip = 0);

        /**
         * @brief Wait for a transaction to be included in a finalized block
         * @param tx_hash Transaction hash to monitor
         * @param timeout_seconds Maximum time to wait
         * @return true if confirmed
         */
        bool wait_for_finalization(const std::string &tx_hash,
                                   uint32_t timeout_seconds = 120);

        // ─── System Info ──────────────────────────────────────

        /**
         * @brief Get node chain name
         */
        std::string get_chain_name();

        /**
         * @brief Get node software version
         */
        std::string get_node_version();

        /**
         * @brief Get system health (peers, syncing status)
         */
        json get_system_health();

        /**
         * @brief Check if node is fully synced
         */
        bool is_synced();

        /**
         * @brief Get pending extrinsics in the pool
         * @return Array of hex-encoded pending extrinsics
         */
        std::vector<std::string> get_pending_extrinsics();

        // ─── Midnight-specific RPC ────────────────────────────

        /**
         * @brief Get Midnight ledger version
         * @return JSON with ledger version info
         */
        json midnight_ledger_version();

        /**
         * @brief Get Midnight Zswap state root
         * @return Zswap state root hash as hex
         */
        std::string midnight_zswap_state_root();

        /**
         * @brief Get Midnight contract state via RPC
         * @param contract_address Contract address hex
         * @return JSON with contract state
         */
        json midnight_contract_state(const std::string &contract_address);

        /**
         * @brief Get Midnight API versions
         * @return JSON with supported API versions
         */
        json midnight_api_versions();

        /**
         * @brief Compute Substrate storage key for System.Account(AccountId)
         * @param account_id_hex 32-byte account ID in hex
         * @return Full storage key in hex
         */
        static std::string compute_account_storage_key(
            const std::string &account_id_hex);

    private:
        std::unique_ptr<NetworkClient> client_;
        uint32_t request_id_ = 1;

        /// Cached values to avoid redundant RPC calls
        std::optional<RuntimeVersion> cached_runtime_version_;
        std::optional<std::string> cached_genesis_hash_;

        /**
         * @brief Low-level JSON-RPC 2.0 call
         */
        json rpc_call(const std::string &method,
                      const json &params = json::array());


    };

} // namespace midnight::network
