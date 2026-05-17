#include "midnight/production/state_backend.hpp"
#include "midnight/network/network_client.hpp"
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace midnight::production
{
    namespace
    {
        SdkError make_error(ErrorCode code, std::string message, std::string detail = {})
        {
            return {.code = code, .message = std::move(message), .detail = std::move(detail)};
        }

        std::string strip_redb_prefix(const std::string &value)
        {
            constexpr const char *prefix = "redb:";
            if (value.rfind(prefix, 0) == 0)
            {
                return value.substr(5);
            }
            return value;
        }

        bool is_remote_path(const std::string &path)
        {
            return path.find("://") != std::string::npos &&
                   path.rfind("redb:", 0) != 0;
        }

        uint64_t path_size(const std::filesystem::path &path)
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
            {
                return 0;
            }
            if (std::filesystem::is_regular_file(path, ec))
            {
                const auto size = std::filesystem::file_size(path, ec);
                return ec ? 0 : size;
            }
            if (!std::filesystem::is_directory(path, ec))
            {
                return 0;
            }

            uint64_t total = 0;
            for (const auto &entry : std::filesystem::recursive_directory_iterator(path, ec))
            {
                if (ec)
                {
                    return total;
                }
                if (entry.is_regular_file(ec))
                {
                    const auto size = entry.file_size(ec);
                    if (!ec)
                    {
                        total += size;
                    }
                }
            }
            return total;
        }

        void copy_path(
            const std::filesystem::path &from,
            const std::filesystem::path &to)
        {
            std::error_code ec;
            if (!std::filesystem::exists(from, ec))
            {
                return;
            }
            if (!to.parent_path().empty())
            {
                std::filesystem::create_directories(to.parent_path(), ec);
            }
            if (std::filesystem::is_regular_file(from, ec))
            {
                std::filesystem::copy_file(
                    from,
                    to,
                    std::filesystem::copy_options::overwrite_existing,
                    ec);
                if (ec)
                {
                    throw std::runtime_error(ec.message());
                }
                return;
            }
            if (std::filesystem::is_directory(from, ec))
            {
                std::filesystem::create_directories(to, ec);
                if (ec)
                {
                    throw std::runtime_error(ec.message());
                }
                std::filesystem::copy(
                    from,
                    to,
                    std::filesystem::copy_options::recursive |
                        std::filesystem::copy_options::overwrite_existing,
                    ec);
                if (ec)
                {
                    throw std::runtime_error(ec.message());
                }
            }
        }

        json source_to_json(const ledger::SourceConfig &source)
        {
            json value = json::object();
            value["source_mode"] = ledger::source_mode_to_string(source.source_mode);
            value["src_url"] = source.src_url;
            value["src_files"] = source.src_files;
            value["fetch_only_cached"] = source.fetch_only_cached;
            value["require_ledger_state_cache"] = source.require_ledger_state_cache;
            value["ignore_block_context"] = source.ignore_block_context;
            value["dust_warp"] = source.dust_warp;
            value["fetch_cache"] = source.fetch_cache;
            value["ledger_state_db"] = source.ledger_state_db;
            value["fetch_concurrency"] = source.fetch_concurrency;
            value["fetch_compute_concurrency"] = source.fetch_compute_concurrency
                                                    ? json(*source.fetch_compute_concurrency)
                                                    : json(nullptr);
            value["fetch_retry_count"] = source.fetch_retry_count;
            value["fetch_retry_delay_ms"] = source.fetch_retry_delay_ms;
            value["from_block"] = source.from_block ? json(*source.from_block) : json(nullptr);
            value["to_block"] = source.to_block ? json(*source.to_block) : json(nullptr);
            value["transaction_hashes"] = source.transaction_hashes;
            return value;
        }

        StateStatus status_from_json(
            const json &value,
            StateBackendMode mode,
            const ledger::SourceConfig &source)
        {
            StateStatus status;
            status.success = value.value("success", true);
            status.ready = value.value("ready", false);
            status.stale = value.value("stale", false);
            status.backend_mode = mode;
            status.source_mode = source.source_mode;
            status.network = value.value("network", "");
            status.fetch_cache_path = value.value("fetch_cache_path", strip_redb_prefix(source.fetch_cache));
            status.ledger_state_path = value.value("ledger_state_path", source.ledger_state_db);
            status.fetch_cache_available = value.value("fetch_cache_available", false);
            status.ledger_state_available = value.value("ledger_state_available", false);
            status.fetch_cache_bytes = value.value("fetch_cache_bytes", 0ull);
            status.ledger_state_bytes = value.value("ledger_state_bytes", 0ull);
            status.local_checkpoint = value.value("local_checkpoint", 0ull);
            status.local_checkpoint_available =
                value.value("local_checkpoint_available", status.local_checkpoint > 0);
            status.remote_tip_height = value.value("remote_tip_height", 0ull);
            status.lag_blocks = value.value("lag_blocks", 0ull);
            status.error = value.value("error", "");
            status.detail = value.value("detail", "");
            status.raw = value;
            return status;
        }

        json status_to_json(const StateStatus &status)
        {
            return {
                {"success", status.success},
                {"ready", status.ready},
                {"stale", status.stale},
                {"backend_mode", state_backend_mode_to_string(status.backend_mode)},
                {"source_mode", ledger::source_mode_to_string(status.source_mode)},
                {"network", status.network},
                {"fetch_cache_path", status.fetch_cache_path},
                {"ledger_state_path", status.ledger_state_path},
                {"fetch_cache_available", status.fetch_cache_available},
                {"ledger_state_available", status.ledger_state_available},
                {"fetch_cache_bytes", status.fetch_cache_bytes},
                {"ledger_state_bytes", status.ledger_state_bytes},
                {"local_checkpoint", status.local_checkpoint},
                {"local_checkpoint_available", status.local_checkpoint_available},
                {"remote_tip_height", status.remote_tip_height},
                {"lag_blocks", status.lag_blocks},
                {"error", status.error},
                {"detail", status.detail}};
        }
    }

    LocalStateBackend::LocalStateBackend(
        std::shared_ptr<ledger::ILedgerBackend> ledger_backend,
        StateBackendMode mode)
        : ledger_backend_(std::move(ledger_backend)),
          mode_(mode == StateBackendMode::Managed ? StateBackendMode::LocalCache : mode)
    {
    }

    StateStatus LocalStateBackend::status(
        const ledger::SourceConfig &source,
        std::optional<uint64_t> remote_tip_height)
    {
        StateStatus status;
        status.success = true;
        status.backend_mode = mode_;
        status.source_mode = source.source_mode;
        status.fetch_cache_path = strip_redb_prefix(source.fetch_cache);
        status.ledger_state_path = source.ledger_state_db;

        if (!status.fetch_cache_path.empty() && !is_remote_path(status.fetch_cache_path))
        {
            const std::filesystem::path fetch_path(status.fetch_cache_path);
            status.fetch_cache_bytes = path_size(fetch_path);
            status.fetch_cache_available = status.fetch_cache_bytes > 0;
        }
        if (!status.ledger_state_path.empty() && !is_remote_path(status.ledger_state_path))
        {
            const std::filesystem::path state_path(status.ledger_state_path);
            status.ledger_state_bytes = path_size(state_path);
            status.ledger_state_available = status.ledger_state_bytes > 0;
        }

        status.ready = status.ledger_state_available;
        status.remote_tip_height = remote_tip_height.value_or(0);
        status.stale = !status.ready;
        status.raw = status_to_json(status);
        return status;
    }

    StateRefreshResult LocalStateBackend::refresh(
        const ledger::SyncLedgerStateParams &params,
        const StateRefreshOptions &options)
    {
        StateRefreshResult result;
        result.before = status(params.source);

        if (!ledger_backend_)
        {
            result.error = make_error(
                ErrorCode::LedgerBackendError,
                "Local state refresh requires a ledger backend");
            result.after = result.before;
            return result;
        }

        if (!options.force && result.before.ready && !options.allow_source_fetch)
        {
            result.success = true;
            result.skipped = true;
            result.after = result.before;
            return result;
        }
        if (!options.allow_source_fetch && !result.before.ready)
        {
            result.error = make_error(
                ErrorCode::LedgerBuildError,
                "Local state cache is not ready and source fetch is disabled");
            result.after = result.before;
            return result;
        }

        auto sync_params = params;
        if (sync_params.timeout_ms == 0)
        {
            sync_params.timeout_ms = options.timeout_ms;
        }
        if (sync_params.source.source_mode == ledger::SourceMode::Auto ||
            sync_params.source.source_mode == ledger::SourceMode::LocalCache)
        {
            sync_params.source.source_mode = ledger::SourceMode::ColdSync;
            sync_params.source.fetch_only_cached = false;
        }

        result.sync_result = ledger_backend_->sync_ledger_state(sync_params);
        result.after = status(sync_params.source);
        result.success = result.sync_result.success;
        if (!result.success)
        {
            result.error = make_error(
                ErrorCode::LedgerBuildError,
                "Local state refresh failed",
                result.sync_result.error);
        }
        return result;
    }

    SnapshotResult LocalStateBackend::export_snapshot(
        const ledger::SourceConfig &source,
        const std::filesystem::path &snapshot_dir)
    {
        SnapshotResult result;
        result.path = snapshot_dir;
        try
        {
            std::filesystem::create_directories(snapshot_dir);
            const auto fetch_path = std::filesystem::path(strip_redb_prefix(source.fetch_cache));
            const auto state_path = std::filesystem::path(source.ledger_state_db);
            copy_path(fetch_path, snapshot_dir / "fetch_cache");
            copy_path(state_path, snapshot_dir / "ledger_state_db");

            result.manifest = {
                {"version", 1},
                {"source", source_to_json(source)},
                {"fetch_cache_entry", "fetch_cache"},
                {"ledger_state_entry", "ledger_state_db"},
                {"fetch_cache_bytes", path_size(snapshot_dir / "fetch_cache")},
                {"ledger_state_bytes", path_size(snapshot_dir / "ledger_state_db")}};

            std::ofstream manifest(snapshot_dir / "manifest.json");
            manifest << result.manifest.dump(2) << "\n";
            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = make_error(
                ErrorCode::ArtifactError,
                "State snapshot export failed",
                e.what());
        }
        return result;
    }

    SnapshotResult LocalStateBackend::import_snapshot(
        const std::filesystem::path &snapshot_dir,
        const ledger::SourceConfig &destination)
    {
        SnapshotResult result;
        result.path = snapshot_dir;
        try
        {
            const auto manifest_path = snapshot_dir / "manifest.json";
            if (std::filesystem::exists(manifest_path))
            {
                std::ifstream manifest(manifest_path);
                result.manifest = json::parse(manifest);
            }

            const auto fetch_destination =
                std::filesystem::path(strip_redb_prefix(destination.fetch_cache));
            const auto state_destination =
                std::filesystem::path(destination.ledger_state_db);
            copy_path(snapshot_dir / "fetch_cache", fetch_destination);
            copy_path(snapshot_dir / "ledger_state_db", state_destination);
            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = make_error(
                ErrorCode::ArtifactError,
                "State snapshot import failed",
                e.what());
        }
        return result;
    }

    ManagedStateBackend::ManagedStateBackend(ManagedStateServiceConfig config)
        : config_(std::move(config))
    {
    }

    StateStatus ManagedStateBackend::status(
        const ledger::SourceConfig &source,
        std::optional<uint64_t> remote_tip_height)
    {
        if (config_.base_url.empty())
        {
            StateStatus status;
            status.backend_mode = StateBackendMode::Managed;
            status.source_mode = source.source_mode;
            status.error = "Managed state service URL is not configured";
            status.raw = status_to_json(status);
            return status;
        }

        try
        {
            network::NetworkClient client(config_.base_url, config_.timeout_ms);
            if (!config_.api_key.empty())
            {
                client.set_header("Authorization", "Bearer " + config_.api_key);
            }
            auto response = client.post_json(
                "/v1/state/status",
                {
                    {"source", source_to_json(source)},
                    {"remote_tip_height", remote_tip_height ? json(*remote_tip_height) : json(nullptr)}});
            auto status = status_from_json(response, StateBackendMode::Managed, source);
            status.remote_tip_height = remote_tip_height.value_or(status.remote_tip_height);
            return status;
        }
        catch (const std::exception &e)
        {
            StateStatus status;
            status.backend_mode = StateBackendMode::Managed;
            status.source_mode = source.source_mode;
            status.error = "Managed state status request failed";
            status.detail = e.what();
            status.raw = status_to_json(status);
            return status;
        }
    }

    StateRefreshResult ManagedStateBackend::refresh(
        const ledger::SyncLedgerStateParams &params,
        const StateRefreshOptions &options)
    {
        StateRefreshResult result;
        result.before = status(params.source);
        if (config_.base_url.empty())
        {
            result.error = make_error(
                ErrorCode::UnsupportedOperation,
                "Managed state service URL is not configured");
            result.after = result.before;
            return result;
        }
        try
        {
            network::NetworkClient client(config_.base_url, config_.timeout_ms);
            if (!config_.api_key.empty())
            {
                client.set_header("Authorization", "Bearer " + config_.api_key);
            }
            auto response = client.post_json(
                "/v1/state/refresh",
                {
                    {"source", source_to_json(params.source)},
                    {"wallet_seeds", params.wallet_seeds},
                    {"timeout_ms", params.timeout_ms},
                    {"options", {
                                    {"max_lag_blocks", options.max_lag_blocks},
                                    {"force", options.force},
                                    {"allow_source_fetch", options.allow_source_fetch},
                                    {"timeout_ms", options.timeout_ms},
                                }}});
            result.success = response.value("success", false);
            result.skipped = response.value("skipped", false);
            if (response.contains("status") && response["status"].is_object())
            {
                result.after = status_from_json(response["status"], StateBackendMode::Managed, params.source);
            }
            else
            {
                result.after = status(params.source);
            }
            if (!result.success)
            {
                result.error = make_error(
                    ErrorCode::LedgerBuildError,
                    response.value("error", "Managed state refresh failed"),
                    response.value("detail", ""));
            }
        }
        catch (const std::exception &e)
        {
            result.after = result.before;
            result.error = make_error(
                ErrorCode::NodeRpcError,
                "Managed state refresh request failed",
                e.what());
        }
        return result;
    }

    SnapshotResult ManagedStateBackend::export_snapshot(
        const ledger::SourceConfig &,
        const std::filesystem::path &snapshot_dir)
    {
        SnapshotResult result;
        result.path = snapshot_dir;
        result.error = make_error(
            ErrorCode::UnsupportedOperation,
            "ManagedStateBackend does not export local snapshots");
        return result;
    }

    SnapshotResult ManagedStateBackend::import_snapshot(
        const std::filesystem::path &snapshot_dir,
        const ledger::SourceConfig &)
    {
        SnapshotResult result;
        result.path = snapshot_dir;
        result.error = make_error(
            ErrorCode::UnsupportedOperation,
            "ManagedStateBackend does not import local snapshots");
        return result;
    }

    std::shared_ptr<IStateBackend> make_local_state_backend(
        std::shared_ptr<ledger::ILedgerBackend> ledger_backend,
        StateBackendMode mode)
    {
        return std::make_shared<LocalStateBackend>(std::move(ledger_backend), mode);
    }

    std::shared_ptr<IStateBackend> make_managed_state_backend(
        ManagedStateServiceConfig config)
    {
        return std::make_shared<ManagedStateBackend>(std::move(config));
    }

} // namespace midnight::production
