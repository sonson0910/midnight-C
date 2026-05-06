/**
 * websocket-test.cpp
 * Test WebSocket connection to Midnight Indexer
 * 
 * Usage:
 *   websocket-test.exe
 */

#include "midnight/network/graphql_subscription.hpp"
#include "midnight/core/logger.hpp"
#include <iostream>
#include <thread>
#include <atomic>

using json = nlohmann::json;

int main() {
    std::cout << "=== WebSocket Test to Midnight Indexer ===\n\n";
    
    // Test endpoint: wss://indexer.preview.midnight.network/api/v4/graphql/ws
    std::string ws_url = "wss://indexer.preview.midnight.network/api/v4/graphql/ws";
    std::cout << "Connecting to: " << ws_url << "\n\n";
    
    // Create subscription client
    midnight::network::GraphQLSubscriptionClient client(ws_url);
    
    std::atomic<bool> connected{false};
    std::atomic<int> message_count{0};
    
    // Set up callbacks
    client.set_connection_callback([&connected](bool success, const std::string& msg) {
        if (success) {
            std::cout << "[CONNECTED] " << msg << "\n";
            connected = true;
        } else {
            std::cout << "[DISCONNECTED] " << msg << "\n";
            connected = false;
        }
    });
    
    client.set_message_callback([&message_count](const json& msg) {
        message_count++;
        std::cout << "[MSG #" << message_count << "] ";
        if (msg.contains("type")) {
            std::cout << "type=" << msg["type"].get<std::string>();
        }
        if (msg.contains("id")) {
            std::cout << " id=" << msg["id"].get<std::string>();
        }
        std::cout << "\n";
        
        // Print payload if present
        if (msg.contains("payload")) {
            auto& payload = msg["payload"];
            if (payload.contains("data")) {
                std::cout << "  Data keys: ";
                auto& data = payload["data"];
                if (data.is_object()) {
                    for (auto& [k, v] : data.items()) {
                        std::cout << k << " ";
                    }
                    std::cout << "\n";
                }
            }
            if (payload.contains("errors")) {
                std::cout << "  Errors: " << payload["errors"].dump() << "\n";
            }
        }
    });
    
    // Connect
    std::cout << "Starting connection...\n";
    if (!client.start()) {
        std::cout << "Failed to start WebSocket client\n";
        return 1;
    }
    
    // Wait for connection
    for (int i = 0; i < 30 && !connected; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Waiting for connection... " << i << "s\n";
    }
    
    if (!connected) {
        std::cout << "\nConnection timeout!\n";
        client.stop();
        return 1;
    }
    
    // Subscribe to new blocks
    std::cout << "\nSubscribing to new blocks...\n";
    std::string sub_id = "blocks-sub";
    client.subscribe(sub_id, R"(
        subscription {
            block {
                height
                hash
                timestamp
            }
        }
    )");
    
    // Listen for a few messages
    std::cout << "Listening for messages (30 seconds)...\n";
    for (int i = 0; i < 30; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (i % 5 == 0) {
            std::cout << "  Still listening... " << i << "s (messages: " << message_count << ")\n";
        }
    }
    
    // Unsubscribe
    std::cout << "\nUnsubscribing...\n";
    client.unsubscribe(sub_id);
    
    // Stop
    std::cout << "Stopping client...\n";
    client.stop();
    
    std::cout << "\n=== Test Complete ===\n";
    std::cout << "Messages received: " << message_count << "\n";
    
    return 0;
}
