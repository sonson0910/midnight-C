#pragma once

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

#include "midnight/wallet/bech32m.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace midnight::wallet {

/**
 * @brief 32-byte public key for shielded transactions.
 *
 * Ported from @midnight-ntwrk/wallet-sdk-address-format:
 *   ShieldedCoinPublicKey class with keyLength = 32
 *
 * Used as the coin public key component in ShieldedAddress.
 * Data is concatenated with encryption public key: coinPk || encPk = 64 bytes
 */
class ShieldedCoinPublicKey {
public:
    /// Key length in bytes (matches SDK: ShieldedCoinPublicKey.keyLength = 32)
    static constexpr size_t kKeyLength = 32;

    /**
     * @brief Construct from raw 32 bytes.
     * @param data Raw key bytes (must be exactly 32 bytes)
     * @throws std::invalid_argument if data.size() != 32
     */
    explicit ShieldedCoinPublicKey(const std::vector<uint8_t>& data);

    /**
     * @brief Construct from hex string (64 hex characters).
     * @param hex 64-character hex string
     * @throws std::invalid_argument if hex is invalid
     */
    explicit ShieldedCoinPublicKey(const std::string& hex);

    /// Default constructor (creates empty/invalid key)
    ShieldedCoinPublicKey() = default;

    /// Copy constructor
    ShieldedCoinPublicKey(const ShieldedCoinPublicKey&) = default;

    /// Move constructor
    ShieldedCoinPublicKey(ShieldedCoinPublicKey&&) = default;

    /// Copy assignment
    ShieldedCoinPublicKey& operator=(const ShieldedCoinPublicKey&) = default;

    /// Move assignment
    ShieldedCoinPublicKey& operator=(ShieldedCoinPublicKey&&) = default;

    /**
     * @brief Get raw bytes
     * @return Const reference to the 32-byte key data
     */
    const std::vector<uint8_t>& bytes() const { return data_; }

    /**
     * @brief Get hex string representation
     * @return 64-character hex string
     */
    std::string to_hex() const;

    /**
     * @brief Validate this key against another
     * @param other Key to compare against
     * @return true if keys are equal
     */
    bool equals(const ShieldedCoinPublicKey& other) const;

    /**
     * @brief Validate hex string pattern
     * @param hex String to validate
     * @return true if hex is valid 64-character hex string
     */
    static bool is_valid_hex(const std::string& hex);

    /**
     * @brief Check if this key is valid (has 32 bytes)
     */
    bool is_valid() const { return data_.size() == kKeyLength; }

private:
    std::vector<uint8_t> data_;
};

/**
 * @brief 32-byte encryption public key for shielded transactions.
 *
 * Ported from @midnight-ntwrk/wallet-sdk-address-format:
 *   ShieldedEncryptionPublicKey class with keyLength = 32
 *
 * Used as the encryption public key component in ShieldedAddress.
 * CRITICAL: Validates exactly 32 bytes for security.
 */
class ShieldedEncryptionPublicKey {
public:
    /// Key length in bytes (matches SDK: ShieldedEncryptionPublicKey.keyLength = 32)
    static constexpr size_t kKeyLength = 32;

    /**
     * @brief Construct from raw 32 bytes.
     * @param data Raw key bytes (must be exactly 32 bytes)
     * @throws std::invalid_argument if data.size() != 32
     */
    explicit ShieldedEncryptionPublicKey(const std::vector<uint8_t>& data);

    /**
     * @brief Construct from hex string (64 hex characters).
     * @param hex 64-character hex string
     * @throws std::invalid_argument if hex is invalid
     */
    explicit ShieldedEncryptionPublicKey(const std::string& hex);

    /// Default constructor (creates empty/invalid key)
    ShieldedEncryptionPublicKey() = default;

    /// Copy constructor
    ShieldedEncryptionPublicKey(const ShieldedEncryptionPublicKey&) = default;

    /// Move constructor
    ShieldedEncryptionPublicKey(ShieldedEncryptionPublicKey&&) = default;

    /// Copy assignment
    ShieldedEncryptionPublicKey& operator=(const ShieldedEncryptionPublicKey&) = default;

    /// Move assignment
    ShieldedEncryptionPublicKey& operator=(ShieldedEncryptionPublicKey&&) = default;

    /**
     * @brief Get raw bytes
     * @return Const reference to the 32-byte key data
     */
    const std::vector<uint8_t>& bytes() const { return data_; }

    /**
     * @brief Get hex string representation
     * @return 64-character hex string
     */
    std::string to_hex() const;

    /**
     * @brief Validate this key against another
     * @param other Key to compare against
     * @return true if keys are equal
     */
    bool equals(const ShieldedEncryptionPublicKey& other) const;

    /**
     * @brief Validate hex string pattern
     * @param hex String to validate
     * @return true if hex is valid 64-character hex string
     */
    static bool is_valid_hex(const std::string& hex);

    /**
     * @brief Check if this key is valid (has 32 bytes)
     */
    bool is_valid() const { return data_.size() == kKeyLength; }

private:
    std::vector<uint8_t> data_;
};

/**
 * @brief Full shielded address with network validation.
 *
 * Ported from @midnight-ntwrk/wallet-sdk-address-format:
 *   ShieldedAddress class
 *
 * Format: Bech32m encoded with:
 *   - MainNet: mn_shield-addr
 *   - Preview: mn_shield-addr_preview
 *   - PreProd: mn_shield-addr_preprod
 *   - etc.
 *
 * Data layout: coinPublicKey (32 bytes) || encryptionPublicKey (32 bytes) = 64 bytes
 */
class ShieldedAddress {
public:
    /**
     * @brief Construct a shielded address from component keys.
     * @param coin_key 32-byte coin public key
     * @param encrypt_key 32-byte encryption public key
     * @param network Network identifier
     * @throws std::invalid_argument if keys are invalid
     */
    ShieldedAddress(
        const ShieldedCoinPublicKey& coin_key,
        const ShieldedEncryptionPublicKey& encrypt_key,
        address::Network network);

    /// Default constructor (creates empty/invalid address)
    ShieldedAddress() = default;

    /**
     * @brief Create from bech32m encoded string.
     * @param address Bech32m encoded shielded address string
     * @param expected_network Network to validate against
     * @return ShieldedAddress if parsing succeeds, std::nullopt otherwise
     */
    static std::optional<ShieldedAddress> from_string(
        const std::string& address,
        address::Network expected_network);

    /**
     * @brief Get bech32m encoded string.
     * @return Encoded address string
     */
    std::string to_string() const;

    /**
     * @brief Get the encoded string (alias for to_string).
     */
    explicit operator std::string() const { return to_string(); }

    /**
     * @brief Access component keys
     * @return Reference to coin public key
     */
    const ShieldedCoinPublicKey& coin_public_key() const { return coin_key_; }

    /**
     * @brief Access component keys
     * @return Reference to encryption public key
     */
    const ShieldedEncryptionPublicKey& encryption_public_key() const { return encrypt_key_; }

    /**
     * @brief Get network
     * @return Network identifier
     */
    address::Network network() const { return network_; }

    /**
     * @brief Check if this address is valid
     */
    bool is_valid() const {
        return coin_key_.is_valid() && encrypt_key_.is_valid();
    }

    /**
     * @brief Compare with another address
     * @param other Address to compare
     * @return true if addresses are equal
     */
    bool equals(const ShieldedAddress& other) const;

    /**
     * @brief Get raw concatenated bytes (coinPk || encPk)
     * @return 64-byte vector
     */
    std::vector<uint8_t> to_bytes() const;

private:
    ShieldedCoinPublicKey coin_key_;
    ShieldedEncryptionPublicKey encrypt_key_;
    address::Network network_ = address::Network::Preview;
    std::string encoded_;
};

} // namespace midnight::wallet
