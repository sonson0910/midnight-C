#pragma once

#include "midnight/zk/proof_types.hpp"
#include "midnight/network/network_client.hpp"
#include <memory>
#include <chrono>

namespace midnight::zk
{

    /**
     * @brief Client for communicating with the Midnight Proof Server
     *
     * The Proof Server runs at http://localhost:6300 by default and accepts
     * ledger-built tagged binary payloads.
     *
     * The canonical production endpoints are:
     * - /check with createCheckPayload(...) bytes
     * - /prove with createProvingPayload(...) bytes
     * - /prove-tx with TransactionProvePayload bytes
     *
     * Legacy JSON helpers return explicit errors so callers do not accidentally
     * build non-Midnight proof requests.
     */
    class ProofServerClient
    {
    public:
        /**
         * @brief Configuration for Proof Server connection
         */
        struct Config
        {
            std::string host = "localhost";
            uint16_t port = 6300;
            std::chrono::milliseconds timeout_ms{30000};
            bool use_https = false;

            std::string base_url() const;
        };

        /**
         * @brief Constructor
         * @param config Proof Server configuration
         */
        ProofServerClient();
        explicit ProofServerClient(const Config &config);

        /**
         * @brief Initialize connection to Proof Server
         * @return true if connection successful
         */
        bool connect();

        /**
         * @brief Check if connected to Proof Server
         * @return true if reachable
         */
        bool is_connected() const;

        /**
         * @brief Get Proof Server status
         *
         * Check if server is running and responsive.
         *
         * @return Server status information (version, circuits available, etc.)
         */
        json get_server_status();

        /**
         * @brief Read the proof-server package version from /version.
         */
        std::string get_server_version();

        /**
         * @brief Read the supported proof versions from /proof-versions.
         */
        json get_supported_proof_versions();

        /**
         * @brief Low-level Midnight proof-server /check endpoint.
         *
         * The official proof server expects a tagged binary payload produced by
         * ledger createCheckPayload(serializedPreimage), not JSON.
         *
         * @param check_payload Tagged binary check payload.
         * @return Tagged binary parseCheckResult payload from the proof server.
         */
        std::vector<uint8_t> post_check_payload(const std::vector<uint8_t> &check_payload);

        /**
         * @brief Low-level Midnight proof-server /prove endpoint.
         *
         * The official proof server expects a tagged binary payload produced by
         * ledger createProvingPayload(serializedPreimage, overwriteBindingInput,
         * keyMaterial), not JSON.
         *
         * @param proving_payload Tagged binary proving payload.
         * @return Tagged binary Proof payload from the proof server.
         */
        std::vector<uint8_t> post_proving_payload(const std::vector<uint8_t> &proving_payload);

        /**
         * @brief Low-level Midnight proof-server /prove-tx endpoint.
         *
         * Deprecated upstream but useful for compatibility. The request body
         * must be the ledger TransactionProvePayload binary serialization.
         *
         * @param prove_tx_payload Tagged binary transaction proving payload.
         * @return Serialized proven transaction bytes.
         */
        std::vector<uint8_t> post_prove_tx_payload(const std::vector<uint8_t> &prove_tx_payload);

        /**
         * @brief Set Proof Server configuration
         * @param config New configuration
         */
        void set_config(const Config &config);

        /**
         * @brief Get current configuration
         * @return Current Config
         */
        Config get_config() const { return config_; }

        /**
         * @brief Get last error message
         * @return Error message from last operation
         */
        std::string get_last_error() const { return last_error_; }

    private:
        Config config_;
        std::shared_ptr<midnight::network::NetworkClient> network_client_;
        std::string last_error_;

        void set_error(const std::string &error_msg);
    };

} // namespace midnight::zk
