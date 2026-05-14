#pragma once

#include "midnight/ledger/transaction_types.hpp"
#include <memory>
#include <string>

namespace midnight::ledger
{
    struct WalletAddressDerivationResult
    {
        bool success = false;
        std::string network;
        std::string shielded;
        std::string unshielded;
        std::string dust;
        std::string dust_public;
        std::string coin_public;
        std::string coin_public_tagged;
        std::string verifying_key;
        std::string user_address;
        std::string unshielded_user_address_untagged;
        std::string error;
    };

    /**
     * @brief Production ledger transaction construction backend.
     *
     * C++ network code can submit raw Midnight transaction bytes, but the bytes
     * must be produced by the official ledger implementation. This interface
     * keeps production code independent from CLI/toolkit processes and allows a
     * native Rust/C ABI ledger library or future pure C++ port to be plugged in.
     */
    class ILedgerBackend
    {
    public:
        virtual ~ILedgerBackend() = default;

        virtual BuildResult build_transfer_night(const TransferNightParams &params) = 0;
        virtual BuildResult build_register_dust(const RegisterDustParams &params) = 0;
        virtual BuildResult build_deregister_dust(const DeregisterDustParams &params) = 0;
        virtual BuildResult build_simple_contract_deploy(const SimpleContractDeployParams &params) = 0;
        virtual BuildResult build_simple_contract_call(const SimpleContractCallParams &params) = 0;
        virtual BuildResult build_custom_contract_transaction(const CustomContractIntentParams &params) = 0;
        virtual BuildResult sync_ledger_state(const SyncLedgerStateParams &params) = 0;
    };

    /**
     * @brief Backend returned when no native ledger implementation is configured.
     */
    class UnavailableLedgerBackend final : public ILedgerBackend
    {
    public:
        BuildResult build_transfer_night(const TransferNightParams &params) override;
        BuildResult build_register_dust(const RegisterDustParams &params) override;
        BuildResult build_deregister_dust(const DeregisterDustParams &params) override;
        BuildResult build_simple_contract_deploy(const SimpleContractDeployParams &params) override;
        BuildResult build_simple_contract_call(const SimpleContractCallParams &params) override;
        BuildResult build_custom_contract_transaction(const CustomContractIntentParams &params) override;
        BuildResult sync_ledger_state(const SyncLedgerStateParams &params) override;

    private:
        static BuildResult unavailable(const char *operation);
    };

    /**
     * @brief Native FFI backend for libmidnight_ledger_ffi.
     *
     * Expected C ABI:
     *   int midnight_ledger_build_transaction(const char* request_json, char** response_json);
     *   void midnight_ledger_free_string(char* value);
     *
     * Request JSON contains { "operation": "...", "params": ... }.
     * Response JSON contains the same fields as BuildResult.
     */
    class FfiLedgerBackend final : public ILedgerBackend
    {
    public:
        explicit FfiLedgerBackend(std::string library_path = {});
        ~FfiLedgerBackend() override;

        FfiLedgerBackend(const FfiLedgerBackend &) = delete;
        FfiLedgerBackend &operator=(const FfiLedgerBackend &) = delete;

        bool is_available() const { return build_fn_ != nullptr && free_fn_ != nullptr; }
        bool can_derive_wallet_addresses() const { return wallet_addresses_fn_ != nullptr && free_fn_ != nullptr; }
        std::string last_error() const { return last_error_; }

        WalletAddressDerivationResult derive_wallet_addresses(
            const std::string &seed_hex_or_mnemonic,
            const std::string &network = "preview");

        BuildResult build_transfer_night(const TransferNightParams &params) override;
        BuildResult build_register_dust(const RegisterDustParams &params) override;
        BuildResult build_deregister_dust(const DeregisterDustParams &params) override;
        BuildResult build_simple_contract_deploy(const SimpleContractDeployParams &params) override;
        BuildResult build_simple_contract_call(const SimpleContractCallParams &params) override;
        BuildResult build_custom_contract_transaction(const CustomContractIntentParams &params) override;
        BuildResult sync_ledger_state(const SyncLedgerStateParams &params) override;

    private:
        using BuildFn = int (*)(const char *, char **);
        using WalletAddressesFn = int (*)(const char *, char **);
        using FreeFn = void (*)(char *);

        void *handle_ = nullptr;
        BuildFn build_fn_ = nullptr;
        WalletAddressesFn wallet_addresses_fn_ = nullptr;
        FreeFn free_fn_ = nullptr;
        std::string last_error_;

        BuildResult call_backend(const std::string &operation, const std::string &params_json);
    };

    std::shared_ptr<ILedgerBackend> make_unavailable_ledger_backend();
    std::shared_ptr<ILedgerBackend> make_ffi_ledger_backend(const std::string &library_path = {});

} // namespace midnight::ledger
