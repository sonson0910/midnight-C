/**
 * indexer-probe.cpp v8
 * Midnight Indexer v4 - Find how to get the first transaction id
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

#include "midnight/network/indexer_client.hpp"
#include "midnight/core/logger.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

struct Endpoints {
    std::string network_name;
    std::string graphql_host;
    std::string graphql_path;
};

static Endpoints get_endpoints(const std::string& net) {
    if (net == "mainnet")
        return {"mainnet", "https://indexer.mainnet.midnight.network", "/api/v4/graphql"};
    if (net == "preview")
        return {"preview", "https://indexer.preview.midnight.network", "/api/v4/graphql"};
    return {"preprod", "https://indexer.preprod.midnight.network", "/api/v4/graphql"};
}

static json raw_graphql(const Endpoints& ep, const std::string& query) {
    httplib::Client cli(ep.graphql_host.c_str());
    cli.set_read_timeout(30);
    json body;
    body["query"] = query;
    auto resp = cli.Post(ep.graphql_path.c_str(), body.dump(), "application/json");
    if (!resp || resp->status != 200) {
        std::ostringstream oss;
        oss << "HTTP " << (resp ? resp->status : 0);
        if (resp) oss << " - " << resp->body.substr(0, 300);
        throw std::runtime_error(oss.str());
    }
    return json::parse(resp->body);
}

static void print_result(const std::string& name, const json& j) {
    if (j.contains("errors")) {
        for (const auto& e : j["errors"])
            std::cout << "  [ERR:" << name << "] " << e.value("message", "") << "\n";
    } else {
        std::cout << "  [OK:" << name << "]\n  " << j["data"].dump(2).substr(0, 2000) << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::string network = (argc > 1) ? argv[1] : "preprod";
    auto ep = get_endpoints(network);

    std::cout << "========================================\n";
    std::cout << "  Midnight Indexer v4 - Find transaction types\n";
    std::cout << "  Network: " << ep.network_name << "\n";
    std::cout << "========================================\n";

    // 1. Block + epoch
    std::cout << "\n--- [1] BLOCK + EPOCH ---\n";
    try {
        print_result("block", raw_graphql(ep, "{ block { hash height timestamp } currentEpochInfo { epochNo durationSeconds elapsedSeconds } }"));
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    // 2. Block type introspection
    std::cout << "\n--- [2] BLOCK TYPE ---\n";
    try {
        auto j = raw_graphql(ep, R"({
            __type(name: "Block") {
                name kind description
                fields { name description type { name kind ofType { name kind } } }
            }
        })");
        print_result("block_type", j);
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    // 3. RegularTransaction type introspection
    std::cout << "\n--- [3] REGULAR TRANSACTION TYPE ---\n";
    try {
        auto j = raw_graphql(ep, R"({
            __type(name: "RegularTransaction") {
                name kind description
                fields { name description type { name kind ofType { name kind } } }
            }
        })");
        print_result("reg_tx", j);
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    // 4. UnshieldedTransaction type
    std::cout << "\n--- [4] UNSHIELDED TRANSACTION TYPE ---\n";
    try {
        auto j = raw_graphql(ep, R"({
            __type(name: "UnshieldedTransaction") {
                name kind description
                fields { name description type { name kind ofType { name kind } } }
            }
        })");
        print_result("unshielded_tx", j);
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    // 5. RelevantTransaction type
    std::cout << "\n--- [5] RELEVANT TRANSACTION TYPE ---\n";
    try {
        auto j = raw_graphql(ep, R"({
            __type(name: "RelevantTransaction") {
                name kind description
                fields { name type { name kind ofType { name kind } } }
            }
        })");
        print_result("relevant_tx", j);
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    // 6. Subscription type (WebSocket)
    std::cout << "\n--- [6] SUBSCRIPTION TYPE ---\n";
    try {
        auto j = raw_graphql(ep, R"({
            __type(name: "Subscription") {
                name kind description
                fields { name description type { name kind ofType { name kind } } }
            }
        })");
        print_result("subscription", j);
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    // 7. SystemTransaction type
    std::cout << "\n--- [7] SYSTEM TRANSACTION TYPE ---\n";
    try {
        auto j = raw_graphql(ep, R"({
            __type(name: "SystemTransaction") {
                name kind description
                fields { name type { name kind ofType { name kind } } }
            }
        })");
        print_result("system_tx", j);
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    // 8. CollapsedMerkleTree type
    std::cout << "\n--- [8] COLLAPSED MERKLE TREE ---\n";
    try {
        auto j = raw_graphql(ep, R"({
            __type(name: "CollapsedMerkleTree") {
                name kind description
                fields { name type { name kind ofType { name kind } } }
            }
        })");
        print_result("merkle", j);
    } catch (const std::exception& e) { std::cout << "  [EXC] " << e.what() << "\n"; }

    std::cout << "\n========================================\n";
    std::cout << "  Complete.\n";
    std::cout << "========================================\n";
    return 0;
}
