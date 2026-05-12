// Quick tool to decode Bech32m address to hex bytes for RPC calls
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

#include "midnight/wallet/bech32m.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <bech32m_address>" << std::endl;
        return 1;
    }

    std::string address = argv[1];
    std::cout << "Decoding address: " << address << std::endl;

    try {
        // Try unshielded address
        auto bytes = midnight::wallet::address::decode_unshielded(address);
        std::cout << "Unshielded address decoded successfully!" << std::endl;
        std::cout << "Hex (32 bytes): 0x";
        for (auto b : bytes) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        std::cout << std::endl;
        std::cout << "Byte count: " << std::dec << bytes.size() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "Not unshielded: " << e.what() << std::endl;
    }

    try {
        // Try dust address
        auto bytes = midnight::wallet::address::decode_dust(address);
        std::cout << "Dust address decoded successfully!" << std::endl;
        std::cout << "Hex: 0x";
        for (auto b : bytes) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        std::cout << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "Not dust: " << e.what() << std::endl;
    }

    try {
        // Try shielded address
        auto data = midnight::wallet::address::decode_shielded(address);
        std::cout << "Shielded address decoded successfully!" << std::endl;
        std::cout << "Coin public key hex: 0x";
        for (auto b : data.coin_public_key) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        std::cout << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "Not shielded: " << e.what() << std::endl;
    }

    std::cerr << "Failed to decode address" << std::endl;
    return 1;
}
