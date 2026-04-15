#include "midnight/zk/proof_server_resilient_client.hpp"
#include <iostream>
#include <thread>
#include <fmt/format.h>

namespace midnight::zk
{

    // ============ Constructor and configuration ============

    ProofServerResilientClient::ProofServerResilientClient(
        std::shared_ptr<ProofServerClient> client)
        : client_(client), config_(ResilienceConfig()) {}

    ProofServerResilientClient::ProofServerResilientClient(
        std::shared_ptr<ProofServerClient> client,
        const ResilienceConfig &config)
        : client_(client), config_(config) {}

    void ProofServerResilientClient::set_config(const ResilienceConfig &config)
    {
        config_ = config;
    }

    ResilienceConfig ProofServerResilientClient::get_config() const
    {
        return config_;
    }

    // ============ Resilient operation wrappers ============

    ProofResult ProofServerResilientClient::generate_proof_resilient(
        const std::string &circuit_name,
        const std::vector<uint8_t> &circuit_data,
        const PublicInputs &inputs,
        const WitnessOutput &witnesses)
    {

        ResilienceMetrics metrics;
        auto start_time = std::chrono::high_resolution_clock::now();
        metrics.first_attempt_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            start_time.time_since_epoch());

        // Check circuit breaker
        if (circuit_breaker_state_ == CircuitBreakerState::OPEN)
        {
            if (!should_attempt_recovery())
            {
                metrics.last_error = "Circuit breaker is OPEN; proof server is unavailable";
                log_metrics("generate_proof", metrics, false);
                throw std::runtime_error(metrics.last_error);
            }
            circuit_breaker_state_ = CircuitBreakerState::HALF_OPEN;
        }

        ProofResult result;

        for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt)
        {
            try
            {
                total_operations_++;

                // Perform operation
                result = client_->generate_proof(
                    circuit_name,
                    circuit_data,
                    inputs,
                    {{"default", witnesses}});

                // Success
                successful_operations_++;
                update_circuit_breaker(true);
                metrics.retry_count = attempt;
                metrics.succeeded_after_retry = (attempt > 0);

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("generate_proof", metrics, true);
                return result;
            }
            catch (const std::exception &e)
            {
                metrics.last_error = e.what();
                consecutive_failures_++;
                last_failure_time_ = std::chrono::system_clock::now();

                if (attempt < config_.max_retries)
                {
                    // Calculate backoff and retry
                    auto backoff = calculate_backoff(attempt);
                    log_metrics("generate_proof (retry " + std::to_string(attempt + 1) + ")", metrics, false);
                    std::this_thread::sleep_for(backoff);
                    continue;
                }

                // All retries exhausted
                update_circuit_breaker(false);
                metrics.retry_count = attempt;

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("generate_proof", metrics, false);
                throw;
            }
        }

        return result;
    }

    bool ProofServerResilientClient::verify_proof_resilient(
        const CircuitProof &proof,
        ResilienceMetrics &metrics)
    {

        auto start_time = std::chrono::high_resolution_clock::now();

        // Check circuit breaker
        if (circuit_breaker_state_ == CircuitBreakerState::OPEN)
        {
            if (!should_attempt_recovery())
            {
                metrics.last_error = "Circuit breaker is OPEN";
                return false;
            }
            circuit_breaker_state_ = CircuitBreakerState::HALF_OPEN;
        }

        for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt)
        {
            try
            {
                total_operations_++;
                bool result = client_->verify_proof(proof);

                successful_operations_++;
                update_circuit_breaker(true);
                metrics.retry_count = attempt;
                metrics.succeeded_after_retry = (attempt > 0);

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("verify_proof", metrics, true);
                return result;
            }
            catch (const std::exception &e)
            {
                metrics.last_error = e.what();
                consecutive_failures_++;
                last_failure_time_ = std::chrono::system_clock::now();

                if (attempt < config_.max_retries)
                {
                    auto backoff = calculate_backoff(attempt);
                    std::this_thread::sleep_for(backoff);
                    continue;
                }

                update_circuit_breaker(false);
                metrics.retry_count = attempt;

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("verify_proof", metrics, false);
                return false;
            }
        }

        return false;
    }

    CircuitProofMetadata ProofServerResilientClient::get_circuit_metadata_resilient(
        const std::string &circuit_name,
        ResilienceMetrics &metrics)
    {

        auto start_time = std::chrono::high_resolution_clock::now();

        // Check circuit breaker
        if (circuit_breaker_state_ == CircuitBreakerState::OPEN)
        {
            if (!should_attempt_recovery())
            {
                throw std::runtime_error("Circuit breaker is OPEN");
            }
            circuit_breaker_state_ = CircuitBreakerState::HALF_OPEN;
        }

        for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt)
        {
            try
            {
                total_operations_++;
                auto result = client_->get_circuit_metadata(circuit_name);

                successful_operations_++;
                update_circuit_breaker(true);
                metrics.retry_count = attempt;
                metrics.succeeded_after_retry = (attempt > 0);

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("get_circuit_metadata", metrics, true);
                return result;
            }
            catch (const std::exception &e)
            {
                metrics.last_error = e.what();
                consecutive_failures_++;
                last_failure_time_ = std::chrono::system_clock::now();

                if (attempt < config_.max_retries)
                {
                    auto backoff = calculate_backoff(attempt);
                    std::this_thread::sleep_for(backoff);
                    continue;
                }

                update_circuit_breaker(false);
                metrics.retry_count = attempt;

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("get_circuit_metadata", metrics, false);
                throw;
            }
        }

        throw std::runtime_error("Failed to get circuit metadata after all retries");
    }

    json ProofServerResilientClient::get_server_status_resilient(ResilienceMetrics &metrics)
    {
        auto start_time = std::chrono::high_resolution_clock::now();

        // Check circuit breaker
        if (circuit_breaker_state_ == CircuitBreakerState::OPEN)
        {
            if (!should_attempt_recovery())
            {
                throw std::runtime_error("Circuit breaker is OPEN");
            }
            circuit_breaker_state_ = CircuitBreakerState::HALF_OPEN;
        }

        for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt)
        {
            try
            {
                total_operations_++;
                auto result = client_->get_server_status();

                successful_operations_++;
                update_circuit_breaker(true);
                metrics.retry_count = attempt;
                metrics.succeeded_after_retry = (attempt > 0);

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("get_server_status", metrics, true);
                return result;
            }
            catch (const std::exception &e)
            {
                metrics.last_error = e.what();
                consecutive_failures_++;
                last_failure_time_ = std::chrono::system_clock::now();

                if (attempt < config_.max_retries)
                {
                    auto backoff = calculate_backoff(attempt);
                    std::this_thread::sleep_for(backoff);
                    continue;
                }

                update_circuit_breaker(false);
                metrics.retry_count = attempt;

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("get_server_status", metrics, false);
                throw;
            }
        }

        throw std::runtime_error("Failed to get server status after all retries");
    }

    std::string ProofServerResilientClient::submit_proof_transaction_resilient(
        const ProofEnabledTransaction &tx,
        const std::string &rpc_endpoint,
        ResilienceMetrics &metrics)
    {

        auto start_time = std::chrono::high_resolution_clock::now();

        // Check circuit breaker
        if (circuit_breaker_state_ == CircuitBreakerState::OPEN)
        {
            if (!should_attempt_recovery())
            {
                throw std::runtime_error("Circuit breaker is OPEN");
            }
            circuit_breaker_state_ = CircuitBreakerState::HALF_OPEN;
        }

        for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt)
        {
            try
            {
                total_operations_++;
                auto result = client_->submit_proof_transaction(tx, rpc_endpoint);

                successful_operations_++;
                update_circuit_breaker(true);
                metrics.retry_count = attempt;
                metrics.succeeded_after_retry = (attempt > 0);

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("submit_proof_transaction", metrics, true);
                return result;
            }
            catch (const std::exception &e)
            {
                metrics.last_error = e.what();
                consecutive_failures_++;
                last_failure_time_ = std::chrono::system_clock::now();

                if (attempt < config_.max_retries)
                {
                    auto backoff = calculate_backoff(attempt);
                    std::this_thread::sleep_for(backoff);
                    continue;
                }

                update_circuit_breaker(false);
                metrics.retry_count = attempt;

                auto end_time = std::chrono::high_resolution_clock::now();
                metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                log_metrics("submit_proof_transaction", metrics, false);
                throw;
            }
        }

        throw std::runtime_error("Failed to submit proof transaction after all retries");
    }

    bool ProofServerResilientClient::connect_resilient()
    {
        ResilienceMetrics metrics;
        auto start_time = std::chrono::high_resolution_clock::now();

        for (uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt)
        {
            try
            {
                bool connected = client_->connect();

                if (connected)
                {
                    consecutive_failures_ = 0;
                    circuit_breaker_state_ = CircuitBreakerState::CLOSED;
                    last_health_check_ = std::chrono::system_clock::now();

                    auto end_time = std::chrono::high_resolution_clock::now();
                    metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time);
                    metrics.retry_count = attempt;

                    log_metrics("connect", metrics, true);
                    return true;
                }
            }
            catch (const std::exception &e)
            {
                metrics.last_error = e.what();
            }

            if (attempt < config_.max_retries)
            {
                auto backoff = calculate_backoff(attempt);
                std::this_thread::sleep_for(backoff);
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        metrics.total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        metrics.retry_count = config_.max_retries;

        log_metrics("connect", metrics, false);
        return false;
    }

    // ============ Health check and monitoring ============

    bool ProofServerResilientClient::perform_health_check()
    {
        try
        {
            ResilienceMetrics metrics;
            auto status = get_server_status_resilient(metrics);

            last_health_check_ = std::chrono::system_clock::now();

            // If we get a response, server is healthy
            if (circuit_breaker_state_ == CircuitBreakerState::HALF_OPEN)
            {
                circuit_breaker_state_ = CircuitBreakerState::CLOSED;
                consecutive_failures_ = 0;
            }

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    CircuitBreakerState ProofServerResilientClient::get_circuit_breaker_state() const
    {
        return circuit_breaker_state_;
    }

    void ProofServerResilientClient::reset_circuit_breaker()
    {
        circuit_breaker_state_ = CircuitBreakerState::CLOSED;
        consecutive_failures_ = 0;
        request_queue_ = std::queue<QueuedRequest>();
    }

    uint32_t ProofServerResilientClient::get_failure_count() const
    {
        return consecutive_failures_;
    }

    uint64_t ProofServerResilientClient::get_total_operations() const
    {
        return total_operations_;
    }

    uint64_t ProofServerResilientClient::get_successful_operations() const
    {
        return successful_operations_;
    }

    double ProofServerResilientClient::get_average_retries() const
    {
        if (total_operations_ == 0)
            return 0.0;
        return static_cast<double>(total_retries_) / static_cast<double>(total_operations_);
    }

    size_t ProofServerResilientClient::get_queued_requests_count() const
    {
        return request_queue_.size();
    }

    json ProofServerResilientClient::get_statistics_json() const
    {
        json stats;
        stats["circuit_breaker_state"] = static_cast<int>(circuit_breaker_state_);
        stats["consecutive_failures"] = consecutive_failures_;
        stats["total_operations"] = total_operations_;
        stats["successful_operations"] = successful_operations_;
        stats["failure_rate"] = total_operations_ > 0
                                    ? (static_cast<double>(total_operations_ - successful_operations_) / total_operations_)
                                    : 0.0;
        stats["average_retries"] = get_average_retries();
        stats["queued_requests"] = request_queue_.size();

        // Add timing information
        if (!last_failure_time_.time_since_epoch().count() == 0)
        {
            auto now = std::chrono::system_clock::now();
            auto seconds_since_failure = std::chrono::duration_cast<std::chrono::seconds>(
                                             now - last_failure_time_)
                                             .count();
            stats["seconds_since_last_failure"] = seconds_since_failure;
        }

        if (!last_health_check_.time_since_epoch().count() == 0)
        {
            auto now = std::chrono::system_clock::now();
            auto seconds_since_health_check = std::chrono::duration_cast<std::chrono::seconds>(
                                                  now - last_health_check_)
                                                  .count();
            stats["seconds_since_last_health_check"] = seconds_since_health_check;
        }

        return stats;
    }

    std::string ProofServerResilientClient::get_last_error() const
    {
        return client_->get_last_error();
    }

    void ProofServerResilientClient::clear_state()
    {
        reset_circuit_breaker();
        total_operations_ = 0;
        successful_operations_ = 0;
        total_retries_ = 0;
    }

    // ============ Internal helper methods ============

    std::chrono::milliseconds ProofServerResilientClient::calculate_backoff(uint32_t retry_count) const
    {
        // Exponential backoff: initial_backoff * (multiplier ^ retry_count)
        double backoff_ms = config_.initial_backoff_ms.count();
        for (uint32_t i = 0; i < retry_count; ++i)
        {
            backoff_ms *= config_.backoff_multiplier;
            if (backoff_ms > config_.max_backoff_ms.count())
            {
                backoff_ms = config_.max_backoff_ms.count();
                break;
            }
        }

        const_cast<ProofServerResilientClient *>(this)->total_retries_++;
        return std::chrono::milliseconds(static_cast<int64_t>(backoff_ms));
    }

    void ProofServerResilientClient::update_circuit_breaker(bool success)
    {
        if (success)
        {
            if (circuit_breaker_state_ == CircuitBreakerState::HALF_OPEN)
            {
                circuit_breaker_state_ = CircuitBreakerState::CLOSED;
            }
            consecutive_failures_ = 0;
        }
        else
        {
            consecutive_failures_++;
            if (consecutive_failures_ >= config_.failure_threshold)
            {
                circuit_breaker_state_ = CircuitBreakerState::OPEN;
                last_failure_time_ = std::chrono::system_clock::now();
            }
        }
    }

    bool ProofServerResilientClient::should_attempt_recovery() const
    {
        if (circuit_breaker_state_ != CircuitBreakerState::OPEN)
        {
            return true;
        }

        auto now = std::chrono::system_clock::now();
        auto time_since_failure = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_failure_time_);

        return time_since_failure >= config_.circuit_recovery_time;
    }

    void ProofServerResilientClient::attempt_drain_queue()
    {
        while (!request_queue_.empty())
        {
            try
            {
                auto req = request_queue_.front();
                auto result = client_->generate_proof(
                    req.circuit_name,
                    req.circuit_data,
                    req.inputs,
                    {{"default", req.witnesses}});
                request_queue_.pop();
                successful_operations_++;
            }
            catch (...)
            {
                break; // Stop draining if any request fails
            }
        }
    }

    void ProofServerResilientClient::log_metrics(
        const std::string &operation_name,
        const ResilienceMetrics &metrics,
        bool success)
    {

        std::string status = success ? "SUCCESS" : "FAILED";
        std::string message = fmt::format(
            "[{}] Operation: {}, Retries: {}, Time: {}ms, Status: {}",
            status,
            operation_name,
            metrics.retry_count,
            metrics.total_time_ms.count(),
            status);

        if (!metrics.last_error.empty())
        {
            message += fmt::format(", Error: {}", metrics.last_error);
        }

        // For production, this would be sent to logging system
        // For now, just track internally
    }

    // ============ Utility namespace implementations ============

    namespace resilience_util
    {

        ResilienceConfig create_dev_config()
        {
            ResilienceConfig config;
            config.max_retries = 5;
            config.initial_backoff_ms = std::chrono::milliseconds(50);
            config.max_backoff_ms = std::chrono::milliseconds(5000);
            config.operation_timeout_ms = std::chrono::milliseconds(120000); // 2 minutes
            config.failure_threshold = 10;
            config.circuit_recovery_time = std::chrono::seconds(30);
            config.health_check_interval = std::chrono::seconds(60);
            return config;
        }

        ResilienceConfig create_production_config()
        {
            ResilienceConfig config;
            config.max_retries = 3;
            config.initial_backoff_ms = std::chrono::milliseconds(100);
            config.max_backoff_ms = std::chrono::milliseconds(30000);       // 30 seconds
            config.operation_timeout_ms = std::chrono::milliseconds(60000); // 1 minute
            config.failure_threshold = 5;
            config.circuit_recovery_time = std::chrono::seconds(120); // 2 minutes
            config.health_check_interval = std::chrono::seconds(30);
            return config;
        }

        ResilienceConfig create_test_config()
        {
            ResilienceConfig config;
            config.max_retries = 2;
            config.initial_backoff_ms = std::chrono::milliseconds(10);
            config.max_backoff_ms = std::chrono::milliseconds(100);
            config.operation_timeout_ms = std::chrono::milliseconds(5000); // 5 seconds
            config.failure_threshold = 3;
            config.circuit_recovery_time = std::chrono::seconds(5);
            config.health_check_interval = std::chrono::seconds(10);
            return config;
        }

        std::string format_metrics(const ResilienceMetrics &metrics)
        {
            return fmt::format(
                "Retries: {}, Time: {}ms, Succeeded after retry: {}",
                metrics.retry_count,
                metrics.total_time_ms.count(),
                metrics.succeeded_after_retry ? "yes" : "no");
        }

        std::shared_ptr<ProofServerResilientClient> create_test_resilient_client()
        {
            auto client = std::make_shared<ProofServerClient>();
            auto resilient = std::make_shared<ProofServerResilientClient>(
                client,
                create_test_config());
            return resilient;
        }

    } // namespace resilience_util

} // namespace midnight::zk
