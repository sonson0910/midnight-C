#pragma once

#include <cstdint>
#include <chrono>

namespace midnight::network
{

    /**
     * Retry Configuration for Network Operations
     *
     * Implements exponential backoff strategy for handling transient failures:
     * - Connection timeouts
     * - Temporary network errors
     * - Server temporary unavailability (5xx errors on first attempt)
     */

    struct RetryConfig
    {
        // Maximum number of retry attempts for transient failures
        // Total attempts = initial + retries (e.g., 1 + 3 = 4 total)
        uint32_t max_retries = 3;

        // Initial backoff delay before first retry (milliseconds)
        uint32_t initial_backoff_ms = 100;

        // Maximum backoff delay (milliseconds)
        // Prevents exponential backoff from becoming too large
        uint32_t max_backoff_ms = 5000;

        // Backoff multiplier applied after each retry
        // 2.0 = exponential backoff (100ms → 200ms → 400ms → 800ms)
        double backoff_multiplier = 2.0;

        // HTTP errors that trigger retry
        // Errors like 500, 502, 503, 504 are transient
        // Errors like 400, 401, 404 are not retryable
        bool retry_on_500 = true;
        bool retry_on_502 = true;
        bool retry_on_503 = true;
        bool retry_on_504 = true;

        // Connection timeout (milliseconds) before retry
        bool retry_on_timeout = true;

        // Network connection errors (DNS, connection refused)
        bool retry_on_connection_error = true;

        /**
         * Check if the given HTTP status code should trigger a retry
         * @param status_code HTTP status code (e.g., 503)
         * @return true if this error is transient and should be retried
         */
        bool should_retry_status(int status_code) const
        {
            if (status_code >= 200 && status_code < 400)
                return false; // Success
            if (status_code >= 400 && status_code < 500)
                return false; // Client error

            // Server errors (5xx)
            if (status_code == 500)
                return retry_on_500;
            if (status_code == 502)
                return retry_on_502;
            if (status_code == 503)
                return retry_on_503;
            if (status_code == 504)
                return retry_on_504;

            return false;
        }

        /**
         * Calculate backoff delay for the nth retry
         * @param retry_count Current retry attempt number (0-based)
         * @return Milliseconds to wait before retry
         */
        uint32_t get_backoff_delay_ms(uint32_t retry_count) const
        {
            if (retry_count == 0)
                return initial_backoff_ms;

            // Exponential: initial * (multiplier ^ retry_count)
            double delay = initial_backoff_ms * std::pow(backoff_multiplier, retry_count);

            // Cap at max_backoff_ms
            if (delay > max_backoff_ms)
            {
                delay = max_backoff_ms;
            }

            return static_cast<uint32_t>(delay);
        }
    };

    /**
     * Network Operation Statistics
     * Tracks success, failures, and retry patterns
     */

    struct NetworkStats
    {
        // Total requests attempted (including retries)
        uint64_t total_requests = 0;

        // Successful requests (200 OK)
        uint64_t successful_requests = 0;

        // Failed requests (permanent errors, gave up on retries)
        uint64_t failed_requests = 0;

        // Number of retries performed
        uint64_t total_retries = 0;

        // Successful retries (succeeded after initial failure)
        uint64_t successful_retries = 0;

        // Transient errors that were retried
        uint64_t transient_errors = 0;

        // Network timeouts encountered
        uint64_t timeout_errors = 0;

        // Average response time (milliseconds)
        double avg_response_time_ms = 0.0;

        /**
         * Get success rate as percentage
         * @return Success rate (0-100)
         */
        double get_success_rate() const
        {
            if (total_requests == 0)
                return 0.0;
            return (100.0 * successful_requests) / total_requests;
        }

        /**
         * Get retry effectiveness percentage
         * @return Percentage of retries that succeeded
         * (0 if no retries were performed)
         */
        double get_retry_success_rate() const
        {
            if (total_retries == 0)
                return 0.0;
            return (100.0 * successful_retries) / total_retries;
        }

        /**
         * Reset all statistics
         */
        void reset()
        {
            total_requests = 0;
            successful_requests = 0;
            failed_requests = 0;
            total_retries = 0;
            successful_retries = 0;
            transient_errors = 0;
            timeout_errors = 0;
            avg_response_time_ms = 0.0;
        }
    };

} // namespace midnight::network
