#pragma once

#include "proof_server_client.hpp"
#include <cstdint>
#include <chrono>
#include <queue>
#include <memory>
#include <optional>
#include <mutex>
#include <atomic>

namespace midnight::zk
{

    /**
     * @brief Configuration for resilient proof server client behavior
     *
     * Controls retry strategy, timeout handling, and health check behavior
     */
    struct ResilienceConfig
    {
        // Retry configuration
        uint32_t max_retries = 3;                          ///< Maximum number of retry attempts
        std::chrono::milliseconds initial_backoff_ms{100}; ///< Initial backoff duration (exponential)
        std::chrono::milliseconds max_backoff_ms{30000};   ///< Maximum backoff duration cap (30 seconds)
        double backoff_multiplier = 2.0;                   ///< Exponential backoff multiplier

        // Timeout configuration
        std::chrono::milliseconds operation_timeout_ms{60000};   ///< Timeout for individual operations (1 minute)
        std::chrono::milliseconds health_check_timeout_ms{5000}; ///< Timeout for health checks (5 seconds)

        // Circuit breaker configuration
        uint32_t failure_threshold = 5;                 ///< Failures before circuit opens
        std::chrono::seconds circuit_recovery_time{60}; ///< Time before attempting recovery (60 seconds)

        // Health check configuration
        std::chrono::seconds health_check_interval{30}; ///< Interval between health checks
        bool enable_auto_health_check = true;           ///< Enable automatic periodic health checks

        // Request queue configuration
        size_t max_queued_requests = 100; ///< Maximum requests in queue during recovery
        bool enable_request_queue = true; ///< Enable request queueing during transient failures
    };

    /**
     * @brief Result of a resilient operation attempt
     */
    struct ResilienceMetrics
    {
        uint32_t retry_count = 0;                           ///< Number of retries performed
        std::chrono::milliseconds total_time_ms{0};         ///< Total time including retries
        std::chrono::milliseconds first_attempt_time_ms{0}; ///< Time of first attempt
        bool succeeded_after_retry = false;                 ///< True if succeeded after at least one retry
        std::string last_error;                             ///< Last error message if all retries failed
    };

    /**
     * @brief Circuit breaker state for the proof server connection
     */
    enum class CircuitBreakerState
    {
        CLOSED,   ///< Normal operation, requests proceeding
        OPEN,     ///< Too many failures, requests blocked
        HALF_OPEN ///< Recovery attempt in progress
    };

    /**
     * @brief Resilient wrapper for ProofServerClient with error recovery
     *
     * Provides automatic retry logic, circuit breaker pattern, health checks,
     * and request queueing to handle transient failures gracefully.
     *
     * Features:
     * - Exponential backoff retry strategy with configurable limits
     * - Circuit breaker to prevent cascading failures
     * - Periodic health checks to detect connection issues early
     * - Request queueing to retry failed requests during recovery
     * - Comprehensive metrics tracking for monitoring
     */
    class ProofServerResilientClient
    {
    public:
        /**
         * @brief Construct resilient client with default configuration
         * @param client Base proof server client to wrap
         */
        explicit ProofServerResilientClient(std::shared_ptr<ProofServerClient> client);

        /**
         * @brief Construct resilient client with custom configuration
         * @param client Base proof server client to wrap
         * @param config Resilience configuration
         */
        ProofServerResilientClient(std::shared_ptr<ProofServerClient> client, const ResilienceConfig &config);

        /**
         * @brief Update resilience configuration at runtime
         * @param config New configuration
         */
        void set_config(const ResilienceConfig &config);

        /**
         * @brief Get current resilience configuration
         * @return Current configuration
         */
        ResilienceConfig get_config() const;

        // ============ Resilient operation wrappers ============

        /**
         * @brief Generate proof with automatic retry and error recovery
         * @param circuit_name Name of the circuit
         * @param circuit_data Compiled circuit data
         * @param inputs Public inputs
         * @param witnesses Private witness data
         * @return ProofResult with metrics
         */
        ProofResult generate_proof_resilient(
            const std::string &circuit_name,
            const std::vector<uint8_t> &circuit_data,
            const PublicInputs &inputs,
            const WitnessOutput &witnesses);

        /**
         * @brief Verify proof with automatic retry
         * @param proof Proof to verify
         * @return Verification result with metrics
         */
        bool verify_proof_resilient(const CircuitProof &proof, ResilienceMetrics &metrics);

        /**
         * @brief Get circuit metadata with automatic retry
         * @param circuit_name Name of the circuit
         * @return Circuit metadata with metrics
         */
        CircuitProofMetadata get_circuit_metadata_resilient(
            const std::string &circuit_name,
            ResilienceMetrics &metrics);

        /**
         * @brief Get server status with automatic retry
         * @return Server status JSON
         */
        json get_server_status_resilient(ResilienceMetrics &metrics);

        /**
         * @brief Submit proof transaction with automatic retry
         * @param tx Proof-enabled transaction
         * @param rpc_endpoint RPC endpoint URL
         * @return Transaction ID on success
         */
        std::string submit_proof_transaction_resilient(
            const ProofEnabledTransaction &tx,
            const std::string &rpc_endpoint,
            ResilienceMetrics &metrics);

        /**
         * @brief Connect to proof server with health check
         * @return true if connection successful
         */
        bool connect_resilient();

        // ============ Health check and monitoring ============

        /**
         * @brief Perform immediate health check
         * @return true if server is healthy
         */
        bool perform_health_check();

        /**
         * @brief Get current circuit breaker state
         * @return Circuit breaker state
         */
        CircuitBreakerState get_circuit_breaker_state() const;

        /**
         * @brief Reset circuit breaker and clear failure counters
         */
        void reset_circuit_breaker();

        /**
         * @brief Get number of consecutive failures
         * @return Current failure count
         */
        uint32_t get_failure_count() const;

        /**
         * @brief Get total operations attempted
         * @return Total operation count
         */
        uint64_t get_total_operations() const;

        /**
         * @brief Get total successful operations
         * @return Successful operation count
         */
        uint64_t get_successful_operations() const;

        /**
         * @brief Get average retry count across all operations
         * @return Average retry count
         */
        double get_average_retries() const;

        /**
         * @brief Get number of requests currently in recovery queue
         * @return Queue size
         */
        size_t get_queued_requests_count() const;

        /**
         * @brief Get detailed resilience statistics as JSON
         * @return Statistics object
         */
        json get_statistics_json() const;

        // ============ Error handling ============

        /**
         * @brief Get last error message from underlying client
         * @return Error message
         */
        std::string get_last_error() const;

        /**
         * @brief Clear all error state and statistics
         */
        void clear_state();

    private:
        // ============ Internal types and helpers ============

        /**
         * @brief Queued request for later retry
         */
        struct QueuedRequest
        {
            std::string operation_type;                       ///< Type of operation (e.g., "generate_proof")
            std::chrono::system_clock::time_point created_at; ///< When request was queued
            std::string circuit_name;
            std::vector<uint8_t> circuit_data;
            PublicInputs inputs;
            WitnessOutput witnesses;
            json user_data; ///< Additional user-provided data
        };

        // ============ Internal state ============

        std::shared_ptr<ProofServerClient> client_; ///< Underlying proof server client
        ResilienceConfig config_;                   ///< Current resilience configuration

        // Thread safety: mutex protects circuit breaker state transitions
        mutable std::mutex state_mutex_;

        CircuitBreakerState circuit_breaker_state_{CircuitBreakerState::CLOSED};
        uint32_t consecutive_failures_ = 0;                       ///< Current failure count
        std::chrono::system_clock::time_point last_failure_time_; ///< When last failure occurred
        std::chrono::system_clock::time_point last_health_check_; ///< When last health check was performed

        // Statistics tracking — atomic for lock-free reads from monitoring threads
        std::atomic<uint64_t> total_operations_{0};      ///< Total operations attempted
        std::atomic<uint64_t> successful_operations_{0}; ///< Total successful operations
        mutable std::atomic<uint64_t> total_retries_{0};         ///< Total retries performed

        // Request queuing
        std::queue<QueuedRequest> request_queue_; ///< Queue for requests during transient failures

        // ============ Internal helper methods ============

        /**
         * @brief Calculate next backoff duration using exponential backoff
         * @param retry_count Number of retries so far
         * @return Duration to backoff
         */
        std::chrono::milliseconds calculate_backoff(uint32_t retry_count) const;

        /**
         * @brief Update circuit breaker state based on success/failure
         * @param success true if operation succeeded
         */
        void update_circuit_breaker(bool success);

        /**
         * @brief Check if circuit breaker should transition to HALF_OPEN
         * @return true if should attempt recovery
         */
        bool should_attempt_recovery() const;

        /**
         * @brief Unlocked variant for use inside already-locked critical sections
         * @note Caller MUST hold state_mutex_ before calling
         */
        bool should_attempt_recovery_unlocked() const;


        /**
         * @brief Attempt to drain queued requests
         */
        void attempt_drain_queue();

        /**
         * @brief Log operation metrics for monitoring
         * @param operation_name Name of operation
         * @param metrics Operation metrics
         * @param success Whether operation succeeded
         */
        void log_metrics(const std::string &operation_name, const ResilienceMetrics &metrics, bool success);
    };

    // ============ Utility namespace for resilience helpers ============

    namespace resilience_util
    {
        /**
         * @brief Create default resilience configuration for development
         * @return Configuration with relaxed timeouts and retry counts
         */
        ResilienceConfig create_dev_config();

        /**
         * @brief Create default resilience configuration for production
         * @return Configuration with strict timeouts and higher resilience
         */
        ResilienceConfig create_production_config();

        /**
         * @brief Create default resilience configuration for testing
         * @return Configuration optimized for automated testing
         */
        ResilienceConfig create_test_config();

        /**
         * @brief Format resilience metrics as readable string
         * @param metrics Metrics to format
         * @return Formatted string
         */
        std::string format_metrics(const ResilienceMetrics &metrics);

        /**
         * @brief Create factory function for resilient client with test defaults
         * @return Configured resilient client for testing
         */
        std::shared_ptr<ProofServerResilientClient> create_test_resilient_client();
    }

} // namespace midnight::zk
