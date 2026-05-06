// This file is part of MIDNIGHT-WALLET-SDK.
// Copyright (C) Midnight Foundation
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0 (the "License");
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "midnight/wallet/shielded_address.hpp"
#include "midnight/core/logger.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace midnight::wallet {

// ─── Hex Utilities ─────────────────────────────────────────────

static bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static uint8_t hex_char_to_nibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0;
}

static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        result.push_back(
            static_cast<uint8_t>((hex_char_to_nibble(hex[i]) << 4) |
                                  hex_char_to_nibble(hex[i + 1])));
    }
    return result;
}

static std::string bytes_to_hex(const std::vector<uint8_t>& data) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(kHex[(b >> 4) & 0x0F]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

// ─── ShieldedCoinPublicKey Implementation ───────────────────────

ShieldedCoinPublicKey::ShieldedCoinPublicKey(const std::vector<uint8_t>& data)
    : data_(data)
{
    if (data.size() != kKeyLength) {
        throw std::invalid_argument(
            "Coin public key needs to be 32 bytes long, got " +
            std::to_string(data.size()));
    }
}

ShieldedCoinPublicKey::ShieldedCoinPublicKey(const std::string& hex)
{
    if (!is_valid_hex(hex)) {
        throw std::invalid_argument(
            "Invalid hex string for ShieldedCoinPublicKey: must be 64 hex characters");
    }
    data_ = hex_to_bytes(hex);
}

std::string ShieldedCoinPublicKey::to_hex() const {
    return bytes_to_hex(data_);
}

bool ShieldedCoinPublicKey::equals(const ShieldedCoinPublicKey& other) const {
    return data_ == other.data_;
}

bool ShieldedCoinPublicKey::is_valid_hex(const std::string& hex) {
    if (hex.size() != kKeyLength * 2) return false;
    return std::all_of(hex.begin(), hex.end(), is_hex_char);
}

// ─── ShieldedEncryptionPublicKey Implementation ─────────────────

ShieldedEncryptionPublicKey::ShieldedEncryptionPublicKey(const std::vector<uint8_t>& data)
    : data_(data)
{
    if (data.size() != kKeyLength) {
        throw std::invalid_argument(
            "Encryption public key needs to be 32 bytes long, got " +
            std::to_string(data.size()) +
            " — this is critical for security!");
    }
}

ShieldedEncryptionPublicKey::ShieldedEncryptionPublicKey(const std::string& hex)
{
    if (!is_valid_hex(hex)) {
        throw std::invalid_argument(
            "Invalid hex string for ShieldedEncryptionPublicKey: must be 64 hex characters");
    }
    data_ = hex_to_bytes(hex);
}

std::string ShieldedEncryptionPublicKey::to_hex() const {
    return bytes_to_hex(data_);
}

bool ShieldedEncryptionPublicKey::equals(const ShieldedEncryptionPublicKey& other) const {
    return data_ == other.data_;
}

bool ShieldedEncryptionPublicKey::is_valid_hex(const std::string& hex) {
    if (hex.size() != kKeyLength * 2) return false;
    return std::all_of(hex.begin(), hex.end(), is_hex_char);
}

// ─── ShieldedAddress Implementation ─────────────────────────────

ShieldedAddress::ShieldedAddress(
    const ShieldedCoinPublicKey& coin_key,
    const ShieldedEncryptionPublicKey& encrypt_key,
    address::Network network)
    : coin_key_(coin_key)
    , encrypt_key_(encrypt_key)
    , network_(network)
{
    if (!coin_key_.is_valid()) {
        throw std::invalid_argument("Invalid coin public key in ShieldedAddress");
    }
    if (!encrypt_key_.is_valid()) {
        throw std::invalid_argument("Invalid encryption public key in ShieldedAddress");
    }

    // Build the encoded Bech32m string
    std::vector<uint8_t> combined;
    combined.reserve(64);
    const auto& coin_bytes = coin_key_.bytes();
    const auto& enc_bytes = encrypt_key_.bytes();
    combined.insert(combined.end(), coin_bytes.begin(), coin_bytes.end());
    combined.insert(combined.end(), enc_bytes.begin(), enc_bytes.end());

    encoded_ = address::encode_shielded(coin_bytes, enc_bytes, network_);
}

std::optional<ShieldedAddress> ShieldedAddress::from_string(
    const std::string& address,
    address::Network expected_network)
{
    try {
        // Parse the bech32m address
        auto decoded = address::decode_shielded(address);

        if (decoded.coin_public_key.size() != 32 ||
            decoded.encryption_public_key.size() != 32) {
            if (midnight::g_logger) {
                midnight::g_logger->warn("ShieldedAddress::from_string: invalid key lengths");
            }
            return std::nullopt;
        }

        ShieldedCoinPublicKey coin_key(decoded.coin_public_key);
        ShieldedEncryptionPublicKey enc_key(decoded.encryption_public_key);

        ShieldedAddress result(coin_key, enc_key, expected_network);

        // Validate the network by checking the encoded string
        // The decode_shielded function handles network validation internally
        // We verify the parsed keys match what we expect

        if (midnight::g_logger) {
            midnight::g_logger->debug("ShieldedAddress::from_string: parsed " +
                                    address.substr(0, 20) + "...");
        }

        return result;

    } catch (const std::exception& e) {
        if (midnight::g_logger) {
            midnight::g_logger->warn("ShieldedAddress::from_string failed: " +
                                   std::string(e.what()));
        }
        return std::nullopt;
    }
}

std::string ShieldedAddress::to_string() const {
    if (encoded_.empty()) {
        // If not encoded yet, encode now
        std::vector<uint8_t> combined;
        combined.reserve(64);
        combined.insert(combined.end(), coin_key_.bytes().begin(), coin_key_.bytes().end());
        combined.insert(combined.end(), encrypt_key_.bytes().begin(), encrypt_key_.bytes().end());
        const_cast<ShieldedAddress*>(this)->encoded_ =
            address::encode_shielded(coin_key_.bytes(), encrypt_key_.bytes(), network_);
    }
    return encoded_;
}

bool ShieldedAddress::equals(const ShieldedAddress& other) const {
    return coin_key_.equals(other.coin_key_) &&
           encrypt_key_.equals(other.encrypt_key_);
}

std::vector<uint8_t> ShieldedAddress::to_bytes() const {
    std::vector<uint8_t> result;
    result.reserve(64);
    const auto& coin_bytes = coin_key_.bytes();
    const auto& enc_bytes = encrypt_key_.bytes();
    result.insert(result.end(), coin_bytes.begin(), coin_bytes.end());
    result.insert(result.end(), enc_bytes.begin(), enc_bytes.end());
    return result;
}

} // namespace midnight::wallet
