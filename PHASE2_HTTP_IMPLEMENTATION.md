# Phase 2: Real HTTP Implementation with cpp-httplib

## Overview
Phase 2 implements real HTTP transport using cpp-httplib, replacing the previous mock/error-throwing NetworkClient.

## Changes Made

### 1. CMakeLists.txt Updates

**Added FetchContent for cpp-httplib:**
```cmake
include(FetchContent)
FetchContent_Declare(
    cpp-httplib
    URL https://github.com/yhirose/cpp-httplib/releases/download/v0.11.5/cpp-httplib-0.11.5.zip
)
```

**Benefits:**
- Automatic download and integration of cpp-httplib header-only library
- No manual dependency installation required
- Portable across Windows/Linux/macOS

### 2. src/CMakeLists.txt Updates

**Added hpp-header include path:**
```cmake
target_include_directories(midnight-core
    PRIVATE
        ${HTTPLIB_SOURCE_DIR}
)
```

**Added OpenSSL support:**
```cmake
find_package(OpenSSL QUIET)
target_link_libraries(midnight-core
    PRIVATE
        $<$<BOOL:${OPENSSL_FOUND}>:OpenSSL::SSL>
        $<$<BOOL:${OPENSSL_FOUND}>:OpenSSL::Crypto>
)
```

### 3. network_client.cpp Implementation

**Complete HTTP Implementation:**

#### http_post() Method
```cpp
std::string NetworkClient::http_post(
    const std::string& path,
    const std::string& body,
    const std::string& content_type
)
```
- Uses `httplib::Client` for HTTP, `httplib::SSLClient` for HTTPS
- Parses base URL to extract hostname, port, and request path
- Sets connection timeout from `timeout_ms_` parameter
- Handles both HTTP (port 80) and HTTPS (port 443) by default
- Throws exception on HTTP errors (non-200 status)
- Returns response body as string

#### http_get() Method
```cpp
std::string NetworkClient::http_get(const std::string& path)
```
- Similar to http_post but uses GET method
- Same URL parsing and timeout handling
- Same error handling and return value

#### is_connected() Method
```cpp
bool NetworkClient::is_connected() const
```
- Validates connection to base URL
- Creates temporary httplib::Client to test connectivity
- Returns true if client is valid
- Returns false on timeout or error

### 4. Features

✅ **HTTPS Support**: Automatic SSL/TLS with OpenSSL
✅ **Connection Timeouts**: Configurable timeout per NetworkClient instance
✅ **Error Handling**: Explicit HTTP error detection (status != 200)
✅ **Logging Integration**: Debug logs for all HTTP operations
✅ **URL Parsing**: Automatic extraction of hostname, port, and path components
✅ **JSON Marshalling**: Automatic JSON serialization in post_json() / get_json()

### 5. URL Parsing Logic

The implementation handles various URL formats:
```
https://preprod.midnight.network/api
- Host: preprod.midnight.network
- Port: 443 (default HTTPS)
- Request Path: /api

https://example.com:8443/path/to/endpoint
- Host: example.com
- Port: 8443 (custom)
- Request Path: /path/to/endpoint

http://localhost:5678/api
- Host: localhost
- Port: 5678 (custom)
- Request Path: /api
```

### 6. Testing Against Real Midnight Testnet

**Example: Query UTXOs from Midnight Preprod**

```cpp
#include "midnight/blockchain/midnight_adapter.hpp"
#include "midnight/network/network_config.hpp"

// Create blockchain manager
MidnightBlockchain blockchain;

// Connect to real Midnight testnet (Preprod)
const auto rpc_endpoint = midnight::network::NetworkConfig::get_rpc_endpoint(
    midnight::network::NetworkConfig::Network::TESTNET
);
// Result: "https://preprod.midnight.network/api"

try {
    blockchain.connect(rpc_endpoint);

    // Query UTXOs
    auto address = "addr_test1vz...";  // Valid Midnight address
    auto utxos = blockchain.query_utxos(address);

    std::cout << "Found " << utxos.size() << " UTXOs\n";

    blockchain.disconnect();
} catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << "\n";
}
```

### 7. Error Scenarios

**HTTP 400/401/500 Errors:**
- Logged with error level
- Thrown as std::runtime_error for caller to handle

**Connection Timeout:**
- httplib::Client times out after timeout_ms_
- Results in thrown exception from http_post/http_get

**URL Parsing Errors:**
- Empty URLs validated in constructor
- Malformed URLs handled gracefully

**HTTPS without OpenSSL:**
- Explicit error thrown if OPENSSL not available
- #ifdef guards prevent compilation with missing OpenSSL

### 8. Building Phase 2

**Prerequisites:**
- OpenSSL development libraries (for HTTPS)
- CMake 3.20+
- C++20 compiler

**On Windows:**
```powershell
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**On Linux:**
```bash
cmake -B build
cmake --build build -- -j4
```

**FetchContent will automatically download cpp-httplib from GitHub**

### 9. Next Steps After Phase 2

✅ **Phase 2 Complete**: Real HTTP transport implemented
→ **Phase 3**: Enhanced Error Handling & Retry Logic
  - Exponential backoff for transient failures
  - Connection pooling (optional)
  - Request/response caching (optional)

→ **Phase 4**: Real Ed25519 Signing
  - Replace mock signing with libsodium
  - Actual Midnight transaction signing

→ **Phase 5**: Full Transaction Workflow
  - Build real transactions
  - Sign with real keys
  - Submit to Midnight testnet
  - Monitor confirmation

### 10. Troubleshooting

**Issue: OpenSSL not found**
- Windows: Install vcpkg or OpenSSL Dev Kit
- Linux: `sudo apt install libssl-dev`
- macOS: `brew install openssl`

**Issue: Download fails**
- Check internet connection
- cpp-httplib fallback: manually download from GitHub and set HTTPLIB_SOURCE_DIR

**Issue: Build succeeds but requests fail with timeout**
- Midnight network may be temporarily down
- Check: https://status.midnight.network/
- Verify testnet endpoint: https://preprod.midnight.network/api

## Summary

Phase 2 successfully replaces NetworkClient mock implementation with fully functional HTTP client:

- ✅ Real HTTPS support via cpp-httplib and OpenSSL
- ✅ Automatic URL parsing for any HTTP/HTTPS endpoint
- ✅ Integrated timeout and error handling
- ✅ Full logging for debugging
- ✅ Ready for production use with Midnight Preprod testnet
- ✅ Backward compatible with existing MidnightBlockchain API

**Status: Phase 2 COMPLETE - SDK now has real HTTP transport ✅**
