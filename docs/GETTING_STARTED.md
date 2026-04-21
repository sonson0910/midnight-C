# Midnight SDK - Getting Started Guide

## Installation & Setup

### System Requirements

- Windows 10+ / Linux / macOS
- C++20 compatible compiler (gcc 10+, clang 10+, MSVC 2019+)
- CMake 3.20+
- Git (recommended)

### Step 1: Clone/Setup Project

```bash
cd d:\venera\midnight
```

### Step 2: Create Build Directory

```bash
mkdir build
cd build
```

### Step 3: Configure CMake

```bash
# On Windows
cmake -G "Visual Studio 16 2019" -DCMAKE_BUILD_TYPE=Release ..

# On Linux/macOS
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### Step 4: Build Project

```bash
# On Windows
cmake --build . --config Release

# On Linux/macOS
make -j4
```

### Step 5: Install

```bash
cmake --install . --prefix ./install
```

## Using the Library

### Include Headers

```cpp
#include "midnight/core/logger.hpp"
#include "midnight/protocols/mqtt/mqtt_client.hpp"
#include "midnight/blockchain/wallet.hpp"
```

### Linking

When linking your project:

```cmake
find_package(midnight REQUIRED)
target_link_libraries(your_project midnight-core)
```

## Quick Start Examples

### 1. Initialize Logger

```cpp
#include "midnight/core/logger.hpp"

int main() {
    midnight::g_logger->info("Hello Midnight!");
    return 0;
}
```

### 2. Use Config

```cpp
#include "midnight/core/config.hpp"

midnight::g_config->set("device_id", "sensor_001");
std::string device_id = midnight::g_config->get("device_id");
```

### 3. Manage Session

```cpp
#include "midnight/session/session_manager.hpp"

midnight::SessionManager session_mgr;
auto session_id = session_mgr.create_session("device_123");
if (session_mgr.is_valid(session_id)) {
    session_mgr.update_activity(session_id);
}
```

### 4. MQTT Communication

```cpp
#include "midnight/protocols/mqtt/mqtt_client.hpp"

midnight::protocols::mqtt::MqttClient mqtt;
mqtt.connect("broker.example.com", 1883);

if (mqtt.is_connected()) {
    mqtt.publish("sensors/temp", "25.5");
    mqtt.subscribe("commands/action", [](const auto& topic, const auto& msg) {
        std::cout << "Received: " << msg << std::endl;
    });
}
```

### 5. Blockchain Wallet

```cpp
#include "midnight/blockchain/wallet.hpp"

midnight::blockchain::Wallet wallet;
wallet.create("my_secure_passphrase");
std::string address = wallet.get_address();
```

## Troubleshooting

### Build Issues

**Problem**: CMake not found

```bash
# Install CMake
# Windows: Download from cmake.org
# Linux: apt-get install cmake
# macOS: brew install cmake
```

**Problem**: C++20 compiler error

```bash
# Update compiler
# Windows: Update Visual Studio
# Linux: apt-get install g++-10
# macOS: brew install gcc@11
```

### Runtime Issues

**Problem**: Logger not output

```cpp
// Ensure logger is initialized
if (midnight::g_logger) {
    midnight::g_logger->info("Logging works");
}
```

**Problem**: Connection refused (MQTT)

```cpp
// Check broker address and port
// Verify network connectivity
// Check firewall rules
```

## Next Steps

1. Explore examples in `examples/` directory
2. Read [ARCHITECTURE.md](ARCHITECTURE.md) for detailed design
3. Run unit tests: `ctest`
4. Start building your IoT application!

## Support

- Check documentation in `docs/` folder
- Review examples in `examples/` folder
- Create unit tests in `tests/` folder
