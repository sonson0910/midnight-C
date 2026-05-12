/**
 * Rate Limiting Module
 *
 * Implements token bucket rate limiting with exponential backoff for RPC calls.
 * Thread-safe jitter: uses std::random_device + std::mt19937 + std::uniform_real_distribution
 */

#pragma once

#include <cstdint>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <random>

namespace midnight::network {

/**
 * Token bucket rate limiter
 */
class RateLimiter {
public:
    /**
     * @param rate Tokens per second
     * @param burst Maximum burst size (tokens)
     */
    RateLimiter(double rate, size_t burst = 1)
        : rate_(rate)
        , tokens_(static_cast<double>(burst))
        , max_tokens_(static_cast<double>(burst))
        , last_update_(std::chrono::steady_clock::now()) {}

    /**
     * Try to acquire a token
     * @return true if token was acquired, false if rate limited
     */
    bool try_acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        refill();
        
        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return true;
        }
        return false;
    }

    /**
     * Wait until a token is available (blocks)
     */
    void acquire() {
        while (!try_acquire()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    /**
     * Get time until next token is available
     */
    std::chrono::milliseconds time_until_next_token() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tokens_ >= 1.0) return std::chrono::milliseconds(0);
        
        double tokens_needed = 1.0 - tokens_;
        auto ms = static_cast<uint64_t>((tokens_needed / rate_) * 1000);
        return std::chrono::milliseconds(ms);
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_update_).count();
        last_update_ = now;
        
        tokens_ = std::min(max_tokens_, tokens_ + elapsed * rate_);
    }

    double rate_;
    double tokens_;
    double max_tokens_;
    std::chrono::steady_clock::time_point last_update_;
    mutable std::mutex mutex_;
};

/**
 * Exponential backoff with jitter
 */
class ExponentialBackoff {
public:
    struct Config {
        uint32_t initial_delay_ms;
        uint32_t max_delay_ms;
        double multiplier;
        double jitter_factor;
        uint32_t max_retries;
    };

    ExponentialBackoff()
        : config_{100, 30000, 2.0, 0.3, 5}
        , current_delay_ms_(100) {}

    explicit ExponentialBackoff(Config config)
        : config_(config)
        , current_delay_ms_(config.initial_delay_ms) {}

    /**
     * Execute operation with retry and exponential backoff
     * @tparam Func Callable that returns bool (true = success)
     * @param operation Operation to execute
     * @return true if operation succeeded, false after all retries exhausted
     */
    template<typename Func>
    bool execute(Func&& operation) {
        uint32_t attempt = 0;

        while (attempt <= config_.max_retries) {
            if (operation()) {
                reset();
                return true;
            }

            if (attempt >= config_.max_retries) {
                break;
            }

            uint32_t delay = get_next_delay();
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            attempt++;
        }

        return false;
    }

    /**
     * Get next delay with exponential backoff and jitter
     * Thread-safe: uses std::random_device + std::uniform_real_distribution
     */
    uint32_t get_next_delay() {
        uint32_t delay = current_delay_ms_;
        current_delay_ms_ = static_cast<uint32_t>(std::min(
            static_cast<double>(current_delay_ms_) * config_.multiplier,
            static_cast<double>(config_.max_delay_ms)
        ));

        // Thread-safe jitter using random_device + uniform_real_distribution
        thread_local std::random_device rd;
        thread_local std::mt19937 gen(rd());
        thread_local std::uniform_real_distribution<double> dist(-1.0, 1.0);
        double jitter = dist(gen) * config_.jitter_factor;
        uint32_t jitter_ms = static_cast<uint32_t>(delay * jitter);

        return std::max(1u, static_cast<uint32_t>(delay) + jitter_ms);
    }

    /**
     * Reset backoff state (call on successful operation)
     */
    void reset() {
        current_delay_ms_ = config_.initial_delay_ms;
    }

private:
    Config config_;
    uint32_t current_delay_ms_;
};

/**
 * Circuit breaker pattern for RPC resilience
 */
class CircuitBreaker {
public:
    enum class State { CLOSED, OPEN, HALF_OPEN };

    struct Config {
        uint32_t failure_threshold;
        uint32_t success_threshold;
        uint32_t open_duration_ms;
    };

    CircuitBreaker()
        : config_{5, 3, 30000}
        , state_(State::CLOSED)
        , consecutive_failures_(0)
        , consecutive_successes_(0)
        , last_failure_time_(std::chrono::steady_clock::time_point{}) {}

    explicit CircuitBreaker(Config config)
        : config_(config)
        , state_(State::CLOSED)
        , consecutive_failures_(0)
        , consecutive_successes_(0)
        , last_failure_time_(std::chrono::steady_clock::time_point{}) {}

    /**
     * Check if circuit allows requests
     */
    bool is_request_allowed() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == State::CLOSED) {
            return true;
        }
        
        if (state_ == State::OPEN) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_failure_time_ >= std::chrono::milliseconds(config_.open_duration_ms)) {
                state_ = State::HALF_OPEN;
                consecutive_successes_ = 0;
                return true;
            }
            return false;
        }
        
        // HALF_OPEN - allow limited requests
        return true;
    }

    /**
     * Record successful request
     */
    void record_success() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == State::HALF_OPEN) {
            consecutive_successes_++;
            if (consecutive_successes_ >= config_.success_threshold) {
                state_ = State::CLOSED;
                consecutive_failures_ = 0;
                consecutive_successes_ = 0;
            }
        } else if (state_ == State::CLOSED) {
            consecutive_failures_ = 0;
        }
    }

    /**
     * Record failed request
     */
    void record_failure() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        consecutive_failures_++;
        last_failure_time_ = std::chrono::steady_clock::now();
        
        if (state_ == State::HALF_OPEN) {
            state_ = State::OPEN;
            consecutive_successes_ = 0;
        } else if (state_ == State::CLOSED) {
            if (consecutive_failures_ >= config_.failure_threshold) {
                state_ = State::OPEN;
            }
        }
    }

    State get_state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

private:
    Config config_;
    State state_;
    uint32_t consecutive_failures_;
    uint32_t consecutive_successes_;
    std::chrono::steady_clock::time_point last_failure_time_;
    mutable std::mutex mutex_;
};

} // namespace midnight::network
