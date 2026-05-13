#pragma once

#include "midnight/ledger/transaction_builder.hpp"
#include "midnight/production/errors.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace midnight::production
{
    struct ArtifactConfig
    {
        std::filesystem::path root_dir = "midnight-artifacts";
        std::string network = "preprod";
        std::string wallet_id = "default";
        bool enabled = true;
    };

    struct ArtifactSaveResult
    {
        bool success = false;
        std::filesystem::path directory;
        std::vector<std::filesystem::path> files;
        SdkError error;
    };

    class ArtifactManager
    {
    public:
        explicit ArtifactManager(ArtifactConfig config = {});

        ArtifactSaveResult save_build_result(
            const std::string &operation,
            const ledger::BuildResult &build) const;

        ArtifactSaveResult save_intent_result(
            const std::string &operation,
            const ledger::IntentResult &intent) const;

        ArtifactSaveResult write_text(
            const std::string &operation,
            const std::string &name,
            const std::string &content) const;

    private:
        ArtifactConfig config_;

        std::filesystem::path create_operation_dir(
            const std::string &operation,
            const std::string &id) const;
    };

} // namespace midnight::production
