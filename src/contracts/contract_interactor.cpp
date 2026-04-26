#include "midnight/contracts/contract_interactor.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace midnight::contracts
{

    // ════════════════════════════════════════════════
    // NetworkConfig factory methods
    // ════════════════════════════════════════════════

    NetworkConfig NetworkConfig::preprod()
    {
        return {
            .node_url = "https://rpc.preprod.midnight.network",
            .indexer_url = "https://indexer.preprod.midnight.network/api/v4/graphql",
            .proof_server_url = "http://localhost:6300",
            .network_name = "preprod",
        };
    }

    NetworkConfig NetworkConfig::preview()
    {
        return {
            .node_url = "https://rpc.preview.midnight.network",
            .indexer_url = "https://indexer.preview.midnight.network/api/v4/graphql",
            .proof_server_url = "http://localhost:6300",
            .network_name = "preview",
        };
    }

    NetworkConfig NetworkConfig::from_urls(const std::string &node,
                                           const std::string &indexer,
                                           const std::string &proof_server,
                                           const std::string &network)
    {
        return {
            .node_url = node,
            .indexer_url = indexer,
            .proof_server_url = proof_server,
            .network_name = network,
        };
    }

    // ════════════════════════════════════════════════
    // Constructor
    // ════════════════════════════════════════════════

    ContractInteractor::ContractInteractor(const NetworkConfig &config)
        : config_(config)
    {
        // Initialize native clients
        indexer_ = std::make_unique<network::IndexerClient>(config.indexer_url);
        rpc_ = std::make_unique<network::MidnightNodeRPC>(config.node_url);

        // Parse proof server URL for client config
        zk::ProofServerClient::Config ps_config;
        // Extract host and port from URL
        std::string url = config.proof_server_url;
        if (url.find("https://") == 0)
        {
            ps_config.use_https = true;
            url = url.substr(8);
        }
        else if (url.find("http://") == 0)
        {
            url = url.substr(7);
        }

        auto colon_pos = url.find(':');
        if (colon_pos != std::string::npos)
        {
            ps_config.host = url.substr(0, colon_pos);
            auto slash_pos = url.find('/', colon_pos);
            std::string port_str = (slash_pos != std::string::npos)
                                       ? url.substr(colon_pos + 1, slash_pos - colon_pos - 1)
                                       : url.substr(colon_pos + 1);
            ps_config.port = static_cast<uint16_t>(std::stoi(port_str));
        }
        else
        {
            ps_config.host = url;
            ps_config.port = ps_config.use_https ? 443 : 6300;
        }

        proof_server_ = std::make_unique<zk::ProofServerClient>(ps_config);

        midnight::g_logger->info("ContractInteractor initialized: " + config.network_name +
                                 " (node=" + config.node_url +
                                 ", indexer=" + config.indexer_url +
                                 ", proof=" + config.proof_server_url + ")");
    }

    // ════════════════════════════════════════════════
    // Wallet Management
    // ════════════════════════════════════════════════

    void ContractInteractor::set_seed_hex(const std::string &seed_hex)
    {
        seed_hex_ = seed_hex;
        derive_wallet();
    }

    void ContractInteractor::derive_wallet()
    {
        if (seed_hex_.empty())
        {
            return;
        }

        try
        {
            // Initialize libsodium (idempotent)
            crypto::Ed25519Signer::initialize();

            // Derive keypair from seed (first 32 bytes of seed = private key seed)
            crypto::Ed25519Signer::PrivateKey priv_key{};
            if (seed_hex_.size() >= 128)
            {
                // Full 64-byte private key in hex
                for (size_t i = 0; i < 64; ++i)
                {
                    auto hi = midnight::util::hex_nibble(seed_hex_[i * 2]);
                    auto lo = midnight::util::hex_nibble(seed_hex_[i * 2 + 1]);
                    priv_key[i] = static_cast<uint8_t>((hi << 4) | lo);
                }
            }
            else if (seed_hex_.size() >= 64)
            {
                // 32-byte seed → derive full key
                std::array<uint8_t, 32> seed{};
                for (size_t i = 0; i < 32; ++i)
                {
                    auto hi = midnight::util::hex_nibble(seed_hex_[i * 2]);
                    auto lo = midnight::util::hex_nibble(seed_hex_[i * 2 + 1]);
                    seed[i] = static_cast<uint8_t>((hi << 4) | lo);
                }
                // Use seed as first 32 bytes, derive public key for second 32
                auto pub_key = crypto::Ed25519Signer::extract_public_key(priv_key);
                std::copy(seed.begin(), seed.end(), priv_key.begin());
                std::copy(pub_key.begin(), pub_key.end(), priv_key.begin() + 32);
            }

            auto pub_key = crypto::Ed25519Signer::extract_public_key(priv_key);
            std::string pub_hex = crypto::Ed25519Signer::public_key_to_hex(pub_key);

            // Encode as Midnight address (Bech32m)
            std::string hrp = (config_.network_name == "mainnet") ? "mn_addr_mainnet" : "mn_addr_preprod";
            std::array<uint8_t, 32> payload{};
            std::copy(pub_key.begin(), pub_key.end(), payload.begin());
            derived_address_ = midnight::util::bech32m::encode(hrp, payload);

            midnight::g_logger->info("Wallet derived: " + derived_address_.substr(0, 30) + "...");
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to derive wallet: " + std::string(e.what()));
        }
    }

    std::string ContractInteractor::get_wallet_address() const
    {
        return derived_address_;
    }

    // ════════════════════════════════════════════════
    // Read-Only Queries (No Proof, No Node.js)
    // ════════════════════════════════════════════════

    json ContractInteractor::read_state(const std::string &contract_address)
    {
        auto state = indexer_->query_contract_state(contract_address);
        if (!state.exists)
        {
            midnight::g_logger->warn("Contract not found: " + contract_address);
            return json::object();
        }
        return state.ledger_state;
    }

    json ContractInteractor::read_fields(const std::string &contract_address,
                                         const std::vector<std::string> &fields)
    {
        return indexer_->query_contract_fields(contract_address, fields);
    }

    network::WalletState ContractInteractor::get_wallet_state()
    {
        if (derived_address_.empty())
        {
            return {.error = "No wallet derived. Call set_seed_hex() first."};
        }
        return indexer_->query_wallet_state(derived_address_);
    }

    network::DustRegistrationStatus ContractInteractor::query_dust_status()
    {
        if (derived_address_.empty())
        {
            return network::DustRegistrationStatus::UNKNOWN;
        }
        return indexer_->query_dust_status(derived_address_);
    }

    // ════════════════════════════════════════════════
    // Contract Deployment (Native — No Node.js)
    // ════════════════════════════════════════════════

    DeployResult ContractInteractor::deploy(
        const std::string &contract_name,
        const json &constructor_args,
        const std::string &zk_config_path)
    {
        DeployResult result;

        if (seed_hex_.empty())
        {
            result.error = "No wallet seed. Call set_seed_hex() first.";
            return result;
        }

        try
        {
            midnight::g_logger->info("Deploying contract: " + contract_name);

            // 1. Load circuit data from ZK config
            auto circuit_data = load_circuit_data(zk_config_path, "constructor");

            // 2. Build public inputs from constructor args
            zk::PublicInputs inputs;
            for (size_t i = 0; i < constructor_args.size(); ++i)
            {
                inputs.add_input("arg_" + std::to_string(i),
                                 constructor_args[i].dump());
            }

            // 3. Generate ZK proof via Proof Server (native HTTP)
            std::map<std::string, zk::WitnessOutput> witnesses;
            auto proof_result = proof_server_->generate_proof(
                contract_name + "_constructor", circuit_data, inputs, witnesses);

            if (!proof_result.success)
            {
                result.error = "Proof generation failed: " + proof_result.error_message;
                midnight::g_logger->error(result.error);
                return result;
            }

            // 4. Build deployment transaction
            zk::ProofEnabledTransaction deploy_tx;
            deploy_tx.circuit_proof = proof_result.proof;

            // 5. Sign and submit
            result.txid = sign_and_submit(deploy_tx);
            if (!result.txid.empty())
            {
                result.success = true;
                midnight::g_logger->info("Contract deployed: txid=" + result.txid);
            }
        }
        catch (const std::exception &e)
        {
            result.error = "Deploy failed: " + std::string(e.what());
            midnight::g_logger->error(result.error);
        }

        return result;
    }

    // ════════════════════════════════════════════════
    // Circuit Call (State-Changing — Native)
    // ════════════════════════════════════════════════

    CallResult ContractInteractor::call_circuit(
        const std::string &contract_address,
        const std::string &circuit_name,
        const json &args)
    {
        CallResult result;

        if (seed_hex_.empty())
        {
            result.error = "No wallet seed. Call set_seed_hex() first.";
            return result;
        }

        try
        {
            midnight::g_logger->info("Calling circuit: " + circuit_name +
                                     " on " + contract_address.substr(0, 20) + "...");

            // 1. Read current contract state (native GraphQL)
            auto current_state = read_state(contract_address);

            // 2. Build public inputs
            auto inputs = build_public_inputs(circuit_name, args, current_state);

            // 3. Generate ZK proof (native HTTP to Proof Server)
            auto circuit_data = load_circuit_data("", circuit_name);
            std::map<std::string, zk::WitnessOutput> witnesses;
            auto proof_result = proof_server_->generate_proof(
                circuit_name, circuit_data, inputs, witnesses);

            if (!proof_result.success)
            {
                result.error = "Proof generation failed: " + proof_result.error_message;
                return result;
            }

            // 4. Build proof-enabled transaction
            zk::ProofEnabledTransaction tx;
            tx.circuit_proof = proof_result.proof;

            // 5. Sign and submit (native Ed25519 + JSON-RPC)
            result.txid = sign_and_submit(tx);
            result.success = !result.txid.empty();

            if (result.success)
            {
                midnight::g_logger->info("Circuit call submitted: " + result.txid);
            }
        }
        catch (const std::exception &e)
        {
            result.error = "Circuit call failed: " + std::string(e.what());
            midnight::g_logger->error(result.error);
        }

        return result;
    }

    // ════════════════════════════════════════════════
    // Native NIGHT Transfer
    // ════════════════════════════════════════════════

    CallResult ContractInteractor::transfer_night(
        const std::string &to_address,
        const std::string &amount)
    {
        CallResult result;

        if (seed_hex_.empty())
        {
            result.error = "No wallet seed. Call set_seed_hex() first.";
            return result;
        }

        try
        {
            midnight::g_logger->info("Transferring " + amount + " NIGHT to " +
                                     to_address.substr(0, 20) + "...");

            // 1. Query UTXOs (native GraphQL)
            auto utxos = indexer_->query_utxos(derived_address_);
            if (utxos.empty())
            {
                result.error = "No UTXOs available for transfer";
                return result;
            }

            // 2. Build transfer transaction (native UTXO builder)
            uint64_t amount_val = std::stoull(amount);
            uint64_t total_input = 0;
            std::vector<blockchain::UTXO> selected;

            for (const auto &utxo : utxos)
            {
                selected.push_back(utxo);
                total_input += utxo.amount;
                if (total_input >= amount_val + 200000) // amount + estimated fee
                {
                    break;
                }
            }

            if (total_input < amount_val)
            {
                result.error = "Insufficient funds: have " + std::to_string(total_input) +
                               ", need " + amount;
                return result;
            }

            // 3. Build outputs
            std::vector<std::pair<std::string, uint64_t>> outputs;
            outputs.emplace_back(to_address, amount_val);

            uint64_t change = total_input - amount_val - 200000; // simple fee estimate
            if (change > 0)
            {
                outputs.emplace_back(derived_address_, change);
            }

            // 4. Sign the transaction (native Ed25519)
            crypto::Ed25519Signer::PrivateKey priv_key{};
            for (size_t i = 0; i < std::min(seed_hex_.size() / 2, size_t(64)); ++i)
            {
                auto hi = midnight::util::hex_nibble(seed_hex_[i * 2]);
                auto lo = midnight::util::hex_nibble(seed_hex_[i * 2 + 1]);
                priv_key[i] = static_cast<uint8_t>((hi << 4) | lo);
            }

            // 5. Build transaction hex representation
            json tx_body = {
                {"type", "transfer"},
                {"from", derived_address_},
                {"to", to_address},
                {"amount", amount},
                {"inputs", json::array()},
                {"outputs", json::array()},
            };

            for (const auto &utxo : selected)
            {
                tx_body["inputs"].push_back({{"txHash", utxo.tx_hash},
                                             {"index", utxo.output_index}});
            }

            for (const auto &[addr, val] : outputs)
            {
                tx_body["outputs"].push_back({{"address", addr},
                                              {"value", std::to_string(val)}});
            }

            std::string tx_hex = tx_body.dump();

            // Sign
            auto signature = crypto::Ed25519Signer::sign_message(tx_hex, priv_key);
            std::string sig_hex = crypto::Ed25519Signer::signature_to_hex(signature);

            // 6. Submit via RPC (native JSON-RPC)
            std::string submission = tx_hex + sig_hex;
            result.txid = rpc_->submit_transaction(submission);
            result.success = !result.txid.empty();

            if (result.success)
            {
                midnight::g_logger->info("Transfer submitted: " + result.txid);
            }
        }
        catch (const std::exception &e)
        {
            result.error = "Transfer failed: " + std::string(e.what());
            midnight::g_logger->error(result.error);
        }

        return result;
    }

    // ════════════════════════════════════════════════
    // Native DUST Registration
    // ════════════════════════════════════════════════

    CallResult ContractInteractor::register_dust()
    {
        CallResult result;

        if (seed_hex_.empty())
        {
            result.error = "No wallet seed. Call set_seed_hex() first.";
            return result;
        }

        try
        {
            // Check current DUST status
            auto status = query_dust_status();
            if (status == network::DustRegistrationStatus::REGISTERED)
            {
                result.success = true;
                result.result = {{"status", "already_registered"}};
                return result;
            }

            midnight::g_logger->info("Registering DUST for " + derived_address_.substr(0, 20) + "...");

            // DUST registration is effectively a self-transfer that triggers
            // the NIGHT → DUST conversion. Build a minimal self-transfer.
            auto utxos = indexer_->query_utxos(derived_address_);
            if (utxos.empty())
            {
                result.error = "No UTXOs available. Need NIGHT to register DUST.";
                return result;
            }

            // Build minimal self-transfer to trigger DUST allocation
            json dust_tx = {
                {"type", "dust_registration"},
                {"address", derived_address_},
                {"utxo_count", utxos.size()},
            };

            // Sign and submit
            crypto::Ed25519Signer::PrivateKey priv_key{};
            for (size_t i = 0; i < std::min(seed_hex_.size() / 2, size_t(64)); ++i)
            {
                auto hi = midnight::util::hex_nibble(seed_hex_[i * 2]);
                auto lo = midnight::util::hex_nibble(seed_hex_[i * 2 + 1]);
                priv_key[i] = static_cast<uint8_t>((hi << 4) | lo);
            }

            std::string tx_hex = dust_tx.dump();
            auto signature = crypto::Ed25519Signer::sign_message(tx_hex, priv_key);
            std::string signed_payload = tx_hex + crypto::Ed25519Signer::signature_to_hex(signature);

            result.txid = rpc_->submit_transaction(signed_payload);
            result.success = !result.txid.empty();

            if (result.success)
            {
                result.result = {{"status", "registered"}, {"txid", result.txid}};
                midnight::g_logger->info("DUST registration submitted: " + result.txid);
            }
        }
        catch (const std::exception &e)
        {
            result.error = "DUST registration failed: " + std::string(e.what());
            midnight::g_logger->error(result.error);
        }

        return result;
    }

    // ════════════════════════════════════════════════
    // Health Check
    // ════════════════════════════════════════════════

    json ContractInteractor::health_check()
    {
        json status;

        // Check Node RPC
        try
        {
            auto info = rpc_->get_node_info();
            status["node"] = {{"status", "ok"}, {"info", info}};
        }
        catch (const std::exception &e)
        {
            status["node"] = {{"status", "error"}, {"error", e.what()}};
        }

        // Check Indexer
        try
        {
            bool synced = indexer_->is_synced();
            uint64_t height = indexer_->get_block_height();
            status["indexer"] = {{"status", "ok"}, {"synced", synced}, {"height", height}};
        }
        catch (const std::exception &e)
        {
            status["indexer"] = {{"status", "error"}, {"error", e.what()}};
        }

        // Check Proof Server
        try
        {
            auto ps_status = proof_server_->get_server_status();
            status["proof_server"] = {{"status", "ok"}, {"info", ps_status}};
        }
        catch (const std::exception &e)
        {
            status["proof_server"] = {{"status", "error"}, {"error", e.what()}};
        }

        return status;
    }

    // ════════════════════════════════════════════════
    // Internal Helpers
    // ════════════════════════════════════════════════

    std::vector<uint8_t> ContractInteractor::load_circuit_data(
        const std::string &zk_config_path,
        const std::string &circuit_name)
    {
        std::vector<uint8_t> data;

        if (zk_config_path.empty())
        {
            // Return empty — proof server may have pre-loaded circuits
            return data;
        }

        // Look for circuit file: zk_config_path/zkir/<circuit_name>.zkir
        namespace fs = std::filesystem;
        fs::path zkir_path = fs::path(zk_config_path) / "zkir";

        if (!fs::exists(zkir_path))
        {
            midnight::g_logger->warn("ZK config directory not found: " + zkir_path.string());
            return data;
        }

        // Search for matching circuit file
        for (const auto &entry : fs::directory_iterator(zkir_path))
        {
            if (entry.path().stem().string().find(circuit_name) != std::string::npos)
            {
                std::ifstream file(entry.path(), std::ios::binary);
                if (file)
                {
                    data.assign(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
                    midnight::g_logger->debug("Loaded circuit: " + entry.path().string() +
                                              " (" + std::to_string(data.size()) + " bytes)");
                    return data;
                }
            }
        }

        midnight::g_logger->warn("Circuit file not found for: " + circuit_name);
        return data;
    }

    zk::PublicInputs ContractInteractor::build_public_inputs(
        const std::string &circuit_name,
        const json &args,
        const json &current_state)
    {
        zk::PublicInputs inputs;

        // Add circuit arguments as public inputs
        if (args.is_array())
        {
            for (size_t i = 0; i < args.size(); ++i)
            {
                const auto &arg = args[i];
                if (arg.is_string())
                {
                    inputs.add_input("arg_" + std::to_string(i), arg.get<std::string>());
                }
                else if (arg.is_object() && arg.contains("__type"))
                {
                    // Typed argument (bigint, bytes, etc.)
                    if (arg["__type"] == "bigint")
                    {
                        inputs.add_input("arg_" + std::to_string(i),
                                         arg.value("value", "0"));
                    }
                    else if (arg["__type"] == "bytes")
                    {
                        inputs.add_input("arg_" + std::to_string(i),
                                         arg.value("hex", ""));
                    }
                }
                else
                {
                    inputs.add_input("arg_" + std::to_string(i), arg.dump());
                }
            }
        }

        // Add caller address for circuits that need it
        if (!derived_address_.empty())
        {
            inputs.add_input("caller", derived_address_);
        }

        // Add current state hash for state-transition proofs
        if (!current_state.empty())
        {
            inputs.add_input("current_state_hash", current_state.dump());
        }

        return inputs;
    }

    std::string ContractInteractor::sign_and_submit(const zk::ProofEnabledTransaction &tx)
    {
        // Serialize transaction
        std::string tx_hex = tx.to_json().dump();

        // Sign with Ed25519
        crypto::Ed25519Signer::PrivateKey priv_key{};
        for (size_t i = 0; i < std::min(seed_hex_.size() / 2, size_t(64)); ++i)
        {
            auto hi = midnight::util::hex_nibble(seed_hex_[i * 2]);
            auto lo = midnight::util::hex_nibble(seed_hex_[i * 2 + 1]);
            priv_key[i] = static_cast<uint8_t>((hi << 4) | lo);
        }

        auto signature = crypto::Ed25519Signer::sign_message(tx_hex, priv_key);
        std::string sig_hex = crypto::Ed25519Signer::signature_to_hex(signature);

        // Build signed payload
        std::string signed_payload = tx_hex + sig_hex;

        // Submit via native JSON-RPC
        return rpc_->submit_transaction(signed_payload);
    }

} // namespace midnight::contracts
