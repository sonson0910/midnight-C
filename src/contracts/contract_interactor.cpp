#include "midnight/contracts/contract_interactor.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/production/validation.hpp"
#include <cstdlib>
#include <fstream>
#include <stdexcept>
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
            .ledger_ffi_library = std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY") ? std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY") : "",
        };
    }

    NetworkConfig NetworkConfig::preview()
    {
        return {
            .node_url = "https://rpc.preview.midnight.network",
            .indexer_url = "https://indexer.preview.midnight.network/api/v4/graphql",
            .proof_server_url = "http://localhost:6300",
            .network_name = "preview",
            .ledger_ffi_library = std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY") ? std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY") : "",
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
            .ledger_ffi_library = std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY") ? std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY") : "",
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
        ledger_backend_ = ledger::make_ffi_ledger_backend(config.ledger_ffi_library);

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

        auto *ffi_backend = dynamic_cast<ledger::FfiLedgerBackend *>(ledger_backend_.get());
        if (ffi_backend == nullptr || !ffi_backend->can_derive_wallet_addresses())
        {
            throw std::runtime_error(
                "ContractInteractor requires libmidnight_ledger_ffi to derive canonical Midnight wallet addresses");
        }

        const auto addresses = ffi_backend->derive_wallet_addresses(seed_hex_, config_.network_name);
        if (!addresses.success)
        {
            throw std::runtime_error("Canonical Midnight wallet derivation failed: " + addresses.error);
        }
        derived_address_ = addresses.unshielded;

        midnight::g_logger->info("Wallet derived: " + derived_address_.substr(0, 30) + "...");
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
        if (!midnight::production::validation::is_contract_address_hex(contract_address))
        {
            throw std::invalid_argument(
                "Contract state queries require a 32-byte contract hex address, not a wallet address");
        }
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
        if (!midnight::production::validation::is_contract_address_hex(contract_address))
        {
            throw std::invalid_argument(
                "Contract field queries require a 32-byte contract hex address, not a wallet address");
        }
        return indexer_->query_contract_fields(contract_address, fields);
    }

    network::WalletState ContractInteractor::get_wallet_state()
    {
        if (derived_address_.empty())
        {
            return {.error = "No wallet derived. Call set_seed_hex() first."};
        }
        try
        {
            return indexer_->query_wallet_state(derived_address_);
        }
        catch (const std::exception &e)
        {
            return {.error = e.what()};
        }
    }

    network::WalletState ContractInteractor::get_wallet_state_from_transaction(const std::string &tx_hash)
    {
        if (derived_address_.empty())
        {
            return {.error = "No wallet derived. Call set_seed_hex() first."};
        }
        try
        {
            return indexer_->query_wallet_state_from_transaction(derived_address_, tx_hash);
        }
        catch (const std::exception &e)
        {
            return {.error = e.what()};
        }
    }

    network::DustRegistrationStatus ContractInteractor::query_dust_status()
    {
        if (derived_address_.empty())
        {
            return network::DustRegistrationStatus::UNKNOWN;
        }
        network::WalletState state;
        try
        {
            state = indexer_->query_wallet_state(derived_address_);
        }
        catch (const std::exception &)
        {
            return network::DustRegistrationStatus::UNKNOWN;
        }
        if (!state.error.empty())
        {
            return network::DustRegistrationStatus::UNKNOWN;
        }
        return state.dust_registered_utxo_count > 0
                   ? network::DustRegistrationStatus::REGISTERED
                   : network::DustRegistrationStatus::NOT_REGISTERED;
    }

    // ════════════════════════════════════════════════
    // Contract Deployment (Native — No Node.js)
    // ════════════════════════════════════════════════

    DeployResult ContractInteractor::submit_deploy_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        DeployResult result;
        auto submit = submit_serialized_transaction(transaction_bytes);
        result.success = submit.success;
        result.txid = submit.txid;
        result.error = submit.error;
        return result;
    }

    // ════════════════════════════════════════════════
    // Circuit Call (State-Changing — Native)
    // ════════════════════════════════════════════════

    CallResult ContractInteractor::submit_call_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        return submit_serialized_transaction(transaction_bytes);
    }

    // ════════════════════════════════════════════════
    // Native NIGHT Transfer
    // ════════════════════════════════════════════════

    CallResult ContractInteractor::submit_transfer_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        return submit_serialized_transaction(transaction_bytes);
    }

    // ════════════════════════════════════════════════
    // Native DUST Registration
    // ════════════════════════════════════════════════

    CallResult ContractInteractor::submit_serialized_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        CallResult result;

        if (transaction_bytes.empty())
        {
            result.error = "Serialized transaction bytes cannot be empty.";
            return result;
        }

        try
        {
            const auto tx_hex = midnight::util::ensure_hex_prefix(
                midnight::util::bytes_to_hex(transaction_bytes));
            result.txid = rpc_->submit_transaction(tx_hex);
            result.success = !result.txid.empty();
            if (!result.success)
            {
                result.error = "Node RPC returned an empty transaction hash.";
            }
        }
        catch (const std::exception &e)
        {
            result.error = "Serialized transaction submit failed: " + std::string(e.what());
        }

        return result;
    }

    std::vector<uint8_t> ContractInteractor::prove_contract_payload(
        const std::vector<uint8_t> &proving_payload)
    {
        return proof_server_->post_proving_payload(proving_payload);
    }

    std::vector<uint8_t> ContractInteractor::check_contract_payload(
        const std::vector<uint8_t> &check_payload)
    {
        return proof_server_->post_check_payload(check_payload);
    }

    std::vector<uint8_t> ContractInteractor::prove_transaction_payload(
        const std::vector<uint8_t> &prove_tx_payload)
    {
        return proof_server_->post_prove_tx_payload(prove_tx_payload);
    }

    CallResult ContractInteractor::submit_dust_registration_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        return submit_serialized_transaction(transaction_bytes);
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
        (void)circuit_name;
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

} // namespace midnight::contracts
