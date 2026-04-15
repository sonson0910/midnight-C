# Midnight SDK - Architecture Documentation

## Tổng Quan Kiến Trúc

Midnight SDK được thiết kế theo kiến trúc modular, cho phép các developers có thể sử dụng chỉ những thành phần họ cần.

## Lớp Tầng

### 1. **Core Layer** (Lớp Cốt Lõi)
- Configuration Management
- Logging System
- Session Management
- Base Utilities

### 2. **Protocol Layer** (Lớp Giao Thức)
- MQTT Protocol
- CoAP Protocol
- HTTP/HTTPS
- WebSocket

### 3. **Blockchain Layer** (Lớp Blockchain)
- Chain Management
- Block Management
- Transaction Management
- Wallet Management
- Cardano Adapter

### 4. **Application Layer** (Lớp Ứng Dụng)
- User Applications
- Services
- Business Logic

## Thiết Kế Mẫu

### Pattern 1: Singleton
- Global Logger instance
- Global Config instance
- Chain Manager (có thể singleton)

### Pattern 2: Factory
- Protocol client creation
- Block creation

### Pattern 3: Observer
- Message callbacks (MQTT, WebSocket)
- Event handlers

## Tiếp mục phát triển hạn tiếp theo

### Phase 1: Foundation (Current)
- [x] Cấu trúc dự án
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

```
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
