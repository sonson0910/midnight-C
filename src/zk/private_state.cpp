#include "midnight/zk/private_state.hpp"
#include <fmt/format.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <sodium.h>
#include <openssl/sha.h>

namespace midnight::zk
{

    // ============================================================================
    // Constants for secret encryption (XSalsa20-Poly1305)
    // ============================================================================
    static constexpr size_t ENCRYPTED_KEY_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 24;
    static constexpr size_t MAC_SIZE = 16;

    // Derive per-secret encryption key using SHA256-based key derivation.
    // For encryption: generates a new random nonce (stored with ciphertext).
    // For decryption: uses the stored nonce from ciphertext.
    // Uses the secret name as part of the input for domain separation.
    static void derive_key_with_nonce(
        const std::vector<uint8_t> &master_key,
        const std::string &name,
        const std::vector<uint8_t> &nonce,
        uint8_t *derived_key_out)
    {
        // Derive: key = SHA256(master_key || nonce || name)
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, master_key.data(), master_key.size());
        SHA256_Update(&ctx, nonce.data(), nonce.size());
        SHA256_Update(&ctx, name.data(), name.size());
        SHA256_Final(derived_key_out, &ctx);
    }

    static std::vector<uint8_t> derive_encryption_key(
        const std::vector<uint8_t> &master_key,
        const std::string &name,
        std::vector<uint8_t> &nonce_out)
    {
        nonce_out.resize(NONCE_SIZE);
        randombytes_buf(nonce_out.data(), NONCE_SIZE);

        std::vector<uint8_t> derived_key(ENCRYPTED_KEY_SIZE);
        derive_key_with_nonce(master_key, name, nonce_out, derived_key.data());
        return derived_key;
    }

    static std::vector<uint8_t> derive_decryption_key(
        const std::vector<uint8_t> &master_key,
        const std::string &name,
        const std::vector<uint8_t> &stored_nonce)
    {
        std::vector<uint8_t> derived_key(ENCRYPTED_KEY_SIZE);
        derive_key_with_nonce(master_key, name, stored_nonce, derived_key.data());
        return derived_key;
    }

    static std::vector<uint8_t> get_master_key()
    {
        std::vector<uint8_t> master_key(ENCRYPTED_KEY_SIZE, 0);
        const char* env_master = std::getenv("MIDNIGHT_SECRET_MASTER_KEY");
        if (env_master != nullptr && std::strlen(env_master) >= 64)
        {
            std::vector<uint8_t> decoded;
            decoded.reserve(32);
            for (size_t i = 0; i + 1 < 64 && env_master[i] && env_master[i + 1]; i += 2)
            {
                char hex_byte[3] = {env_master[i], env_master[i + 1], '\0'};
                decoded.push_back(static_cast<uint8_t>(std::strtoul(hex_byte, nullptr, 16)));
            }
            if (decoded.size() >= ENCRYPTED_KEY_SIZE)
            {
                std::memcpy(master_key.data(), decoded.data(), ENCRYPTED_KEY_SIZE);
                return master_key;
            }
        }
        randombytes_buf(master_key.data(), ENCRYPTED_KEY_SIZE);
        return master_key;
    }

    // ============================================================================
    // SecretKeyStore Implementation
    // ============================================================================

    bool SecretKeyStore::store_secret(
        const std::string &name,
        const std::vector<uint8_t> &data)
    {
        if (name.empty() || data.empty())
        {
            return false;
        }

        if (sodium_init() < 0)
        {
            return false;
        }

        std::vector<uint8_t> master_key = get_master_key();
        std::vector<uint8_t> nonce;
        std::vector<uint8_t> derived_key = derive_encryption_key(master_key, name, nonce);

        std::vector<uint8_t> ciphertext(data.size() + MAC_SIZE);
        if (crypto_secretbox_easy(ciphertext.data(),
                                  data.data(), data.size(),
                                  nonce.data(),
                                  derived_key.data()) != 0)
        {
            return false;
        }

        std::vector<uint8_t> encrypted;
        encrypted.reserve(nonce.size() + ciphertext.size());
        encrypted.insert(encrypted.end(), nonce.begin(), nonce.end());
        encrypted.insert(encrypted.end(), ciphertext.begin(), ciphertext.end());

        secrets_[name] = std::move(encrypted);
        return true;
    }

    std::vector<uint8_t> SecretKeyStore::retrieve_secret(const std::string &name) const
    {
        auto it = secrets_.find(name);
        if (it == secrets_.end())
        {
            throw std::runtime_error(fmt::format("Secret '{}' not found in store", name));
        }

        if (sodium_init() < 0)
        {
            throw std::runtime_error("Failed to initialize libsodium");
        }

        const std::vector<uint8_t> &encrypted = it->second;
        if (encrypted.size() < NONCE_SIZE + MAC_SIZE + 1)
        {
            throw std::runtime_error("Encrypted secret data is corrupted or too short");
        }

        std::vector<uint8_t> master_key = get_master_key();

        std::vector<uint8_t> nonce(encrypted.begin(), encrypted.begin() + NONCE_SIZE);
        std::vector<uint8_t> ciphertext(encrypted.begin() + NONCE_SIZE, encrypted.end());

        std::vector<uint8_t> derived_key = derive_decryption_key(master_key, name, nonce);

        std::vector<uint8_t> plaintext(ciphertext.size());
        if (crypto_secretbox_open_easy(plaintext.data(),
                                       ciphertext.data(), ciphertext.size(),
                                       nonce.data(),
                                       derived_key.data()) != 0)
        {
            throw std::runtime_error("Secret decryption failed - invalid key or corrupted data");
        }

        if (plaintext.size() <= MAC_SIZE)
        {
            throw std::runtime_error("Decrypted secret too short");
        }
        plaintext.resize(plaintext.size() - MAC_SIZE);
        return plaintext;
    }

    bool SecretKeyStore::has_secret(const std::string &name) const
    {
        return secrets_.find(name) != secrets_.end();
    }

    bool SecretKeyStore::remove_secret(const std::string &name)
    {
        auto it = secrets_.find(name);
        if (it == secrets_.end())
        {
            return false;
        }
        secrets_.erase(it);
        return true;
    }

    void SecretKeyStore::clear()
    {
        secrets_.clear();
    }

    size_t SecretKeyStore::size() const
    {
        return secrets_.size();
    }

    std::vector<std::string> SecretKeyStore::list_secrets() const
    {
        std::vector<std::string> names;
        for (const auto &[secret_name, data] : secrets_)
        {
            (void)data;
            names.push_back(secret_name);
        }
        return names;
    }

    // ============================================================================
    // PrivateStateManager Implementation
    // ============================================================================

    LedgerState PrivateStateManager::get_private_state(const std::string &contract_address) const
    {
        auto it = private_states_.find(contract_address);
        if (it == private_states_.end())
        {
            return LedgerState();
        }
        return it->second;
    }

    void PrivateStateManager::update_private_state(
        const std::string &contract_address,
        const LedgerState &state)
    {
        private_states_[contract_address] = state;
    }

    LedgerState PrivateStateManager::get_public_state(
        const std::string &contract_address,
        const std::string &rpc_endpoint)
    {
        (void)rpc_endpoint;
        auto it = private_states_.find(contract_address);
        if (it != private_states_.end())
        {
            return it->second;
        }

        LedgerState state;
        state.contract_address = contract_address;
        state.block_height = 0;
        return state;
    }

    void PrivateStateManager::sync_state(
        const std::string &contract_address,
        const LedgerState &public_state)
    {
        LedgerState merged = get_private_state(contract_address);
        merged.public_state = public_state.public_state;
        merged.block_height = public_state.block_height;
        merged.block_hash = public_state.block_hash;

        update_private_state(contract_address, merged);
    }

    bool PrivateStateManager::has_state(const std::string &contract_address) const
    {
        return private_states_.find(contract_address) != private_states_.end();
    }

    void PrivateStateManager::clear()
    {
        private_states_.clear();
    }

    std::vector<std::string> PrivateStateManager::list_contracts() const
    {
        std::vector<std::string> addresses;
        for (const auto &[addr, state] : private_states_)
        {
            (void)state;
            addresses.push_back(addr);
        }
        return addresses;
    }

    // ============================================================================
    // WitnessContext Implementation
    // ============================================================================

    json WitnessContext::to_json() const
    {
        json j;
        j["public_ledger_state"] = public_ledger_state.to_json();
        j["contract_address"] = contract_address;
        j["block_height"] = block_height;

        json private_data_json;
        for (const auto &[name, data] : private_data)
        {
            std::stringstream ss;
            for (uint8_t byte : data)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            private_data_json[name] = ss.str();
        }
        j["private_data"] = private_data_json;

        return j;
    }

    WitnessContext WitnessContext::from_json(const json &j)
    {
        WitnessContext ctx;
        ctx.public_ledger_state = LedgerState::from_json(j.at("public_ledger_state"));
        ctx.contract_address = j.at("contract_address").get<std::string>();
        ctx.block_height = j.at("block_height").get<uint64_t>();

        if (j.contains("private_data") && j["private_data"].is_object())
        {
            for (const auto &[name, hex_str] : j["private_data"].items())
            {
                std::string hex = hex_str.get<std::string>();
                std::vector<uint8_t> data;

                for (size_t i = 0; i < hex.length(); i += 2)
                {
                    std::string byte_str = hex.substr(i, 2);
                    uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
                    data.push_back(byte);
                }

                ctx.private_data[name] = data;
            }
        }

        return ctx;
    }

    // ============================================================================
    // WitnessContextBuilder Implementation
    // ============================================================================

    WitnessContextBuilder &WitnessContextBuilder::set_contract_address(const std::string &address)
    {
        contract_address_ = address;
        contract_set_ = true;
        return *this;
    }

    WitnessContextBuilder &WitnessContextBuilder::set_public_state(const LedgerState &state)
    {
        public_state_ = state;
        public_state_set_ = true;
        return *this;
    }

    WitnessContextBuilder &WitnessContextBuilder::add_private_secret(
        const std::string &name,
        const std::vector<uint8_t> &data)
    {
        private_data_[name] = data;
        return *this;
    }

    WitnessContextBuilder &WitnessContextBuilder::add_private_secrets(
        const std::map<std::string, std::vector<uint8_t>> &secrets)
    {
        for (const auto &[name, data] : secrets)
        {
            private_data_[name] = data;
        }
        return *this;
    }

    WitnessContextBuilder &WitnessContextBuilder::set_block_height(uint64_t height)
    {
        block_height_ = height;
        return *this;
    }

    WitnessContext WitnessContextBuilder::build() const
    {
        validate_for_build();

        WitnessContext ctx;
        ctx.contract_address = contract_address_;
        ctx.public_ledger_state = public_state_;
        ctx.private_data = private_data_;
        ctx.block_height = block_height_;

        return ctx;
    }

    WitnessContext WitnessContextBuilder::build_from_managers(
        const std::string &contract_address,
        const PrivateStateManager &state_mgr,
        const SecretKeyStore &secret_store,
        const std::vector<std::string> &required_secrets)
    {
        WitnessContextBuilder builder;
        builder.set_contract_address(contract_address);

        LedgerState state = state_mgr.get_private_state(contract_address);
        builder.set_public_state(state);

        std::map<std::string, std::vector<uint8_t>> secrets;
        for (const auto &secret_name : required_secrets)
        {
            if (secret_store.has_secret(secret_name))
            {
                secrets[secret_name] = secret_store.retrieve_secret(secret_name);
            }
        }
        builder.add_private_secrets(secrets);

        return builder.build();
    }

    void WitnessContextBuilder::validate_for_build() const
    {
        if (!contract_set_)
        {
            throw std::runtime_error("Contract address not set in WitnessContextBuilder");
        }

        if (!public_state_set_)
        {
            throw std::runtime_error("Public state not set in WitnessContextBuilder");
        }
    }

    // ============================================================================
    // WitnessExecutionResult Implementation
    // ============================================================================

    json WitnessExecutionResult::to_json() const
    {
        json j;
        j["status"] = static_cast<int>(status);
        j["error_message"] = error_message;
        j["execution_time_ms"] = execution_time_ms;

        json outputs;
        for (const auto &[name, output] : witness_outputs)
        {
            outputs[name] = output.to_json();
        }
        j["witness_outputs"] = outputs;

        return j;
    }

    // ============================================================================
    // Utility Functions
    // ============================================================================

    namespace private_state_util
    {

        WitnessContext create_test_context(const std::string &contract_address)
        {
            WitnessContextBuilder builder;
            builder.set_contract_address(contract_address);

            LedgerState public_state;
            public_state.contract_address = contract_address;
            public_state.block_height = 1;
            public_state.block_hash = "test_hash_" + contract_address;
            builder.set_public_state(public_state);

            std::vector<uint8_t> test_secret(32, 0xAA);
            builder.add_private_secret("localSecretKey", test_secret);
            builder.set_block_height(1);

            return builder.build();
        }

        std::vector<uint8_t> create_test_secret()
        {
            return std::vector<uint8_t>(32, 0xAA);
        }

        bool validate_secret_size(
            const std::vector<uint8_t> &secret,
            size_t expected_size)
        {
            return secret.size() == expected_size;
        }

        std::string hash_secret(const std::vector<uint8_t> &secret)
        {
            uint8_t hash[SHA256_DIGEST_LENGTH];
            SHA256(secret.data(), secret.size(), hash);
            std::string result;
            result.reserve(SHA256_DIGEST_LENGTH * 2);
            for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
            {
                result += fmt::format("{:02x}", hash[i]);
            }
            return result;
        }

    } // namespace private_state_util

} // namespace midnight::zk
