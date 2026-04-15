#include "midnight/zk/proof_server_resilient_client.hpp"
#include <iostream>
#include <iomanip>

using json = nlohmann::json;
using namespace midnight::zk;

// ============================================================================
// Example 1: Basic resilient client setup with exponential backoff
// ============================================================================

void example_resilient_client_setup()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 1: Basic Resilient Client Setup" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Create base client
    auto base_client = std::make_shared<ProofServerClient>();

    // Create resilient wrapper with custom configuration
    ResilienceConfig config;
    config.max_retries = 3;
    config.initial_backoff_ms = std::chrono::milliseconds(100);
    config.max_backoff_ms = std::chrono::milliseconds(10000);
    config.backoff_multiplier = 2.0;
    config.failure_threshold = 5;

    auto resilient_client = ProofServerResilientClient(base_client, config);

    std::cout << "✓ Resilient client created with config:" << std::endl;
    std::cout << "  - Max retries: " << config.max_retries << std::endl;
    std::cout << "  - Initial backoff: " << config.initial_backoff_ms.count() << "ms" << std::endl;
    std::cout << "  - Max backoff: " << config.max_backoff_ms.count() << "ms" << std::endl;
    std::cout << "  - Backoff multiplier: " << config.backoff_multiplier << "x" << std::endl;
    std::cout << "  - Failure threshold: " << config.failure_threshold << std::endl;
}

// ============================================================================
// Example 2: Predefined configurations (dev, prod, test)
// ============================================================================

void example_predefined_configurations()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 2: Predefined Configurations" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Development configuration
    auto dev_config = resilience_util::create_dev_config();
    std::cout << "\n[Development Config]" << std::endl;
    std::cout << "  - Max retries: " << dev_config.max_retries << std::endl;
    std::cout << "  - Initial backoff: " << dev_config.initial_backoff_ms.count() << "ms" << std::endl;
    std::cout << "  - Operation timeout: " << dev_config.operation_timeout_ms.count() << "ms" << std::endl;
    std::cout << "  - Failure threshold: " << dev_config.failure_threshold << std::endl;
    std::cout << "  - Circuit recovery time: " << dev_config.circuit_recovery_time.count() << "s" << std::endl;

    // Production configuration
    auto prod_config = resilience_util::create_production_config();
    std::cout << "\n[Production Config]" << std::endl;
    std::cout << "  - Max retries: " << prod_config.max_retries << std::endl;
    std::cout << "  - Initial backoff: " << prod_config.initial_backoff_ms.count() << "ms" << std::endl;
    std::cout << "  - Operation timeout: " << prod_config.operation_timeout_ms.count() << "ms" << std::endl;
    std::cout << "  - Failure threshold: " << prod_config.failure_threshold << std::endl;
    std::cout << "  - Circuit recovery time: " << prod_config.circuit_recovery_time.count() << "s" << std::endl;

    // Test configuration
    auto test_config = resilience_util::create_test_config();
    std::cout << "\n[Test Config]" << std::endl;
    std::cout << "  - Max retries: " << test_config.max_retries << std::endl;
    std::cout << "  - Initial backoff: " << test_config.initial_backoff_ms.count() << "ms" << std::endl;
    std::cout << "  - Operation timeout: " << test_config.operation_timeout_ms.count() << "ms" << std::endl;
    std::cout << "  - Failure threshold: " << test_config.failure_threshold << std::endl;
    std::cout << "  - Circuit recovery time: " << test_config.circuit_recovery_time.count() << "s" << std::endl;
}

// ============================================================================
// Example 3: Circuit breaker state transitions
// ============================================================================

void example_circuit_breaker_states()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 3: Circuit Breaker State Transitions" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto base_client = std::make_shared<ProofServerClient>();
    auto config = resilience_util::create_test_config();
    auto resilient_client = ProofServerResilientClient(base_client, config);

    std::cout << "\nCircuit Breaker State Lifecycle:" << std::endl;
    std::cout << "  1. Initial state (CLOSED): Requests proceed normally" << std::endl;
    std::cout << "     State: " << static_cast<int>(resilient_client.get_circuit_breaker_state()) << std::endl;

    std::cout << "\n  2. After failure threshold exceeded:" << std::endl;
    std::cout << "     State transitions to OPEN (0=CLOSED, 1=OPEN, 2=HALF_OPEN)" << std::endl;
    std::cout << "     - Requests are rejected without attempting connection" << std::endl;
    std::cout << "     - Prevents cascading failures" << std::endl;

    std::cout << "\n  3. After recovery timeout passes:" << std::endl;
    std::cout << "     State transitions to HALF_OPEN" << std::endl;
    std::cout << "     - Attempts one request to test recovery" << std::endl;
    std::cout << "     - If successful → CLOSED (normal operation)" << std::endl;
    std::cout << "     - If failed → OPEN (wait again)" << std::endl;

    std::cout << "\n  4. Manual reset available:" << std::endl;
    resilient_client.reset_circuit_breaker();
    std::cout << "     State after reset: " << static_cast<int>(resilient_client.get_circuit_breaker_state()) << std::endl;
}

// ============================================================================
// Example 4: Exponential backoff calculation
// ============================================================================

void example_exponential_backoff()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 4: Exponential Backoff Calculation" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    ResilienceConfig config;
    config.initial_backoff_ms = std::chrono::milliseconds(100);
    config.max_backoff_ms = std::chrono::milliseconds(30000);
    config.backoff_multiplier = 2.0;

    std::cout << "\nBackoff progression with exponential strategy:" << std::endl;
    std::cout << "  - Initial backoff: " << config.initial_backoff_ms.count() << "ms" << std::endl;
    std::cout << "  - Multiplier: " << config.backoff_multiplier << "x" << std::endl;
    std::cout << "  - Max backoff: " << config.max_backoff_ms.count() << "ms" << std::endl;

    std::cout << "\n  Retry Progress:" << std::endl;
    double current_backoff = config.initial_backoff_ms.count();
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        int backoff_ms = static_cast<int>(current_backoff);
        if (backoff_ms > config.max_backoff_ms.count())
        {
            backoff_ms = config.max_backoff_ms.count();
        }

        std::cout << "    Attempt " << (attempt + 1) << ": " << backoff_ms << "ms ";

        // Convert to seconds for readability
        if (backoff_ms >= 1000)
        {
            std::cout << "(" << (backoff_ms / 1000.0) << "s)";
        }
        std::cout << std::endl;

        current_backoff *= config.backoff_multiplier;
    }

    std::cout << "\nBenefits of exponential backoff:" << std::endl;
    std::cout << "  ✓ Avoids overwhelming recovering server" << std::endl;
    std::cout << "  ✓ Reduces cascading failures" << std::endl;
    std::cout << "  ✓ Gives transient issues time to resolve" << std::endl;
}

// ============================================================================
// Example 5: Health check and monitoring
// ============================================================================

void example_health_check_and_monitoring()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 5: Health Check and Monitoring" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto base_client = std::make_shared<ProofServerClient>();
    auto resilient_client = ProofServerResilientClient(base_client);

    std::cout << "\nHealth Check Workflow:" << std::endl;
    std::cout << "  1. Perform immediate health check of Proof Server" << std::endl;
    bool is_healthy = resilient_client.perform_health_check();
    std::cout << "     Status: " << (is_healthy ? "Healthy ✓" : "Unhealthy ✗") << std::endl;

    std::cout << "\n  2. Monitor resilience statistics:" << std::endl;
    std::cout << "     Total operations: " << resilient_client.get_total_operations() << std::endl;
    std::cout << "     Successful operations: " << resilient_client.get_successful_operations() << std::endl;
    std::cout << "     Failure count: " << resilient_client.get_failure_count() << std::endl;
    std::cout << "     Average retries: " << std::fixed << std::setprecision(2)
              << resilient_client.get_average_retries() << std::endl;

    std::cout << "\n  3. Get detailed statistics as JSON:" << std::endl;
    auto stats = resilient_client.get_statistics_json();
    std::cout << "     " << stats.dump(2) << std::endl;

    std::cout << "\n  4. Query circuit breaker state:" << std::endl;
    std::cout << "     State: " << static_cast<int>(resilient_client.get_circuit_breaker_state())
              << " (0=CLOSED, 1=OPEN, 2=HALF_OPEN)" << std::endl;
}

// ============================================================================
// Example 6: Error recovery with metrics tracking
// ============================================================================

void example_error_recovery_and_metrics()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Example 6: Error Recovery with Metrics Tracking" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    auto base_client = std::make_shared<ProofServerClient>();
    auto config = resilience_util::create_test_config();
    auto resilient_client = ProofServerResilientClient(base_client, config);

    std::cout << "\nMetrics Tracking for Resilient Operations:" << std::endl;

    // Simulate operation with metrics
    ResilienceMetrics metrics;
    metrics.retry_count = 2;
    metrics.total_time_ms = std::chrono::milliseconds(250);
    metrics.succeeded_after_retry = true;
    metrics.first_attempt_time_ms = std::chrono::milliseconds(0);

    std::cout << "\n  1. Operation Metrics:" << std::endl;
    std::cout << "     Retry count: " << metrics.retry_count << std::endl;
    std::cout << "     Total time: " << metrics.total_time_ms.count() << "ms" << std::endl;
    std::cout << "     Succeeded after retry: " << (metrics.succeeded_after_retry ? "yes" : "no") << std::endl;

    std::cout << "\n  2. Formatted Metrics:" << std::endl;
    std::string formatted = resilience_util::format_metrics(metrics);
    std::cout << "     " << formatted << std::endl;

    std::cout << "\n  3. Error During Requests:" << std::endl;
    std::cout << "     - First attempt fails → retry after backoff" << std::endl;
    std::cout << "     - Second attempt fails → retry after longer backoff" << std::endl;
    std::cout << "     - Third attempt succeeds → marked as 'succeeded_after_retry'" << std::endl;

    std::cout << "\n  4. Individual Operation Failures:" << std::endl;
    std::cout << "     If all retries exhausted:" << std::endl;
    std::cout << "       - Exception thrown to caller" << std::endl;
    std::cout << "       - Last error message available via get_last_error()" << std::endl;
    std::cout << "       - Circuit breaker updated on failure threshold" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    std::cout << "\n"
              << std::string(80, '=') << std::endl;
    std::cout << "Midnight SDK - Phase 4C: Proof Server Resilience" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    try
    {
        // Run all examples
        example_resilient_client_setup();
        example_predefined_configurations();
        example_circuit_breaker_states();
        example_exponential_backoff();
        example_health_check_and_monitoring();
        example_error_recovery_and_metrics();

        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "Phase 4C Examples Complete!" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "\nKey Takeaways:" << std::endl;
        std::cout << "  ✓ Exponential backoff prevents server overwhelming" << std::endl;
        std::cout << "  ✓ Circuit breaker prevents cascading failures" << std::endl;
        std::cout << "  ✓ Health checks detect issues early" << std::endl;
        std::cout << "  ✓ Metrics track resilience performance" << std::endl;
        std::cout << "  ✓ Ready for production deployment" << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
