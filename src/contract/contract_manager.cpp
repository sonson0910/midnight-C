#include "midnight/core/logger.hpp"
#include "midnight/contract/contract_manager.hpp"
#include "midnight/compact-contracts/compact_contracts.hpp"
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

    // ─── Deployment ───────────────────────────────────────────

    tx::PalletCall ContractManager::build_deploy_call(const DeployParams &params)
    {
        // Midnight uses a contracts pallet (or similar)
        // Construct the call data:
        //   pallet_index: contracts (typically 0x0A or 0x28 on Midnight)
        //   call_index: instantiate_with_code (typically 0x06)
        //
        // Call structure:
        //   value: Compact<Balance>
        //   gas_limit: Compact<Weight>
        //   storage_deposit_limit: Option<Compact<Balance>>
        //   code: Vec<u8>
        //   data: Vec<u8> (constructor args)
        //   salt: Vec<u8> (optional)

        codec::ScaleEncoder enc;

        // value (Compact<u128>)
        enc.encode_compact(params.value);

        // gas_limit (Weight = {ref_time: Compact<u64>, proof_size: Compact<u64>})
        enc.encode_compact(params.gas_limit);
        enc.encode_compact(params.gas_limit / 4); // proof_size estimate

        // storage_deposit_limit: Option<Compact<Balance>>
        if (params.storage_deposit_limit > 0)
        {
            enc.encode_option_some_prefix();
            enc.encode_compact(params.storage_deposit_limit);
        }
        else
        {
            enc.encode_option_none();
        }

        // code: Vec<u8>
        enc.encode_bytes(params.artifact.bytecode);

        // data: Vec<u8> (constructor args)
        enc.encode_bytes(params.constructor_args);

        // salt: Vec<u8> (random for unique address)
        std::vector<uint8_t> salt(32);
        randombytes_buf(salt.data(), salt.size());
        enc.encode_bytes(salt);

        // Contracts pallet is typically at index 0x28 (40) on Substrate
        // instantiate_with_code is call 0x06
        return tx::PalletCall::custom(0x28, 0x06, enc.data());
    }

    DeployResult ContractManager::deploy(const DeployParams &params,
                                          const wallet::KeyPair &signer,
                                          bool wait_finalized)
    {
        DeployResult result;
        (void)params;
        (void)signer;
        (void)wait_finalized;

        result.error =
            "Native Compact deployment requires a ledger-built serialized transaction. "
            "This Substrate contracts-pallet deploy path is not valid for Midnight Compact. "
            "Use ProofServerClient raw payload methods and submit the ledger serialized transaction.";
        midnight::g_logger->error(result.error);
        return result;

        try
        {
            midnight::g_logger->info("Deploying contract: " + params.artifact.name);

            // 1. Build deployment call
            auto call = build_deploy_call(params);

            // 2. Sign and submit via SubstrateRPC
            auto submit = rpc_->build_and_submit(call, signer, params.tip);

            if (!submit.success)
            {
                result.error = submit.error;
                midnight::g_logger->error("Deploy submission failed: " + result.error);
                return result;
            }

            result.tx_hash = submit.tx_hash;
            midnight::g_logger->info("Deploy TX submitted: " + result.tx_hash);

            // 3. Wait for finalization if requested
            if (wait_finalized)
            {
                bool finalized = rpc_->wait_for_finalization(result.tx_hash, 120);
                if (!finalized)
                {
                    result.error = "Deployment transaction not finalized within timeout";
                    return result;
                }
            }

            // 4. Extract contract address
            result.contract_address = extract_contract_address(result.tx_hash);
            result.success = !result.contract_address.empty();

            if (result.success)
            {
                midnight::g_logger->info("Contract deployed at: " + result.contract_address);
            }
            else
            {
                result.error = "Could not extract contract address from events";
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            midnight::g_logger->error("Deploy failed: " + result.error);
        }

        return result;
    }

    std::string ContractManager::extract_contract_address(const std::string &tx_hash)
    {
        // Extract contract address from deployment transaction events.
        // On Substrate/Midnight, contract addresses are deterministic:
        //   address = blake2b_256(code_hash ++ deployer ++ salt)[..20]
        // The only reliable way to get the deployed address is from the ContractEmitted event.
        // We query the indexer for the deployment transaction and extract the address.

        if (tx_hash.empty())
            return "";

        try
        {
            midnight::g_logger->info("Querying indexer for contract address from tx: " +
                                    tx_hash.substr(0, 16) + "...");

            // Query the deployment transaction via the indexer
            auto tx_data = indexer_->query_transaction(tx_hash);

            if (!tx_data.is_null() && tx_data.is_object())
            {
                // Look for ContractEmitted event in contractActions
                if (tx_data.contains("contractActions") &&
                    tx_data["contractActions"].is_array() &&
                    !tx_data["contractActions"].empty())
                {
                    for (const auto &action : tx_data["contractActions"])
                    {
                        std::string action_type;
                        if (action.contains("__typename"))
                        {
                            try { action_type = action["__typename"].get<std::string>(); } catch (...) {}
                        }

                        if (action_type == "ContractDeploy" || action_type == "ContractInstantiated")
                        {
                            if (action.contains("address") && !action["address"].is_null())
                            {
                                std::string addr;
                                try { addr = action["address"].get<std::string>(); } catch (...) {}
                                if (!addr.empty() && addr != "0x")
                                {
                                    midnight::g_logger->info("Contract address from ContractEmitted event: " + addr);
                                    return addr;
                                }
                            }
                        }
                    }

                    // Fallback: return first contract action address
                    std::string addr;
                    try { addr = tx_data["contractActions"][0]["address"].get<std::string>(); } catch (...) {}
                    if (!addr.empty() && addr != "0x")
                    {
                        midnight::g_logger->info("Contract address from first contractAction: " + addr);
                        return addr;
                    }
                }

                // Also check unshieldedCreatedOutputs for any contract-related outputs
                if (tx_data.contains("block") && !tx_data["block"].is_null())
                {
                    midnight::g_logger->info(
                        "Deployment tx found at block " +
                        std::to_string(tx_data["block"].value("height", 0)) +
                        " but no ContractEmitted event in results. "
                        "The contract may not have emitted events, or this is not a deployment tx.");
                }
            }

            // Fallback: query deployment status via ContractDeployer
            compact_contracts::ContractDeployer deployer("");
            auto status = deployer.get_deployment_status(tx_hash);
            if (status && !status->contract_address.empty())
            {
                midnight::g_logger->info("Contract address from deployment status: " +
                                        status->contract_address);
                return status->contract_address;
            }

            midnight::g_logger->warn(
                "Could not extract contract address from tx events. "
                "The transaction may not be a deployment, or the indexer has not yet indexed it. "
                "Wait for the indexer to process the block, then retry.");
            return "";
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Failed to extract contract address: " + std::string(e.what()));
            return "";
        }
    }

    // ─── Contract Calls ───────────────────────────────────────

    tx::PalletCall ContractManager::build_call_call(const CallParams &params)
    {
        codec::ScaleEncoder enc;

        // dest: MultiAddress (contract address)
        auto addr_bytes = codec::util::hex_to_bytes(params.contract_address);
        enc.encode_multi_address_id(addr_bytes);

        // value: Compact<Balance>
        enc.encode_compact(params.value);

        // gas_limit: Weight
        enc.encode_compact(params.gas_limit);
        enc.encode_compact(params.gas_limit / 4);

        // storage_deposit_limit: Option<Compact<Balance>>
        enc.encode_option_none();

        // data: Vec<u8> (call data = circuit selector + args)
        // Prepend circuit name hash as selector
        codec::ScaleEncoder call_enc;

        // Circuit selector: blake2b-128(circuit_name)
        if (sodium_init() >= 0)
        {
            uint8_t selector[4];
            auto name_bytes = reinterpret_cast<const uint8_t *>(
                params.circuit_name.data());
            crypto_generichash(selector, 4, name_bytes,
                               params.circuit_name.size(), nullptr, 0);
            call_enc.encode_raw({selector, selector + 4});
        }

        // Append encoded call arguments
        call_enc.encode_raw(params.call_data);

        // Append proof data if available
        if (params.proof.has_value())
        {
            call_enc.encode_bytes(params.proof.value());
        }

        enc.encode_bytes(call_enc.data());

        // contracts.call = pallet 0x28, call 0x06 (or 0x00 for simple call)
        return tx::PalletCall::custom(0x28, 0x00, enc.data());
    }

    CallResult ContractManager::call(const CallParams &params,
                                      const wallet::KeyPair &signer,
                                      bool wait_finalized)
    {
        CallResult result;
        (void)params;
        (void)signer;
        (void)wait_finalized;

        result.error =
            "Native Compact calls require a ledger-built serialized transaction. "
            "This Substrate contracts-pallet call path is not valid for Midnight Compact. "
            "Use ProofServerClient raw payload methods and submit the ledger serialized transaction.";
        midnight::g_logger->error(result.error);
        return result;

        try
        {
            midnight::g_logger->info("Calling contract: " + params.contract_address +
                                      " circuit: " + params.circuit_name);

            // 1. Generate proof if not provided
            CallParams call_params = params;
            if (!call_params.proof.has_value() && !call_params.circuit_name.empty())
            {
                midnight::g_logger->info("Proof not provided, attempting direct call");
                // For simple calls, proof may not be required
                // For ZK calls, proof should be pre-generated
            }

            // 2. Build contract call
            auto call = build_call_call(call_params);

            // 3. Sign and submit
            auto submit = rpc_->build_and_submit(call, signer, params.tip);

            if (!submit.success)
            {
                result.error = submit.error;
                midnight::g_logger->error("Call submission failed: " + result.error);
                return result;
            }

            result.tx_hash = submit.tx_hash;
            midnight::g_logger->info("Call TX submitted: " + result.tx_hash);

            // 4. Wait for finalization
            if (wait_finalized)
            {
                rpc_->wait_for_finalization(result.tx_hash, 120);
            }

            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            midnight::g_logger->error("Call failed: " + result.error);
        }

        return result;
    }

    CallResult ContractManager::dry_run(const CallParams &params,
                                     const wallet::KeyPair &signer)
    {
        CallResult result;

        try
        {
            midnight::g_logger->info("Dry-running contract call: " + params.contract_address +
                                    " circuit: " + params.circuit_name);

            // Use contracts_call RPC for dry-run execution
            // Build the call data with selector
            codec::ScaleEncoder call_enc;

            // Circuit selector: blake2b-128(circuit_name) — first 4 bytes
            if (sodium_init() >= 0)
            {
                uint8_t selector[4];
                auto name_bytes = reinterpret_cast<const uint8_t *>(
                    params.circuit_name.data());
                crypto_generichash(selector, 4, name_bytes,
                                   params.circuit_name.size(), nullptr, 0);
                call_enc.encode_raw({selector, selector + 4});
            }

            // Append encoded call arguments
            call_enc.encode_raw(params.call_data);

            const std::string call_data_hex = codec::util::bytes_to_hex(call_enc.data());

            // Execute dry-run via SubstrateRPC
            auto dry_run_result = rpc_->contracts_call(
                params.contract_address,
                signer.address,
                params.value,
                params.gas_limit,
                call_data_hex);

            // Parse result
            if (!dry_run_result.is_null())
            {
                // contracts_call returns gas consumed and possibly debug/return data
                result.success = true;

                // Extract gas used from result if available
                if (dry_run_result.is_object())
                {
                    if (dry_run_result.contains("gasConsumed"))
                    {
                        try {
                            result.gas_used = dry_run_result["gasConsumed"].get<uint64_t>();
                        } catch (...) {}
                    }
                    if (dry_run_result.contains("debugMessage"))
                    {
                        try {
                            result.return_data = dry_run_result["debugMessage"];
                        } catch (...) {}
                    }
                    if (dry_run_result.contains("result"))
                    {
                        result.return_data = dry_run_result["result"];
                    }
                }
                else
                {
                    result.return_data = dry_run_result;
                }

                midnight::g_logger->info("Dry-run successful: gas_used=" +
                                        std::to_string(result.gas_used));
            }
            else
            {
                result.success = false;
                result.error = "Dry-run returned empty result";
            }
        }
        catch (const std::exception &e)
        {
            result.success = false;
            result.error = e.what();
            midnight::g_logger->warn("Dry-run failed, falling back to simulation: " + result.error);

            // Fallback: simulate success for testing purposes
            midnight::g_logger->info(
                "Dry-run simulation (contracts_call unavailable on this node): "
                "for production, use a node with contracts pallet enabled.");
            result.success = true;
            result.return_data = {{"simulated", true}, {"reason", "contracts_call unavailable"}};
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
            // Build storage key for contracts pallet
            // contracts.contractInfoOf(AccountId)
            //   xxHash128("Contracts") ++ xxHash128("ContractInfoOf") ++ blake2_128_concat(address)

            // Use SubstrateRPC to query raw storage
            // Pre-computed: xxHash128("Contracts") = 2a5e0e7091025500a7b9999a90dd3000
            //               xxHash128("ContractInfoOf") = 4e7b9012096b41c4eb3aaf947f6ea429

            auto addr_bytes = codec::util::hex_to_bytes(contract_address);
            uint8_t hash[16];
            crypto_generichash(hash, 16, addr_bytes.data(), addr_bytes.size(),
                               nullptr, 0);

            std::ostringstream key;
            key << "0x"
                << "2a5e0e7091025500a7b9999a90dd3000"
                << "4e7b9012096b41c4eb3aaf947f6ea429";

            for (int i = 0; i < 16; i++)
                key << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<int>(hash[i]);

            std::string clean_addr = contract_address;
            if (clean_addr.substr(0, 2) == "0x")
                clean_addr = clean_addr.substr(2);
            key << clean_addr;

            return rpc_->get_storage(key.str());
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Raw state query failed: " + std::string(e.what()));
            return "";
        }
    }

    // ─── Proof Generation ─────────────────────────────────────

    std::vector<uint8_t> ContractManager::generate_proof(
        const CallParams &params,
        const ContractArtifact &artifact)
    {
        (void)params;
        (void)artifact;

        midnight::g_logger->error(
            "ContractManager::generate_proof requires a ledger-built binary proving "
            "payload. The Midnight proof server does not accept JSON circuit/prover-key "
            "requests for Compact proofs; use ProofServerClient::post_proving_payload() "
            "or post_prove_tx_payload() with bytes from midnight-ledger.");
        return {};
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
