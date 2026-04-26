#pragma once

#include <string>
#include <cstdint>
#include <map>
#include <memory>

namespace midnight
{

    /**
     * Official Network Configuration (v4 API)
     * Based on: docs.midnight.network (Apr 25, 2026)
     *
     * This file contains all official network parameters for Midnight,
     * covering consensus, cryptography, privacy, and tokenomics.
     * Indexer endpoints now use /api/v4/graphql.
     */

    /**
     * Official Network Parameters (Read-Only)
     * These are constants from official Midnight documentation
     */
    struct NetworkConfig
    {
        // === CONSENSUS (Official - Polkadot SDK) ===
        static constexpr const char *CONSENSUS_MECHANISM = "AURA + GRANDPA";
        static constexpr const char *BASE_FRAMEWORK = "Polkadot SDK";

        // === BLOCKCHAIN PARAMETERS (Official) ===
        static constexpr uint32_t BLOCK_TIME_SECONDS = 6; // Core parameter
        static constexpr uint32_t SESSION_LENGTH_SLOTS = 1200;
        static constexpr const char *HASH_FUNCTION = "blake2_256";
        static constexpr const char *ACCOUNT_TYPE = "sr25519";

        // === SIGNATURE SCHEMES (Official) ===
        static constexpr const char *AURA_SIGNING = "sr25519";       // Block authorship
        static constexpr const char *FINALITY_SIGNING = "ed25519";   // GRANDPA finality
        static constexpr const char *PARTNERCHAIN_SIGNING = "ECDSA"; // Cardano bridge

        // === PRIVACY (Official) ===
        static constexpr const char *PROOF_SYSTEM = "zk-SNARKs";
        static constexpr uint32_t PROOF_SIZE_BYTES = 128;        // Official proof size
        static constexpr const char *STATE_MODEL = "Dual-State"; // Public + Private

        // === TOKENS (Official) ===
        static constexpr const char *PRIMARY_TOKEN = "NIGHT";
        static constexpr const char *RESOURCE_TOKEN = "DUST";
    };

    /**
     * Preprod (Testnet) Configuration
     * Official testnet endpoints and parameters
     */
    struct PreprodConfig
    {
        // === ENDPOINTS (Official) ===
        static constexpr const char *NODE_RPC_PROTOCOL = "wss";
        static constexpr const char *NODE_RPC_HOST = "rpc.preprod.midnight.network";
        static constexpr uint16_t NODE_RPC_PORT = 443;

        static std::string get_node_rpc_endpoint()
        {
            return std::string(NODE_RPC_PROTOCOL) + "://" + NODE_RPC_HOST;
        }

        static constexpr const char *INDEXER_GRAPHQL_URL =
            "https://indexer.preprod.midnight.network/api/v4/graphql";

        static constexpr const char *INDEXER_GRAPHQL_WS =
            "wss://indexer.preprod.midnight.network/api/v4/graphql/ws";

        // === FAUCET (Official) ===
        static constexpr const char *FAUCET_URL = "https://faucet.preprod.midnight.network/";
        static constexpr const char *FAUCET_TOKEN = "tNIGHT";

        // === EXPLORER (Official) ===
        static constexpr const char *EXPLORER_URL = "https://explorer.preprod.midnight.network/";

        static constexpr const char *NETWORK_NAME = "preprod";
        static constexpr const char *NETWORK_ID = "midnight-preprod-1";
        static constexpr bool IS_TESTNET = true;
    };

    /**
     * Preview (Testnet) Configuration
     * Official preview endpoints — recommended for development
     */
    struct PreviewConfig
    {
        static constexpr const char *NODE_RPC_PROTOCOL = "https";
        static constexpr const char *NODE_RPC_HOST = "rpc.preview.midnight.network";
        static constexpr uint16_t NODE_RPC_PORT = 443;

        static std::string get_node_rpc_endpoint()
        {
            return std::string(NODE_RPC_PROTOCOL) + "://" + NODE_RPC_HOST;
        }

        static constexpr const char *INDEXER_GRAPHQL_URL =
            "https://indexer.preview.midnight.network/api/v4/graphql";

        static constexpr const char *INDEXER_GRAPHQL_WS =
            "wss://indexer.preview.midnight.network/api/v4/graphql/ws";

        static constexpr const char *FAUCET_URL = "https://faucet.preview.midnight.network/";
        static constexpr const char *FAUCET_TOKEN = "tNIGHT";

        static constexpr const char *EXPLORER_URL = "https://explorer.preview.midnight.network/";

        static constexpr const char *NETWORK_NAME = "preview";
        static constexpr const char *NETWORK_ID = "midnight-preview";
        static constexpr bool IS_TESTNET = true;
    };

    /**
     * Protocol Parameters (Fee Calculation)
     */
    struct ProtocolParams
    {
        uint64_t min_fee_a = 44;
        uint64_t min_fee_b = 155381;
        uint64_t utxo_cost_per_byte = 4310;
        uint32_t max_tx_size = 16384;
        uint32_t max_block_size = 65536;
        uint32_t max_value_size = 5000;
        double price_memory = 0.0577;
        double price_steps = 0.0000721;

        uint64_t calculate_min_fee(uint32_t tx_size_bytes) const
        {
            return (static_cast<uint64_t>(tx_size_bytes) * min_fee_a) + min_fee_b;
        }
    };

    /**
     * Configuration Manager
     * Runtime configuration (file-based + defaults)
     */
    class Config
    {
    public:
        void load(const std::string &config_path);
        void set(const std::string &key, const std::string &value);
        std::string get(const std::string &key, const std::string &default_value = "") const;
        const std::map<std::string, std::string> &get_all() const;
        bool has(const std::string &key) const;

    private:
        std::map<std::string, std::string> config_map_;
    };

    // Global config instance
    extern std::shared_ptr<Config> g_config;

    // Convenience functions
    inline PreprodConfig get_preprod_config() { return PreprodConfig(); }
    inline PreviewConfig get_preview_config() { return PreviewConfig(); }
    inline NetworkConfig get_network_config() { return NetworkConfig(); }
    inline ProtocolParams get_default_protocol_params() { return ProtocolParams(); }

} // namespace midnight
