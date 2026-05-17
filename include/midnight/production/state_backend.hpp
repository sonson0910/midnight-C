#pragma once

#include "midnight/ledger/ledger_backend.hpp"
#include "midnight/production/errors.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace midnight::production
{
    using json = nlohmann::json;

    enum class StateBackendMode
    {
        LocalCache,
        Snapshot,
        Managed
    };

    inline std::string state_backend_mode_to_string(StateBackendMode mode)
    {
        switch (mode)
        {
        case StateBackendMode::Snapshot:
            return "snapshot";
        case StateBackendMode::Managed:
            return "managed";
        case StateBackendMode::LocalCache:
        default:
            return "local-cache";
        }
    }

    inline StateBackendMode state_backend_mode_from_string(std::string value)
    {
        for (auto &c : value)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
            else if (c == '_')
            {
                c = '-';
            }
        }
        if (value == "managed" || value == "hosted" || value == "state-service")
        {
            return StateBackendMode::Managed;
        }
        if (value == "snapshot" || value == "snapshot-cache")
        {
            return StateBackendMode::Snapshot;
        }
        return StateBackendMode::LocalCache;
    }

    struct ManagedStateServiceConfig
    {
        std::string base_url;
        std::string api_key;
        uint32_t timeout_ms = 10000;
    };

    struct StateStatus
    {
        bool success = false;
        bool ready = false;
        bool stale = false;
        StateBackendMode backend_mode = StateBackendMode::LocalCache;
        ledger::SourceMode source_mode = ledger::SourceMode::Auto;
        std::string network;
        std::string fetch_cache_path;
        std::string ledger_state_path;
        bool fetch_cache_available = false;
        bool ledger_state_available = false;
        uint64_t fetch_cache_bytes = 0;
        uint64_t ledger_state_bytes = 0;
        uint64_t local_checkpoint = 0;
        bool local_checkpoint_available = false;
        uint64_t remote_tip_height = 0;
        uint64_t lag_blocks = 0;
        std::string error;
        std::string detail;
        json raw = json::object();
    };

    struct StateRefreshOptions
    {
        uint64_t max_lag_blocks = 100;
        bool force = false;
        bool allow_source_fetch = true;
        uint64_t timeout_ms = 0;
    };

    struct StateRefreshResult
    {
        bool success = false;
        bool skipped = false;
        StateStatus before;
        StateStatus after;
        ledger::BuildResult sync_result;
        SdkError error;
    };

    struct SnapshotResult
    {
        bool success = false;
        std::filesystem::path path;
        json manifest = json::object();
        SdkError error;
    };

    class IStateBackend
    {
    public:
        virtual ~IStateBackend() = default;

        virtual StateStatus status(
            const ledger::SourceConfig &source,
            std::optional<uint64_t> remote_tip_height = std::nullopt) = 0;

        virtual StateRefreshResult refresh(
            const ledger::SyncLedgerStateParams &params,
            const StateRefreshOptions &options = {}) = 0;

        virtual SnapshotResult export_snapshot(
            const ledger::SourceConfig &source,
            const std::filesystem::path &snapshot_dir) = 0;

        virtual SnapshotResult import_snapshot(
            const std::filesystem::path &snapshot_dir,
            const ledger::SourceConfig &destination) = 0;
    };

    class LocalStateBackend final : public IStateBackend
    {
    public:
        explicit LocalStateBackend(
            std::shared_ptr<ledger::ILedgerBackend> ledger_backend,
            StateBackendMode mode = StateBackendMode::LocalCache);

        StateStatus status(
            const ledger::SourceConfig &source,
            std::optional<uint64_t> remote_tip_height = std::nullopt) override;

        StateRefreshResult refresh(
            const ledger::SyncLedgerStateParams &params,
            const StateRefreshOptions &options = {}) override;

        SnapshotResult export_snapshot(
            const ledger::SourceConfig &source,
            const std::filesystem::path &snapshot_dir) override;

        SnapshotResult import_snapshot(
            const std::filesystem::path &snapshot_dir,
            const ledger::SourceConfig &destination) override;

    private:
        std::shared_ptr<ledger::ILedgerBackend> ledger_backend_;
        StateBackendMode mode_ = StateBackendMode::LocalCache;
    };

    class ManagedStateBackend final : public IStateBackend
    {
    public:
        explicit ManagedStateBackend(ManagedStateServiceConfig config);

        StateStatus status(
            const ledger::SourceConfig &source,
            std::optional<uint64_t> remote_tip_height = std::nullopt) override;

        StateRefreshResult refresh(
            const ledger::SyncLedgerStateParams &params,
            const StateRefreshOptions &options = {}) override;

        SnapshotResult export_snapshot(
            const ledger::SourceConfig &source,
            const std::filesystem::path &snapshot_dir) override;

        SnapshotResult import_snapshot(
            const std::filesystem::path &snapshot_dir,
            const ledger::SourceConfig &destination) override;

    private:
        ManagedStateServiceConfig config_;
    };

    std::shared_ptr<IStateBackend> make_local_state_backend(
        std::shared_ptr<ledger::ILedgerBackend> ledger_backend,
        StateBackendMode mode = StateBackendMode::LocalCache);

    std::shared_ptr<IStateBackend> make_managed_state_backend(
        ManagedStateServiceConfig config);

} // namespace midnight::production
