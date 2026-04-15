# Midnight SDK - C++ IoT & Blockchain Infrastructure Library

**Midnight** là một thư viện C++20 toàn diện được xây dựng để hỗ trợ hạ tầng IoT kết hợp với blockchain Midnight.

## Tính Năng Chính

### 1. **Hỗ Trợ Giao Thức Đa Nền Tảng**
- **MQTT**: Giao tiếp nhẹ nhàng cho thiết bị IoT
- **CoAP**: Constrained Application Protocol cho thiết bị tài nguyên hạn chế
- **HTTP/HTTPS**: Giao tiếp RESTful tiêu chuẩn
- **WebSocket**: Kết nối hai chiều thời gian thực

### 2. **Hỗ Trợ Blockchain Midnight**
- Quản lý chuỗi khối Midnight
- Quản lý ví tiền điện tử với HD key derivation
- Xây dựng và ký giao dịch
- Quản lý UTXO và multi-assets
- Tích hợp phân tán và on-chain operations

### 3. **Quản Lý Phiên & Cấu Hình**
- Session manager cho IoT devices
- Cấu hình linh hoạt
- Logger tích hợp

## Cấu Trúc Dự Án

```
midnight/
├── CMakeLists.txt           # Cấu hình CMake chính
├── include/midnight/
│   ├── core/               # Thành phần cốt lõi
│   ├── protocols/          # Các giao thức (MQTT, CoAP, HTTP, WebSocket)
│   ├── blockchain/         # Midnight blockchain SDK
│   └── session/            # Quản lý phiên
├── src/                    # Triển khai source
├── examples/               # Ví dụ sử dụng (MQTT, Blockchain, HTTP)
├── tests/                  # Kiểm thử
├── docs/                   # Tài liệu
├── README.md               # Tài liệu này
└── MIDNIGHT_BLOCKCHAIN.md  # Hướng dẫn blockchain
```

## Yêu Cầu

- C++20 compatible compiler (gcc 11+, clang 12+, MSVC 2019+)
- CMake 3.20+
- Optional Dependencies:
  - nlohmann_json (JSON support)
  - fmt library (formatted output)

## Build Project

```bash
# Create build directory
mkdir build && cd build

# Configure CMake
cmake -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_MQTT=ON \
      -DENABLE_COAP=ON \
      -DENABLE_HTTP=ON \
      -DENABLE_WEBSOCKET=ON \
      -DENABLE_BLOCKCHAIN=ON \
      -DBUILD_TESTS=ON \
      -DBUILD_EXAMPLES=ON ..

# Build
cmake --build . --config Release

# Install (optional)
cmake --install .
```

## Chạy Examples

```bash
# Compile examples
cmake --build . --target midnight-examples

# MQTT example
./bin/mqtt_example

# Blockchain example
./bin/blockchain_example

# HTTP example
./bin/http_example

# Kiểm tra tương thích/giao tiếp với Midnight Preprod
# Trả về exit code 0 khi đủ điều kiện READY cho production
./bin/http_connectivity_test
```

## Chạy Tests

```bash
# Build tests
cmake --build . --target midnight-tests

# Run tests
ctest --output-on-failure
```

## Quick Start - Blockchain

### 1. Initialize Blockchain

```cpp
#include "midnight/blockchain/midnight_adapter.hpp"

// Setup protocol parameters
midnight::blockchain::ProtocolParams params;
params.min_fee_a = 44;
params.min_fee_b = 155381;

// Create blockchain manager
midnight::blockchain::MidnightBlockchain blockchain;
blockchain.initialize("preprod", params);
blockchain.connect("https://rpc.preprod.midnight.network");
```

### 2. Create Wallet

```cpp
#include "midnight/blockchain/wallet.hpp"

midnight::blockchain::Wallet wallet;
wallet.create_from_mnemonic("mnemonic words here...", "");

std::string address = wallet.get_address();
```

### 3. Build Transaction

```cpp
std::vector<midnight::blockchain::UTXO> utxos;
// Populate UTXOs...

auto result = blockchain.build_transaction(
    utxos,
    {{"recipient_address", 2000000}},
    address  // change address
);
```

### 4. Sign & Submit

```cpp
auto signed = blockchain.sign_transaction(
    result.result,
    private_key_hex
);

auto submitted = blockchain.submit_transaction(signed.result);
if (submitted.success) {
    std::cout << "TX ID: " << submitted.result << std::endl;
}
```

## Mục Tiêu Phát Triển

- ✅ Thiết lập cấu trúc dự án
- ✅ Core components (Config, Logger, SessionManager)
- ✅ Protocol clients (MQTT, CoAP, HTTP, WebSocket)
- ✅ Blockchain components (Transaction, Wallet, MidnightBlockchain)
- ⏳ Triển khai đầy đủ các giao thức với thư viện thư ba
- ⏳ Thêm mã hóa và các tính năng bảo mật
- ⏳ Hỗ trợ multi-sig transactions
- ⏳ On-chain smart contract interop

## Documentation

- [MIDNIGHT_BLOCKCHAIN.md](MIDNIGHT_BLOCKCHAIN.md) - Blockchain API details
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - System architecture
- [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) - Getting started guide

## Giấy Phép

Apache License 2.0

## Liên Hệ

**Dự án**: Midnight SDK - Blockchain-enabled IoT Infrastructure

**Repository**: https://github.com/sonson0910/midnight-C
