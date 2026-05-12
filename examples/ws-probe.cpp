/**
 * ws-probe.cpp - Test WebSocket connectivity to Midnight Indexer
 * Uses httplib::ws::WebSocketClient to diagnose connection issues
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <httplib.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace std;

int main() {
    // Midnight requires graphql-transport-ws subprotocol
    httplib::Headers headers = {
        {"Sec-WebSocket-Protocol", "graphql-transport-ws"}
    };

    // Test both URLs: with and without /ws suffix
    const char* url_http = "wss://indexer.preprod.midnight.network/api/v4/graphql";
    const char* url_ws = "wss://indexer.preprod.midnight.network/api/v4/graphql/ws";

    cout << "========================================\n";
    cout << "  Midnight Indexer WebSocket Probe\n";
    cout << "========================================\n";
    cout << "  URL (no /ws): " << url_http << "\n";
    cout << "  URL (with /ws): " << url_ws << "\n\n";

    // Test BOTH URLs
    for (int test = 0; test < 2; test++) {
        const char* url = (test == 0) ? url_http : url_ws;
        cout << "=== TEST " << (test+1) << ": " << url << " ===\n";

        auto client = std::make_shared<httplib::ws::WebSocketClient>(url, headers);
        cout << "  Created WebSocketClient\n";
        cout << "  is_valid(): " << (client->is_valid() ? "true" : "false") << "\n";

        client->set_connection_timeout(30, 0);
        client->set_read_timeout(60, 0);
        client->enable_server_certificate_verification(false);

        cout << "  Connecting...\n";
        bool ok = client->connect();
        cout << "  connect() result: " << (ok ? "SUCCESS" : "FAILED") << "\n";
        cout << "  is_open(): " << (client->is_open() ? "true" : "false") << "\n";

        if (ok) {
            // Send graphql-ws connection_init
            string init_msg = R"({"type":"connection_init","payload":{}})";
            cout << "  Sending connection_init...\n";
            bool sent = client->send(init_msg);
            cout << "  send() result: " << (sent ? "SUCCESS" : "FAILED") << "\n";

            // Read response
            cout << "  Reading response (5 attempts)...\n";
            for (int i = 0; i < 5; i++) {
                this_thread::sleep_for(chrono::seconds(2));
                string msg;
                auto rr = client->read(msg);
                if (rr == httplib::ws::ReadResult::Text) {
                    cout << "  Received: " << msg.substr(0, 300) << "\n";
                    break;
                } else if (rr == httplib::ws::ReadResult::Binary) {
                    cout << "  Received binary: " << msg.size() << " bytes\n";
                } else {
                    cout << "  Attempt " << (i+1) << ": read() returned " << (int)rr << "\n";
                }
            }

            client->close();
            cout << "  Connection closed\n";
        }
        cout << "\n";
    }

    // 2. Check if SSL is properly enabled
    cout << "\n--- [2] SSL Support Check ---\n";
    cout << "  CPPHTTPLIB_OPENSSL_SUPPORT is defined\n";
    cout << "  (httplib::ws::WebSocketClient should support wss://)\n";

    // 3. Try with httplib::Client WebSocket method if available
    cout << "\n--- [3] httplib::Client as WebSocket ---\n";
    httplib::Client http_cli("indexer.preprod.midnight.network", 443);
    http_cli.set_read_timeout(10);
    http_cli.enable_server_certificate_verification(false);

    cout << "  Creating HTTP client for SSL test...\n";
    auto http_result = http_cli.Get("/api/v4/graphql");
    cout << "  HTTP GET test: " << (http_result ? to_string(http_result->status) : "FAILED") << "\n";

    cout << "\n========================================\n";
    cout << "  Done.\n";
    cout << "========================================\n";
    return 0;
}
