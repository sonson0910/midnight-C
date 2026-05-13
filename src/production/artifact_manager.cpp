#include "midnight/production/artifact_manager.hpp"
#include "midnight/core/common_utils.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

namespace midnight::production
{
    namespace
    {
        std::string timestamp_id()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now.time_since_epoch())
                                    .count() %
                                1000;
            std::tm tm{};
#ifdef _WIN32
            gmtime_s(&tm, &time);
#else
            gmtime_r(&time, &tm);
#endif
            std::ostringstream out;
            out << std::put_time(&tm, "%Y%m%dT%H%M%S") << std::setw(3)
                << std::setfill('0') << millis << "Z";
            return out.str();
        }

        std::string sanitize_path_component(std::string value)
        {
            for (char &ch : value)
            {
                const bool ok =
                    (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    ch == '-' || ch == '_' || ch == '.';
                if (!ok)
                {
                    ch = '_';
                }
            }
            return value.empty() ? "unknown" : value;
        }

        void write_binary_file(const std::filesystem::path &path,
                               const std::vector<uint8_t> &bytes)
        {
            std::ofstream out(path, std::ios::binary);
            if (!out)
            {
                throw std::runtime_error("Could not open artifact file: " + path.string());
            }
            out.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }

        void write_text_file(const std::filesystem::path &path,
                             const std::string &content)
        {
            std::ofstream out(path, std::ios::binary);
            if (!out)
            {
                throw std::runtime_error("Could not open artifact file: " + path.string());
            }
            out << content;
        }
    }

    ArtifactManager::ArtifactManager(ArtifactConfig config)
        : config_(std::move(config))
    {
    }

    std::filesystem::path ArtifactManager::create_operation_dir(
        const std::string &operation,
        const std::string &id) const
    {
        const auto dir =
            config_.root_dir /
            sanitize_path_component(config_.network) /
            sanitize_path_component(config_.wallet_id) /
            sanitize_path_component(operation) /
            sanitize_path_component(id.empty() ? timestamp_id() : id);
        std::filesystem::create_directories(dir);
        return dir;
    }

    ArtifactSaveResult ArtifactManager::save_build_result(
        const std::string &operation,
        const ledger::BuildResult &build) const
    {
        ArtifactSaveResult result;
        if (!config_.enabled)
        {
            result.success = true;
            return result;
        }

        try
        {
            const std::string id = !build.transaction_hash.empty()
                                       ? midnight::util::strip_hex_prefix(build.transaction_hash)
                                       : timestamp_id();
            result.directory = create_operation_dir(operation, id);

            nlohmann::json metadata = {
                {"operation", operation},
                {"success", build.success},
                {"transaction_hash", build.transaction_hash},
                {"output_file", build.output_file},
                {"command", build.command},
                {"error", build.error}};
            write_text_file(result.directory / "metadata.json", metadata.dump(2));
            result.files.push_back(result.directory / "metadata.json");

            if (!build.transaction_bytes.empty())
            {
                write_binary_file(result.directory / "transaction.mn", build.transaction_bytes);
                result.files.push_back(result.directory / "transaction.mn");
            }
            if (!build.transaction_hex.empty())
            {
                write_text_file(result.directory / "transaction.hex", build.transaction_hex);
                result.files.push_back(result.directory / "transaction.hex");
            }
            if (!build.log.empty())
            {
                write_text_file(result.directory / "toolkit.log", build.log);
                result.files.push_back(result.directory / "toolkit.log");
            }
            if (!build.output_file.empty() && std::filesystem::exists(build.output_file))
            {
                const auto copied = result.directory / std::filesystem::path(build.output_file).filename();
                std::filesystem::copy_file(
                    build.output_file,
                    copied,
                    std::filesystem::copy_options::overwrite_existing);
                result.files.push_back(copied);
            }

            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = {
                .code = ErrorCode::ArtifactError,
                .message = "Could not save transaction artifacts",
                .detail = e.what()};
        }
        return result;
    }

    ArtifactSaveResult ArtifactManager::save_intent_result(
        const std::string &operation,
        const ledger::IntentResult &intent) const
    {
        ArtifactSaveResult result;
        if (!config_.enabled)
        {
            result.success = true;
            return result;
        }

        try
        {
            result.directory = create_operation_dir(operation, timestamp_id());
            nlohmann::json metadata = {
                {"operation", operation},
                {"success", intent.success},
                {"output_intent", intent.output_intent},
                {"output_private_state", intent.output_private_state},
                {"output_zswap_state", intent.output_zswap_state},
                {"command", intent.command},
                {"error", intent.error}};
            metadata["output_onchain_state"] =
                intent.output_onchain_state ? nlohmann::json(*intent.output_onchain_state) : nlohmann::json(nullptr);
            metadata["output_result"] =
                intent.output_result ? nlohmann::json(*intent.output_result) : nlohmann::json(nullptr);
            write_text_file(result.directory / "metadata.json", metadata.dump(2));
            result.files.push_back(result.directory / "metadata.json");
            if (!intent.log.empty())
            {
                write_text_file(result.directory / "toolkit.log", intent.log);
                result.files.push_back(result.directory / "toolkit.log");
            }
            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = {
                .code = ErrorCode::ArtifactError,
                .message = "Could not save intent artifacts",
                .detail = e.what()};
        }
        return result;
    }

    ArtifactSaveResult ArtifactManager::write_text(
        const std::string &operation,
        const std::string &name,
        const std::string &content) const
    {
        ArtifactSaveResult result;
        if (!config_.enabled)
        {
            result.success = true;
            return result;
        }

        try
        {
            result.directory = create_operation_dir(operation, timestamp_id());
            const auto file = result.directory / sanitize_path_component(name);
            write_text_file(file, content);
            result.files.push_back(file);
            result.success = true;
        }
        catch (const std::exception &e)
        {
            result.error = {
                .code = ErrorCode::ArtifactError,
                .message = "Could not write artifact",
                .detail = e.what()};
        }
        return result;
    }

} // namespace midnight::production
