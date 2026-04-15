#include "midnight/zk/private_state.hpp"
#include <fmt/format.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace midnight::zk
{

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

        secrets_[name] = data;
        return true;
    }

    std::vector<uint8_t> SecretKeyStore::retrieve_secret(const std::string &name) const
    {
        auto it = secrets_.find(name);
        if (it == secrets_.end())
        {
            throw std::runtime_error(fmt::format("Secret '{}' not found in store", name));
        }
        return it->second;
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
        for (const auto &[name, data] : secrets_)
        {
            names.push_back(name);
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
            return LedgerState(); // Return empty state
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
        // This would normally fetch from RPC
        // For now, return stored state or empty state
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
        // Merge public state with existing private state
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
            // Convert bytes to hex
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

        // Load public state
        LedgerState state = state_mgr.get_private_state(contract_address);
        builder.set_public_state(state);

        // Load private secrets
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

            // Create dummy public state
            LedgerState public_state;
            public_state.contract_address = contract_address;
            public_state.block_height = 1;
            public_state.block_hash = "test_hash_" + contract_address;
            builder.set_public_state(public_state);

            // Add test secret
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
            // Simple SHA256-like hex representation for demo
            // In production, use actual SHA256
            std::stringstream ss;
            for (size_t i = 0; i < std::min(secret.size(), size_t(16)); ++i)
            {
                ss << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(secret[i]);
            }
            return ss.str();
        }

    } // namespace private_state_util

} // namespace midnight::zk
