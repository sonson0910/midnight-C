# Midnight SDK - Architecture Documentation

## Architecture Overview

Midnight SDK is designed with a modular architecture, allowing developers to use only the components they need.

## Layers

### 1. **Core Layer**

- Configuration Management
- Logging System
- Session Management
- Base Utilities

### 2. **Protocol Layer**

- MQTT Protocol
- CoAP Protocol
- HTTP/HTTPS
- WebSocket

### 3. **Blockchain Layer**

- Chain Management
- Block Management
- Transaction Management
- Wallet Management
- Cardano Adapter

### 4. **Application Layer**

- User Applications
- Services
- Business Logic

## Design Patterns

### Pattern 1: Singleton

- Global Logger instance
- Global Config instance
- Chain Manager (can be singleton)

### Pattern 2: Factory

- Protocol client creation
- Block creation

### Pattern 3: Observer

- Message callbacks (MQTT, WebSocket)
- Event handlers

## Next Development Milestones

### Phase 1: Foundation (Current)

- [x] Project structure
- [x] Header files
- [x] Stub implementations
- [ ] Unit tests

### Phase 2: Core Implementation

- [ ] MQTT protocol full implementation
- [ ] CoAP protocol full implementation
- [ ] HTTP client implementation
- [ ] WebSocket implementation

### Phase 3: Blockchain Features

- [ ] Complete block validation
- [ ] Transaction hashing (SHA256)
- [ ] Wallet private key management
- [ ] Cardano integration

### Phase 4: Advanced Features

- [ ] Encryption/Decryption
- [ ] IoT device authentication
- [ ] Blockchain smart contracts
- [ ] Multi-chain support

## Dependencies

### Required

- C++20 compiler
- CMake 3.20+

### Optional

- OpenSSL (for HTTPS)
- libmosquitto or paho-mqtt (for MQTT)
- libcoap (for CoAP)
- nlohmann_json (for JSON)
- fmt (for formatting)

## File Structure Convention

```text
midnight/
├── include/midnight/
│   ├── <module>/
│   │   └── *.hpp
├── src/
│   ├── <module>/
│   │   └── *.cpp
│   └── CMakeLists.txt
├── examples/
│   ├── *.cpp
│   └── CMakeLists.txt
└── tests/
    ├── *.cpp
    └── CMakeLists.txt
```

## Naming Conventions

- **Namespaces**: `midnight::*`
- **Classes**: PascalCase (e.g., `MqttClient`)
- **Functions/Methods**: snake_case (e.g., `send_request()`)
- **Constants**: UPPER_SNAKE_CASE (e.g., `MAX_RETRIES`)
- **Member variables**: snake_case with trailing underscore (e.g., `connected_`)

## Build Configuration

### Default Configuration

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### Debug Configuration

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

### Custom Installation

```bash
cmake -DCMAKE_INSTALL_PREFIX=/custom/path ..
```
