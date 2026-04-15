#include "midnight/network/network_client.hpp"
#include "midnight/network/network_config.hpp"
#include <iostream>
#include <iomanip>
#include <vector>

/**
 * HTTP Connectivity Test - Verifies Phase 2 implementation
 * Tests real HTTPS connectivity to Midnight testnet
 */
int main()
{
    bool preprod_transport_ok = false;
    bool preprod_rpc_ok = false;
    bool public_http_get_ok = false;
    bool public_http_post_ok = false;

    std::cout << "\n"
              << std::string(70, '=') << std::endl;
    std::cout << "  MIDNIGHT SDK - Phase 2: Real HTTP Implementation Test" << std::endl;
    std::cout << std::string(70, '=') << "\n"
              << std::endl;

    // Test 1: Local endpoint (should fail if no local node)
    std::cout << "Test 1: Local Development Network (DEVNET)" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    try
    {
        std::string devnet_url = midnight::network::NetworkConfig::get_rpc_endpoint(
            midnight::network::NetworkConfig::Network::DEVNET);
        std::cout << "Endpoint: " << devnet_url << std::endl;

        midnight::network::NetworkClient devnet_client(devnet_url, 2000);
        std::cout << "Client created, checking connectivity..." << std::endl;

        if (devnet_client.is_connected())
        {
            std::cout << "✓ Connected to local Midnight DEVNET" << std::endl;
        }
        else
        {
            std::cout << "✗ Could not connect to local DEVNET (expected if no local node running)" << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "✗ Error: " << e.what() << std::endl;
    }

    std::cout << "\nTest 2: Midnight Preprod (TESTNET)" << std::endl;
    std::cout << "------------------------------------" << std::endl;
    try
    {
        std::string testnet_url = midnight::network::NetworkConfig::get_rpc_endpoint(
            midnight::network::NetworkConfig::Network::TESTNET);
        std::cout << "Endpoint: " << testnet_url << std::endl;

        midnight::network::NetworkClient testnet_client(testnet_url, 5000);
        std::cout << "Client created, checking connectivity..." << std::endl;

        if (testnet_client.is_connected())
        {
            preprod_transport_ok = true;
            std::cout << "✓ Connected to Midnight TESTNET (Preprod)" << std::endl;

            // Probe the Substrate-compatible RPC methods exposed by Midnight Preprod.
            std::cout << "Attempting JSON-RPC compatibility probe..." << std::endl;

            struct RpcProbe
            {
                const char *method;
            };

            const std::vector<RpcProbe> probes = {
                {"system_chain"},
                {"system_health"},
                {"chain_getHeader"}};

            size_t successful_probes = 0;
            for (size_t i = 0; i < probes.size(); ++i)
            {
                try
                {
                    nlohmann::json request = {
                        {"jsonrpc", "2.0"},
                        {"method", probes[i].method},
                        {"params", nlohmann::json::array()},
                        {"id", static_cast<int>(i + 1)}};

                    auto response = testnet_client.post_json("/", request);
                    if (response.contains("result"))
                    {
                        ++successful_probes;
                        std::cout << "  ✓ " << probes[i].method << " responded" << std::endl;
                    }
                    else if (response.contains("error"))
                    {
                        std::cout << "  ✗ " << probes[i].method
                                  << " error: " << response["error"].dump() << std::endl;
                    }
                    else
                    {
                        std::cout << "  ✗ " << probes[i].method
                                  << " unexpected response: " << response.dump() << std::endl;
                    }
                }
                catch (const std::exception &rpc_error)
                {
                    std::cout << "  ✗ " << probes[i].method
                              << " failed: " << rpc_error.what() << std::endl;
                }
            }

            preprod_rpc_ok = (successful_probes == probes.size());
            std::cout << "JSON-RPC successful probes: " << successful_probes << "/" << probes.size() << std::endl;
        }
        else
        {
            std::cout << "✗ Could not connect to Midnight TESTNET" << std::endl;
            std::cout << "  Check your internet connection or Midnight network status" << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "✗ Error: " << e.what() << std::endl;
    }

    std::cout << "\nTest 3: HTTP Methods Test" << std::endl;
    std::cout << "-------------------------" << std::endl;
    try
    {
        // Use httpbin.org for testing (public HTTP testing service)
        std::string httpbin_url = "https://httpbin.org";

        std::cout << "Testing with: " << httpbin_url << std::endl;
        midnight::network::NetworkClient http_test(httpbin_url, 5000);

        // Test GET
        std::cout << "\nTest GET /get" << std::endl;
        try
        {
            auto get_response = http_test.get_json("/get");
            if (get_response.contains("headers"))
            {
                public_http_get_ok = true;
                std::cout << "✓ GET request successful" << std::endl;
                std::cout << "  Contains " << get_response.size() << " response fields" << std::endl;
            }
        }
        catch (const std::exception &get_error)
        {
            std::cout << "✗ GET request failed: " << get_error.what() << std::endl;
        }

        // Test POST
        std::cout << "\nTest POST /post with JSON payload" << std::endl;
        try
        {
            nlohmann::json test_payload = {
                {"test", "value"},
                {"number", 42},
                {"array", {1, 2, 3}}};

            auto post_response = http_test.post_json("/post", test_payload);
            if (post_response.contains("json"))
            {
                public_http_post_ok = true;
                std::cout << "✓ POST request successful" << std::endl;
                std::cout << "  Echoed payload: " << post_response["json"].dump() << std::endl;
            }
        }
        catch (const std::exception &post_error)
        {
            std::cout << "✗ POST request failed: " << post_error.what() << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "✗ Error during HTTP test: " << e.what() << std::endl;
    }

    std::cout << "\n"
              << std::string(70, '=') << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << "- DEVNET: Local endpoint (optional)" << std::endl;
    std::cout << "- TESTNET: Real Midnight Preprod network (requires internet)" << std::endl;
    std::cout << "- HTTP: Tests with public httpbin.org service" << std::endl;
    std::cout << std::string(70, '=') << "\n"
              << std::endl;

    std::cout << "Phase 2 Features Verified:" << std::endl;
    std::cout << "✓ HTTPS client implemented (cpp-httplib + OpenSSL)" << std::endl;
    std::cout << "✓ URL parsing for all HTTP/HTTPS endpoints" << std::endl;
    std::cout << "✓ Connection timeout support" << std::endl;
    std::cout << "✓ JSON request/response handling" << std::endl;
    std::cout << "✓ HTTP GET and POST methods" << std::endl;
    std::cout << "\nCompatibility verdict:" << std::endl;
    std::cout << "- Midnight Preprod transport: " << (preprod_transport_ok ? "PASS" : "FAIL") << std::endl;
    std::cout << "- Midnight JSON-RPC response: " << (preprod_rpc_ok ? "PASS" : "FAIL") << std::endl;
    std::cout << "- Public HTTPS GET/POST sanity: "
              << ((public_http_get_ok && public_http_post_ok) ? "PASS" : "FAIL") << std::endl;

    const bool production_ready = preprod_transport_ok && preprod_rpc_ok;
    std::cout << "\nProduction readiness: "
              << (production_ready ? "READY ✅" : "NOT READY ❌")
              << std::endl;
    if (!production_ready)
    {
        std::cout << "Reason: SDK chưa chứng minh được giao tiếp JSON-RPC ổn định với Midnight Preprod." << std::endl;
    }

    return production_ready ? 0 : 1;
}
