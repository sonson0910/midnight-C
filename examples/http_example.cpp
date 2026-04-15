#include "midnight/protocols/http/http_client.hpp"
#include "midnight/core/logger.hpp"
#include <iostream>

int main()
{
    midnight::g_logger->info("HTTP Example Started");

    midnight::protocols::http::HttpClient http_client;

    // Perform GET request
    auto response = http_client.get("https://api.example.com/data");

    std::cout << "Status Code: " << response.status_code << std::endl;
    std::cout << "Response Body: " << response.body << std::endl;

    // Perform POST request
    response = http_client.post("https://api.example.com/data", "{\"key\": \"value\"}");

    if (response.status_code == 200)
    {
        midnight::g_logger->info("HTTP request successful");
    }
    else
    {
        midnight::g_logger->warn("HTTP request returned status: " + std::to_string(response.status_code));
    }

    midnight::g_logger->info("HTTP Example Finished");
    return 0;
}
