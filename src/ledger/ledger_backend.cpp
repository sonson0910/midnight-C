#include "midnight/ledger/ledger_backend.hpp"
#include "midnight/core/common_utils.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace midnight::ledger
{
    namespace
    {
        using json = nlohmann::json;

        void source_to_json(json &j, const SourceConfig &source)
        {
            j["src_url"] = source.src_url;
            j["src_files"] = source.src_files;
            j["fetch_only_cached"] = source.fetch_only_cached;
            j["ignore_block_context"] = source.ignore_block_context;
            j["dust_warp"] = source.dust_warp;
            j["fetch_cache"] = source.fetch_cache;
            j["ledger_state_db"] = source.ledger_state_db;
            j["from_block"] = source.from_block ? json(*source.from_block) : json(nullptr);
            j["to_block"] = source.to_block ? json(*source.to_block) : json(nullptr);
            j["transaction_hashes"] = source.transaction_hashes;
        }

        json transfer_to_json(const TransferNightParams &params)
        {
            json j;
            source_to_json(j["source"], params.source);
            j["proof_server"] = params.proof_server ? json(*params.proof_server) : json(nullptr);
            j["source_seed"] = params.source_seed;
            j["funding_seed"] = params.funding_seed ? json(*params.funding_seed) : json(nullptr);
            j["destination_addresses"] = params.destination_addresses;
            j["amount"] = params.amount;
            j["token_type"] = params.token_type;
            j["input_utxos"] = params.input_utxos;
            j["rng_seed"] = params.rng_seed ? json(*params.rng_seed) : json(nullptr);
            j["coin_selection"] = params.coin_selection;
            return j;
        }

        json register_dust_to_json(const RegisterDustParams &params)
        {
            json j;
            source_to_json(j["source"], params.source);
            j["proof_server"] = params.proof_server ? json(*params.proof_server) : json(nullptr);
            j["wallet_seed"] = params.wallet_seed;
            j["funding_seed"] = params.funding_seed ? json(*params.funding_seed) : json(nullptr);
            j["destination_dust_address"] = params.destination_dust_address ? json(*params.destination_dust_address) : json(nullptr);
            j["rng_seed"] = params.rng_seed ? json(*params.rng_seed) : json(nullptr);
            return j;
        }

        json deregister_dust_to_json(const DeregisterDustParams &params)
        {
            json j;
            source_to_json(j["source"], params.source);
            j["proof_server"] = params.proof_server ? json(*params.proof_server) : json(nullptr);
            j["wallet_seed"] = params.wallet_seed;
            j["funding_seed"] = params.funding_seed;
            j["rng_seed"] = params.rng_seed ? json(*params.rng_seed) : json(nullptr);
            return j;
        }

        json deploy_to_json(const SimpleContractDeployParams &params)
        {
            json j;
            source_to_json(j["source"], params.source);
            j["proof_server"] = params.proof_server ? json(*params.proof_server) : json(nullptr);
            j["funding_seed"] = params.funding_seed;
            j["authority_seeds"] = params.authority_seeds;
            j["authority_threshold"] = params.authority_threshold ? json(*params.authority_threshold) : json(nullptr);
            j["rng_seed"] = params.rng_seed ? json(*params.rng_seed) : json(nullptr);
            return j;
        }

        json call_to_json(const SimpleContractCallParams &params)
        {
            json j;
            source_to_json(j["source"], params.source);
            j["proof_server"] = params.proof_server ? json(*params.proof_server) : json(nullptr);
            j["funding_seed"] = params.funding_seed;
            j["contract_address"] = params.contract_address;
            j["call_key"] = params.call_key;
            j["fee"] = params.fee;
            j["rng_seed"] = params.rng_seed ? json(*params.rng_seed) : json(nullptr);
            return j;
        }

        json custom_to_json(const CustomContractIntentParams &params)
        {
            json j;
            source_to_json(j["source"], params.source);
            j["proof_server"] = params.proof_server ? json(*params.proof_server) : json(nullptr);
            j["funding_seed"] = params.funding_seed;
            j["compiled_contract_dirs"] = params.compiled_contract_dirs;
            j["intent_files"] = params.intent_files;
            j["input_utxos"] = params.input_utxos;
            j["zswap_state_file"] = params.zswap_state_file ? json(*params.zswap_state_file) : json(nullptr);
            j["shielded_destinations"] = params.shielded_destinations;
            j["rng_seed"] = params.rng_seed ? json(*params.rng_seed) : json(nullptr);
            return j;
        }

        json sync_to_json(const SyncLedgerStateParams &params)
        {
            json j;
            source_to_json(j["source"], params.source);
            j["wallet_seeds"] = params.wallet_seeds;
            j["timeout_ms"] = params.timeout_ms;
            return j;
        }

        BuildResult build_result_from_json(const json &j)
        {
            BuildResult result;
            result.success = j.value("success", false);
            result.transaction_hex = j.value("transaction_hex", "");
            result.transaction_hash = j.value("transaction_hash", "");
            result.contract_address = j.value("contract_address", "");
            result.output_file = j.value("output_file", "");
            result.intent_path = j.value("intent_path", "");
            result.private_state_path = j.value("private_state_path", "");
            result.zswap_state_path = j.value("zswap_state_path", "");
            result.onchain_state_path = j.value("onchain_state_path", "");
            result.result_path = j.value("result_path", "");
            result.raw_output = j.value("raw_output", "");
            result.command = j.value("command", "");
            result.log = j.value("log", "");
            result.error = j.value("error", "");
            if (!result.transaction_hex.empty())
            {
                const auto hex = midnight::util::strip_hex_prefix(result.transaction_hex);
                if (hex.size() % 2 != 0 || !midnight::util::is_hex_string(hex))
                {
                    result.success = false;
                    result.error = "Native Midnight ledger FFI returned invalid transaction_hex";
                    return result;
                }
                result.transaction_bytes = midnight::util::hex_to_bytes(hex);
            }
            return result;
        }

        WalletAddressDerivationResult wallet_addresses_from_json(const json &j)
        {
            WalletAddressDerivationResult result;
            result.success = j.value("success", false);
            result.network = j.value("network", "");
            result.shielded = j.value("shielded", "");
            result.unshielded = j.value("unshielded", "");
            result.dust = j.value("dust", "");
            result.dust_public = j.value("dust_public", "");
            result.coin_public = j.value("coin_public", "");
            result.coin_public_tagged = j.value("coin_public_tagged", "");
            result.verifying_key = j.value("verifying_key", "");
            result.user_address = j.value("user_address", "");
            result.unshielded_user_address_untagged =
                j.value("unshielded_user_address_untagged", "");
            result.error = j.value("error", "");
            return result;
        }

        std::string default_library_name()
        {
#ifdef _WIN32
            return "midnight_ledger_ffi.dll";
#elif defined(__APPLE__)
            return "libmidnight_ledger_ffi.dylib";
#else
            return "libmidnight_ledger_ffi.so";
#endif
        }
    }

    BuildResult UnavailableLedgerBackend::unavailable(const char *operation)
    {
        BuildResult result;
        result.error = std::string(operation) +
                       " requires a native Midnight ledger backend. Configure FfiLedgerBackend with libmidnight_ledger_ffi.";
        return result;
    }

    BuildResult UnavailableLedgerBackend::build_transfer_night(const TransferNightParams &) { return unavailable("transfer_night"); }
    BuildResult UnavailableLedgerBackend::build_register_dust(const RegisterDustParams &) { return unavailable("register_dust"); }
    BuildResult UnavailableLedgerBackend::build_deregister_dust(const DeregisterDustParams &) { return unavailable("deregister_dust"); }
    BuildResult UnavailableLedgerBackend::build_simple_contract_deploy(const SimpleContractDeployParams &) { return unavailable("deploy_contract"); }
    BuildResult UnavailableLedgerBackend::build_simple_contract_call(const SimpleContractCallParams &) { return unavailable("call_contract"); }
    BuildResult UnavailableLedgerBackend::build_custom_contract_transaction(const CustomContractIntentParams &) { return unavailable("custom_contract_transaction"); }
    BuildResult UnavailableLedgerBackend::sync_ledger_state(const SyncLedgerStateParams &) { return unavailable("sync_ledger_state"); }

    FfiLedgerBackend::FfiLedgerBackend(std::string library_path)
    {
        if (library_path.empty())
        {
            library_path = default_library_name();
        }
#ifdef _WIN32
        HMODULE module = LoadLibraryA(library_path.c_str());
        handle_ = reinterpret_cast<void *>(module);
        if (!module)
        {
            last_error_ = "Could not load " + library_path;
            return;
        }
        build_fn_ = reinterpret_cast<BuildFn>(GetProcAddress(module, "midnight_ledger_build_transaction"));
        wallet_addresses_fn_ = reinterpret_cast<WalletAddressesFn>(GetProcAddress(module, "midnight_ledger_derive_wallet_addresses"));
        free_fn_ = reinterpret_cast<FreeFn>(GetProcAddress(module, "midnight_ledger_free_string"));
#else
        handle_ = dlopen(library_path.c_str(), RTLD_NOW);
        if (!handle_)
        {
            const char *err = dlerror();
            last_error_ = err ? err : ("Could not load " + library_path);
            return;
        }
        build_fn_ = reinterpret_cast<BuildFn>(dlsym(handle_, "midnight_ledger_build_transaction"));
        wallet_addresses_fn_ = reinterpret_cast<WalletAddressesFn>(dlsym(handle_, "midnight_ledger_derive_wallet_addresses"));
        free_fn_ = reinterpret_cast<FreeFn>(dlsym(handle_, "midnight_ledger_free_string"));
#endif
        if (!build_fn_ || !free_fn_)
        {
            last_error_ = "libmidnight_ledger_ffi is missing required symbols";
        }
    }

    FfiLedgerBackend::~FfiLedgerBackend()
    {
#ifdef _WIN32
        if (handle_)
        {
            FreeLibrary(reinterpret_cast<HMODULE>(handle_));
        }
#else
        if (handle_)
        {
            dlclose(handle_);
        }
#endif
    }

    WalletAddressDerivationResult FfiLedgerBackend::derive_wallet_addresses(
        const std::string &seed_hex_or_mnemonic,
        const std::string &network)
    {
        WalletAddressDerivationResult result;
        if (!can_derive_wallet_addresses())
        {
            result.error = last_error_.empty()
                ? "Native Midnight ledger FFI backend is missing midnight_ledger_derive_wallet_addresses"
                : last_error_;
            return result;
        }

        json request = {
            {"seed", seed_hex_or_mnemonic},
            {"network", network.empty() ? "preview" : network}};
        char *response_raw = nullptr;
        const int code = wallet_addresses_fn_(request.dump().c_str(), &response_raw);
        std::string response = response_raw ? response_raw : "";
        if (response_raw)
        {
            free_fn_(response_raw);
        }

        try
        {
            result = wallet_addresses_from_json(json::parse(response));
        }
        catch (const std::exception &e)
        {
            result.error = std::string("Invalid native Midnight wallet FFI response: ") + e.what();
            return result;
        }
        if (code != 0 && result.error.empty())
        {
            result.error = "Native Midnight wallet FFI call failed";
        }
        return result;
    }

    BuildResult FfiLedgerBackend::call_backend(const std::string &operation, const std::string &params_json)
    {
        BuildResult result;
        if (!is_available())
        {
            result.error = last_error_.empty()
                ? "Native Midnight ledger FFI backend is not available"
                : last_error_;
            return result;
        }

        json request = {
            {"operation", operation},
            {"params", json::parse(params_json)}};
        char *response_raw = nullptr;
        const int code = build_fn_(request.dump().c_str(), &response_raw);
        std::string response = response_raw ? response_raw : "";
        if (response_raw)
        {
            free_fn_(response_raw);
        }
        if (code != 0)
        {
            if (!response.empty())
            {
                try
                {
                    result = build_result_from_json(json::parse(response));
                    if (result.error.empty())
                    {
                        result.error = "Native Midnight ledger FFI call failed";
                    }
                    return result;
                }
                catch (const std::exception &)
                {
                    result.error = response;
                    return result;
                }
            }
            result.error = "Native Midnight ledger FFI call failed";
            return result;
        }

        try
        {
            result = build_result_from_json(json::parse(response));
        }
        catch (const std::exception &e)
        {
            result.error = std::string("Invalid native Midnight ledger FFI response: ") + e.what();
        }
        return result;
    }

    BuildResult FfiLedgerBackend::build_transfer_night(const TransferNightParams &params)
    {
        return call_backend("transfer_night", transfer_to_json(params).dump());
    }
    BuildResult FfiLedgerBackend::build_register_dust(const RegisterDustParams &params)
    {
        return call_backend("register_dust", register_dust_to_json(params).dump());
    }
    BuildResult FfiLedgerBackend::build_deregister_dust(const DeregisterDustParams &params)
    {
        return call_backend("deregister_dust", deregister_dust_to_json(params).dump());
    }
    BuildResult FfiLedgerBackend::build_simple_contract_deploy(const SimpleContractDeployParams &params)
    {
        return call_backend("deploy_simple_contract", deploy_to_json(params).dump());
    }
    BuildResult FfiLedgerBackend::build_simple_contract_call(const SimpleContractCallParams &params)
    {
        return call_backend("call_simple_contract", call_to_json(params).dump());
    }
    BuildResult FfiLedgerBackend::build_custom_contract_transaction(const CustomContractIntentParams &params)
    {
        return call_backend("custom_contract_transaction", custom_to_json(params).dump());
    }
    BuildResult FfiLedgerBackend::sync_ledger_state(const SyncLedgerStateParams &params)
    {
        return call_backend("sync_ledger_state", sync_to_json(params).dump());
    }

    std::shared_ptr<ILedgerBackend> make_unavailable_ledger_backend()
    {
        return std::make_shared<UnavailableLedgerBackend>();
    }

    std::shared_ptr<ILedgerBackend> make_ffi_ledger_backend(const std::string &library_path)
    {
        return std::make_shared<FfiLedgerBackend>(library_path);
    }

} // namespace midnight::ledger
