#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/contract/contract_manager.hpp"
#include "midnight/production/validation.hpp"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <sodium.h>

namespace midnight::contract
{

    namespace fs = std::filesystem;

    // ─── ContractArtifact ─────────────────────────────────────

    ContractArtifact ContractArtifact::load_from_dir(const std::string &dir_path)
    {
        ContractArtifact artifact;

        fs::path dir(dir_path);
        if (!fs::exists(dir))
            throw std::runtime_error("Artifact directory not found: " + dir_path);

        // Load contract metadata (contract.json or manifest)
        fs::path meta_path = dir / "contract" / "index.json";
        if (!fs::exists(meta_path))
            meta_path = dir / "contract.json";

        if (fs::exists(meta_path))
        {
            std::ifstream f(meta_path);
            artifact.metadata = json::parse(f);
            artifact.name = artifact.metadata.value("name", dir.filename().string());
        }
        else
        {
            artifact.name = dir.filename().string();
        }

        // Load bytecode (WASM)
        fs::path wasm_path = dir / "contract" / "contract.wasm";
        if (fs::exists(wasm_path))
        {
            std::ifstream f(wasm_path, std::ios::binary);
            f.seekg(0, std::ios::end);
            auto sz = f.tellg();
            f.seekg(0, std::ios::beg);
            artifact.bytecode.resize(static_cast<size_t>(sz));
            f.read(reinterpret_cast<char*>(artifact.bytecode.data()), sz);
        }

        // Load circuit files (*.prover, *.verifier)
        fs::path zk_dir = dir / "zkConfig";
        if (!fs::exists(zk_dir))
            zk_dir = dir / "contract" / "managed";

        if (fs::exists(zk_dir))
        {
            for (const auto &entry : fs::directory_iterator(zk_dir))
            {
                if (!entry.is_regular_file())
                    continue;

                std::string ext = entry.path().extension().string();
                std::string stem = entry.path().stem().string();

                std::ifstream f(entry.path(), std::ios::binary);
                std::vector<uint8_t> data;
                f.seekg(0, std::ios::end);
                auto sz = f.tellg();
                f.seekg(0, std::ios::beg);
                data.resize(static_cast<size_t>(sz));
                f.read(reinterpret_cast<char*>(data.data()), sz);

                if (ext == ".prover")
                {
                    artifact.circuits[stem] = data;
                }
                else if (ext == ".verifier")
                {
                    artifact.verifier_keys[stem] = data;
                }
                else if (ext == ".zkir" || ext == ".zkConfig")
                {
                    artifact.zk_config = data;
                }
            }
        }

        midnight::g_logger->info("Loaded artifact '" + artifact.name +
                                  "' with " + std::to_string(artifact.circuits.size()) +
                                  " circuits");
        return artifact;
    }

    json ContractArtifact::to_json() const
    {
        json j;
        j["name"] = name;
        j["metadata"] = metadata;
        j["bytecode_size"] = bytecode.size();
        j["circuit_count"] = circuits.size();

        json circuit_list = json::array();
        for (const auto &[name, data] : circuits)
        {
            circuit_list.push_back({{"name", name}, {"size", data.size()}});
        }
        j["circuits"] = circuit_list;

        return j;
    }

    ContractArtifact ContractArtifact::from_json(const json &j)
    {
        ContractArtifact artifact;
        artifact.name = j.value("name", "");
        artifact.metadata = j.value("metadata", json::object());
        return artifact;
    }

    // ─── ContractManager ──────────────────────────────────────

    ContractManager::ContractManager(const std::string &node_rpc_url,
                                     const std::string &proof_url,
                                     const std::string &indexer_url)
    {
        rpc_ = std::make_unique<network::SubstrateRPC>(node_rpc_url);

        zk::ProofServerClient::Config proof_config;
        // Parse proof_url
        if (proof_url.find("https://") == 0)
        {
            proof_config.use_https = true;
            std::string rest = proof_url.substr(8);
            auto colon = rest.find(':');
            if (colon != std::string::npos)
            {
                proof_config.host = rest.substr(0, colon);
                proof_config.port = static_cast<uint16_t>(
                    std::stoi(rest.substr(colon + 1)));
            }
            else
            {
                proof_config.host = rest;
                proof_config.port = 443;
            }
        }
        else
        {
            std::string rest = proof_url;
            if (rest.find("http://") == 0)
                rest = rest.substr(7);
            auto colon = rest.find(':');
            if (colon != std::string::npos)
            {
                proof_config.host = rest.substr(0, colon);
                // Strip path from port
                auto slash = rest.find('/', colon);
                std::string port_str = (slash != std::string::npos)
                                           ? rest.substr(colon + 1, slash - colon - 1)
                                           : rest.substr(colon + 1);
                proof_config.port = static_cast<uint16_t>(std::stoi(port_str));
            }
            else
            {
                proof_config.host = rest;
                proof_config.port = 6300;
            }
        }
        proof_server_ = std::make_unique<zk::ProofServerClient>(proof_config);

        indexer_ = std::make_unique<network::IndexerClient>(indexer_url);

        midnight::g_logger->info("ContractManager initialized");
    }

    // ─── Transaction Submission ───────────────────────────────

    namespace
    {
        std::string submit_midnight_tx_bytes(
            network::SubstrateRPC &rpc,
            const std::vector<uint8_t> &transaction_bytes)
        {
            if (transaction_bytes.empty())
            {
                throw std::runtime_error("Midnight transaction bytes cannot be empty");
            }

            const auto call = tx::PalletCall::midnight_send_mn_transaction(transaction_bytes);
            tx::ExtrinsicParams params{};
            params.genesis_hash.assign(32, 0);
            params.block_hash.assign(32, 0);
            tx::ExtrinsicBuilder builder(params);
            const auto extrinsic = builder.build_unsigned(call);
            const auto submit = rpc.submit_extrinsic(
                midnight::util::ensure_hex_prefix(tx::ExtrinsicBuilder::to_hex(extrinsic)));
            if (!submit.success)
            {
                throw std::runtime_error(submit.error.empty()
                    ? "author_submitExtrinsic failed"
                    : submit.error);
            }
            return submit.tx_hash;
        }
    }

    DeployResult ContractManager::submit_deploy_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        DeployResult result;
        try
        {
            result.tx_hash = submit_midnight_tx_bytes(*rpc_, transaction_bytes);
            result.success = !result.tx_hash.empty();
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        return result;
    }

    CallResult ContractManager::submit_call_transaction(
        const std::vector<uint8_t> &transaction_bytes)
    {
        CallResult result;
        try
        {
            result.tx_hash = submit_midnight_tx_bytes(*rpc_, transaction_bytes);
            result.success = !result.tx_hash.empty();
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        return result;
    }

    // ─── State Queries ────────────────────────────────────────

    ContractInfo ContractManager::query_state(const std::string &contract_address)
    {
        ContractInfo info;
        info.address = contract_address;

        try
        {
            if (!midnight::production::validation::is_contract_address_hex(contract_address))
            {
                throw std::invalid_argument(
                    "Contract state queries require a 32-byte contract hex address, not a wallet address");
            }
            // Query via Indexer GraphQL
            auto state = indexer_->query_contract_state(contract_address);
            info.exists = state.exists;
            info.ledger_state = state.ledger_state;
            info.block_height = state.block_height;
        }
        catch (const std::exception &e)
        {
            info.error = e.what();
            midnight::g_logger->error("State query failed: " + info.error);
        }

        return info;
    }

    json ContractManager::query_fields(const std::string &contract_address,
                                        const std::vector<std::string> &fields)
    {
        try
        {
            if (!midnight::production::validation::is_contract_address_hex(contract_address))
            {
                throw std::invalid_argument(
                    "Contract field queries require a 32-byte contract hex address, not a wallet address");
            }
            return indexer_->query_contract_fields(contract_address, fields);
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Field query failed: " + std::string(e.what()));
            return json::object();
        }
    }

    std::string ContractManager::query_raw_state(const std::string &contract_address)
    {
        try
        {
            if (!midnight::production::validation::is_contract_address_hex(contract_address))
            {
                throw std::invalid_argument(
                    "Raw contract state queries require a 32-byte contract hex address, not a wallet address");
            }
            const auto state = rpc_->midnight_contract_state(contract_address);
            if (state.is_string())
            {
                return state.get<std::string>();
            }
            return state.dump();
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Raw state query failed: " + std::string(e.what()));
            return "";
        }
    }

    // ─── Proof Payloads ───────────────────────────────────────

    std::vector<uint8_t> ContractManager::check_payload(const std::vector<uint8_t> &payload)
    {
        return proof_server_->post_check_payload(payload);
    }

    std::vector<uint8_t> ContractManager::prove_payload(const std::vector<uint8_t> &payload)
    {
        return proof_server_->post_proving_payload(payload);
    }

    std::vector<uint8_t> ContractManager::prove_transaction_payload(const std::vector<uint8_t> &payload)
    {
        return proof_server_->post_prove_tx_payload(payload);
    }

    // ─── Utilities ────────────────────────────────────────────

    json ContractManager::health_check()
    {
        json status;

        // Check node RPC
        try
        {
            auto chain = rpc_->get_chain_name();
            auto version = rpc_->get_runtime_version();
            status["node"] = {
                {"connected", true},
                {"chain", chain},
                {"spec_version", version.spec_version},
                {"synced", rpc_->is_synced()}};
        }
        catch (const std::exception &e)
        {
            status["node"] = {{"connected", false}, {"error", e.what()}};
        }

        // Check proof server
        try
        {
            bool connected = proof_server_->is_connected();
            status["proof_server"] = {{"connected", connected}};
        }
        catch (const std::exception &e)
        {
            status["proof_server"] = {{"connected", false}, {"error", e.what()}};
        }

        // Check indexer
        try
        {
            auto height = indexer_->get_block_height();
            status["indexer"] = {
                {"connected", true},
                {"block_height", height}};
        }
        catch (const std::exception &e)
        {
            status["indexer"] = {{"connected", false}, {"error", e.what()}};
        }

        return status;
    }

    std::vector<uint8_t> ContractManager::encode_constructor_args(const json &args)
    {
        codec::ScaleEncoder enc;

        for (const auto &arg : args)
        {
            if (arg.is_number_unsigned())
            {
                uint64_t val = arg.get<uint64_t>();
                if (val <= 0xFF)
                    enc.encode_u8(static_cast<uint8_t>(val));
                else if (val <= 0xFFFF)
                    enc.encode_u16_le(static_cast<uint16_t>(val));
                else if (val <= 0xFFFFFFFF)
                    enc.encode_u32_le(static_cast<uint32_t>(val));
                else
                    enc.encode_u64_le(val);
            }
            else if (arg.is_number_integer())
            {
                enc.encode_compact(static_cast<uint64_t>(arg.get<int64_t>()));
            }
            else if (arg.is_string())
            {
                std::string s = arg.get<std::string>();
                // Check if hex-encoded bytes
                if (s.substr(0, 2) == "0x")
                {
                    auto bytes = codec::util::hex_to_bytes(s);
                    enc.encode_bytes(bytes);
                }
                else
                {
                    enc.encode_string(s);
                }
            }
            else if (arg.is_boolean())
            {
                enc.encode_bool(arg.get<bool>());
            }
            else if (arg.is_array())
            {
                // Recursively encode
                auto inner = encode_constructor_args(arg);
                enc.encode_bytes(inner);
            }
        }

        return enc.data();
    }

    std::vector<uint8_t> ContractManager::encode_call_args(
        const std::string &circuit_name,
        const json &args)
    {
        codec::ScaleEncoder enc;

        // Encode circuit name as selector (first 4 bytes of blake2b hash)
        if (sodium_init() >= 0)
        {
            uint8_t selector[4];
            auto name_bytes = reinterpret_cast<const uint8_t *>(
                circuit_name.data());
            crypto_generichash(selector, 4, name_bytes,
                               circuit_name.size(), nullptr, 0);
            enc.encode_raw({selector, selector + 4});
        }

        // Encode arguments
        auto arg_bytes = encode_constructor_args(args);
        enc.encode_raw(arg_bytes);

        return enc.data();
    }

} // namespace midnight::contract
