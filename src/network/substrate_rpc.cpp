#include "midnight/network/substrate_rpc.hpp"
#include "midnight/core/logger.hpp"
#include <sodium.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstring>

namespace midnight::network
{

    // ─── Constructor ──────────────────────────────────────────

    SubstrateRPC::SubstrateRPC(const std::string &node_url, uint32_t timeout_ms)
        : client_(std::make_unique<NetworkClient>(node_url, timeout_ms))
    {
        midnight::g_logger->info("SubstrateRPC initialized: " + node_url);
    }

    // ─── Low-level RPC ────────────────────────────────────────

    json SubstrateRPC::rpc_call(const std::string &method, const json &params)
    {
        json request = {
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", params},
            {"id", request_id_++}};

        midnight::g_logger->debug("Substrate RPC: " + method);

        json response = client_->post_json("/", request);

        if (response.contains("error") && !response["error"].is_null())
        {
            auto error = response["error"];
            std::string msg = error.value("message", "Unknown RPC error");
            int code = error.value("code", 0);
            throw std::runtime_error(
                "RPC error [" + std::to_string(code) + "]: " + msg);
        }

        if (!response.contains("result"))
        {
            throw std::runtime_error(
                "Invalid RPC response: missing 'result' field");
        }

        return response["result"];
    }

    // ─── Runtime Info ─────────────────────────────────────────

    RuntimeVersion SubstrateRPC::get_runtime_version()
    {
        if (cached_runtime_version_.has_value())
            return cached_runtime_version_.value();

        json result = rpc_call("state_getRuntimeVersion", json::array());

        RuntimeVersion v;
        v.spec_name = result.value("specName", "");
        v.impl_name = result.value("implName", "");
        v.spec_version = result.value("specVersion", 0u);
        v.impl_version = result.value("implVersion", 0u);
        v.tx_version = result.value("transactionVersion", 0u);
        v.authoring_version = result.value("authoringVersion", 0u);

        cached_runtime_version_ = v;

        std::ostringstream msg;
        msg << "Runtime: " << v.spec_name
            << " v" << v.spec_version
            << " tx_v" << v.tx_version;
        midnight::g_logger->info(msg.str());

        return v;
    }

    std::string SubstrateRPC::get_metadata_hex()
    {
        json result = rpc_call("state_getMetadata", json::array());
        if (result.is_string())
            return result.get<std::string>();
        throw std::runtime_error("Unexpected metadata format");
    }

    std::string SubstrateRPC::get_genesis_hash()
    {
        if (cached_genesis_hash_.has_value())
            return cached_genesis_hash_.value();

        json result = rpc_call("chain_getBlockHash", json::array({0}));
        if (!result.is_string())
            throw std::runtime_error("Unexpected genesis hash format");

        cached_genesis_hash_ = result.get<std::string>();
        midnight::g_logger->info("Genesis: " + cached_genesis_hash_.value());
        return cached_genesis_hash_.value();
    }

    std::string SubstrateRPC::get_finalized_head()
    {
        json result = rpc_call("chain_getFinalizedHead", json::array());
        if (result.is_string())
            return result.get<std::string>();
        throw std::runtime_error("Unexpected finalized head format");
    }

    std::string SubstrateRPC::get_block_hash(uint64_t block_number)
    {
        json result = rpc_call("chain_getBlockHash",
                               json::array({block_number}));
        if (result.is_string())
            return result.get<std::string>();
        throw std::runtime_error("Unexpected block hash format");
    }

    json SubstrateRPC::get_header(const std::string &block_hash)
    {
        json params = json::array();
        if (!block_hash.empty())
            params.push_back(block_hash);
        return rpc_call("chain_getHeader", params);
    }

    // ─── Account State ────────────────────────────────────────

    uint32_t SubstrateRPC::get_account_nonce(const std::string &account_id_hex)
    {
        // system_accountNextIndex returns the next valid nonce
        json result = rpc_call("system_accountNextIndex",
                               json::array({account_id_hex}));
        if (result.is_number())
            return result.get<uint32_t>();
        throw std::runtime_error("Unexpected nonce format");
    }

    std::string SubstrateRPC::compute_account_storage_key(
        const std::string &account_id_hex)
    {
        if (sodium_init() < 0)
            throw std::runtime_error("libsodium init failed");

        // Substrate storage key for System.Account(AccountId):
        //   xxHash128("System") ++ xxHash128("Account") ++ blake2_128_concat(AccountId)
        //
        // Pre-computed hashes:
        //   xxHash128("System")  = 26aa394eea5630e07c48ae0c9558cef7
        //   xxHash128("Account") = b99d880ec681799c0cf30e8886371da9

        std::string key = "0x"
                          "26aa394eea5630e07c48ae0c9558cef7"
                          "b99d880ec681799c0cf30e8886371da9";

        // blake2_128_concat(account_id):
        //   blake2b-128(account_id) ++ account_id

        std::string clean_hex = account_id_hex;
        if (clean_hex.substr(0, 2) == "0x")
            clean_hex = clean_hex.substr(2);

        // Convert account hex to bytes
        std::vector<uint8_t> account_bytes;
        for (size_t i = 0; i < clean_hex.size(); i += 2)
        {
            auto byte = static_cast<uint8_t>(
                std::stoul(clean_hex.substr(i, 2), nullptr, 16));
            account_bytes.push_back(byte);
        }

        // blake2b-128
        uint8_t hash[16];
        crypto_generichash(hash, 16,
                           account_bytes.data(), account_bytes.size(),
                           nullptr, 0);

        // Append blake2b hash
        std::ostringstream oss;
        for (int i = 0; i < 16; i++)
            oss << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<int>(hash[i]);

        // Append raw account bytes
        oss << clean_hex;

        key += oss.str();
        return key;
    }

    AccountInfo SubstrateRPC::get_account_info(const std::string &account_id_hex)
    {
        std::string storage_key = compute_account_storage_key(account_id_hex);
        std::string storage_hex = get_storage(storage_key);

        AccountInfo info;
        if (storage_hex.empty() || storage_hex == "null")
            return info;

        // Decode SCALE-encoded AccountInfo
        auto bytes = codec::util::hex_to_bytes(storage_hex);
        codec::ScaleDecoder dec(bytes);

        try
        {
            info.nonce = dec.decode_u32_le();
            info.consumers = dec.decode_u32_le();
            info.providers = dec.decode_u32_le();
            info.sufficients = dec.decode_u32_le();

            // AccountData: free, reserved, frozen (u128 each)
            // Read as u64 + skip upper u64 (assuming values fit in u64)
            info.free = dec.decode_u64_le();
            dec.decode_u64_le(); // upper 64 bits
            info.reserved = dec.decode_u64_le();
            dec.decode_u64_le(); // upper 64 bits
            info.frozen = dec.decode_u64_le();
            dec.decode_u64_le(); // upper 64 bits
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn(
                "AccountInfo decode partial: " + std::string(e.what()));
        }

        return info;
    }

    uint64_t SubstrateRPC::get_free_balance(const std::string &account_id_hex)
    {
        return get_account_info(account_id_hex).free;
    }

    std::string SubstrateRPC::get_storage(const std::string &storage_key)
    {
        json result = rpc_call("state_getStorage",
                               json::array({storage_key}));
        if (result.is_null())
            return "";
        if (result.is_string())
            return result.get<std::string>();
        throw std::runtime_error("Unexpected storage value format");
    }

    // ─── Transaction Submission ───────────────────────────────

    SubmitResult SubstrateRPC::submit_extrinsic(const std::string &extrinsic_hex)
    {
        SubmitResult result;

        try
        {
            json response = rpc_call("author_submitExtrinsic",
                                     json::array({extrinsic_hex}));

            if (response.is_string())
            {
                result.success = true;
                result.tx_hash = response.get<std::string>();
                midnight::g_logger->info("TX submitted: " + result.tx_hash);
            }
            else
            {
                result.error = "Unexpected response format";
            }
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
            midnight::g_logger->error("TX submit failed: " + result.error);
        }

        return result;
    }

    SubmitResult SubstrateRPC::build_and_submit(
        const tx::PalletCall &call,
        const wallet::KeyPair &key,
        uint64_t tip)
    {
        try
        {
            // 1. Fetch runtime version
            auto version = get_runtime_version();

            // 2. Fetch genesis hash
            auto genesis_hex = get_genesis_hash();
            auto genesis_bytes = codec::util::hex_to_bytes(genesis_hex);

            // 3. Fetch finalized head (for mortal era checkpoint)
            auto head_hex = get_finalized_head();
            auto head_bytes = codec::util::hex_to_bytes(head_hex);

            // 4. Get account nonce
            // Account ID = public key hex
            uint32_t nonce = get_account_nonce(key.address);

            // 5. Build ExtrinsicParams
            tx::ExtrinsicParams params;
            params.spec_version = version.spec_version;
            params.tx_version = version.tx_version;
            params.genesis_hash = genesis_bytes;
            params.block_hash = head_bytes;
            params.nonce = nonce;
            params.tip = tip;
            params.mortal_era = false; // immortal for simplicity

            // 6. Build and sign
            tx::ExtrinsicBuilder builder(params);
            auto extrinsic = builder.build_signed(
                call, key.secret_key, key.public_key);

            // 7. Submit
            auto hex = tx::ExtrinsicBuilder::to_hex(extrinsic);
            return submit_extrinsic(hex);
        }
        catch (const std::exception &e)
        {
            SubmitResult result;
            result.error = e.what();
            midnight::g_logger->error("build_and_submit failed: " + result.error);
            return result;
        }
    }

    bool SubstrateRPC::wait_for_finalization(const std::string &tx_hash,
                                              uint32_t timeout_seconds)
    {
        auto start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(timeout_seconds);

        while (true)
        {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > timeout)
            {
                midnight::g_logger->warn("TX finalization timeout: " + tx_hash);
                return false;
            }

            // Check pending pool — if TX was there and is now gone, it's included
            try
            {
                auto pending = get_pending_extrinsics();
                bool found = false;
                for (const auto &ext : pending)
                {
                    if (ext.find(tx_hash.substr(2)) != std::string::npos)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    // TX not in pool — either included or rejected
                    // Check if a new finalized block exists
                    midnight::g_logger->info("TX no longer pending, likely finalized: " + tx_hash);
                    return true;
                }
            }
            catch (const std::exception &e)
            {
                midnight::g_logger->debug("Finalization check error: " + std::string(e.what()));
            }

            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }

    // ─── System Info ──────────────────────────────────────────

    std::string SubstrateRPC::get_chain_name()
    {
        json result = rpc_call("system_chain", json::array());
        return result.is_string() ? result.get<std::string>() : "";
    }

    std::string SubstrateRPC::get_node_version()
    {
        json result = rpc_call("system_version", json::array());
        return result.is_string() ? result.get<std::string>() : "";
    }

    json SubstrateRPC::get_system_health()
    {
        return rpc_call("system_health", json::array());
    }

    bool SubstrateRPC::is_synced()
    {
        try
        {
            auto health = get_system_health();
            return !health.value("isSyncing", true);
        }
        catch (...)
        {
            return false;
        }
    }

    std::vector<std::string> SubstrateRPC::get_pending_extrinsics()
    {
        json result = rpc_call("author_pendingExtrinsics", json::array());
        std::vector<std::string> pending;
        if (result.is_array())
        {
            for (const auto &ext : result)
            {
                if (ext.is_string())
                    pending.push_back(ext.get<std::string>());
            }
        }
        return pending;
    }

    // ─── Midnight-specific RPC ───────────────────────────────

    json SubstrateRPC::midnight_ledger_version()
    {
        try
        {
            return rpc_call("midnight_ledgerVersion", json::array());
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("midnight_ledgerVersion failed: " + std::string(e.what()));
            return json::object();
        }
    }

    std::string SubstrateRPC::midnight_zswap_state_root()
    {
        try
        {
            json result = rpc_call("midnight_zswapStateRoot", json::array());
            if (result.is_string())
                return result.get<std::string>();
            return result.dump();
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("midnight_zswapStateRoot failed: " + std::string(e.what()));
            return "";
        }
    }

    json SubstrateRPC::midnight_contract_state(const std::string &contract_address)
    {
        try
        {
            return rpc_call("midnight_contractState",
                            json::array({contract_address}));
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("midnight_contractState failed: " + std::string(e.what()));
            return json::object();
        }
    }

    json SubstrateRPC::midnight_api_versions()
    {
        try
        {
            return rpc_call("midnight_apiVersions", json::array());
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("midnight_apiVersions failed: " + std::string(e.what()));
            return json::object();
        }
    }

} // namespace midnight::network

