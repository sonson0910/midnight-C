#include "midnight/production/errors.hpp"
#include "midnight/production/validation.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    std::filesystem::path source_dir()
    {
#ifdef MIDNIGHT_TEST_SOURCE_DIR
        return std::filesystem::path(MIDNIGHT_TEST_SOURCE_DIR);
#else
        return std::filesystem::current_path();
#endif
    }

    nlohmann::json read_json(const std::filesystem::path &relative_path)
    {
        const auto path = source_dir() / relative_path;
        std::ifstream input(path);
        if (!input)
        {
            throw std::runtime_error("Failed to open fixture: " + path.string());
        }
        nlohmann::json value;
        input >> value;
        return value;
    }

    std::string as_string(const nlohmann::json &value)
    {
        return value.get<std::string>();
    }
}

TEST(SdkSpecFixturesTest, U128VectorsMatchProductionValidator)
{
    const auto vectors = read_json("golden-fixtures/vectors/u128.json");

    for (const auto &value : vectors.at("valid_decimal"))
    {
        EXPECT_TRUE(midnight::production::validation::is_u128_decimal(as_string(value)))
            << value;
    }

    for (const auto &value : vectors.at("invalid_decimal"))
    {
        EXPECT_FALSE(midnight::production::validation::is_u128_decimal(as_string(value)))
            << value;
    }
}

TEST(SdkSpecFixturesTest, AddressHashContractAndTokenVectorsMatchValidators)
{
    const auto formats = read_json("golden-fixtures/vectors/formats.json");
    const auto addresses = formats.at("addresses");
    const auto hashes = formats.at("hashes");
    const auto tokens = formats.at("tokens");
    const auto contract = formats.at("contract");
    const auto invalid = formats.at("invalid");

    EXPECT_TRUE(midnight::production::validation::is_unshielded_night_address(
        as_string(addresses.at("preview_unshielded_night"))));
    EXPECT_TRUE(midnight::production::validation::is_unshielded_night_address(
        as_string(addresses.at("testnet_unshielded_night"))));
    EXPECT_TRUE(midnight::production::validation::is_dust_address(
        as_string(addresses.at("testnet_dust"))));
    EXPECT_TRUE(midnight::production::validation::is_shielded_address(
        as_string(addresses.at("testnet_shielded"))));

    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(hashes.at("funding_tx_hash"))));
    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(hashes.at("funding_tx_hash_without_0x"))));
    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(hashes.at("transfer_tx_hash"))));
    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(hashes.at("transfer_block_hash"))));

    EXPECT_TRUE(midnight::production::validation::is_contract_address_hex(
        as_string(contract.at("simple_contract_address"))));
    EXPECT_TRUE(midnight::production::validation::is_contract_address_hex(
        as_string(contract.at("simple_contract_address_prefixed"))));
    EXPECT_FALSE(midnight::production::validation::is_contract_address_hex(
        as_string(invalid.at("wallet_address_as_contract"))));
    EXPECT_FALSE(midnight::production::validation::is_contract_address_hex(
        as_string(invalid.at("short_contract"))));

    EXPECT_TRUE(midnight::production::validation::is_token_type_hex(
        as_string(tokens.at("night"))));
    EXPECT_TRUE(midnight::production::validation::is_night_token_type(
        as_string(tokens.at("night"))));
    EXPECT_FALSE(midnight::production::validation::is_token_type_hex(
        as_string(invalid.at("display_token_name"))));
    EXPECT_FALSE(midnight::production::validation::is_tx_hash(
        as_string(invalid.at("short_hash"))));
}

TEST(SdkSpecFixturesTest, ErrorCodeStringsMatchGoldenFixture)
{
    const auto errors = read_json("golden-fixtures/vectors/errors.json");
    const std::vector<std::string> expected = {
        midnight::production::to_string(midnight::production::ErrorCode::None),
        midnight::production::to_string(midnight::production::ErrorCode::InvalidAddress),
        midnight::production::to_string(midnight::production::ErrorCode::InvalidContractAddress),
        midnight::production::to_string(midnight::production::ErrorCode::InvalidTokenType),
        midnight::production::to_string(midnight::production::ErrorCode::InvalidAmount),
        midnight::production::to_string(midnight::production::ErrorCode::InvalidHash),
        midnight::production::to_string(midnight::production::ErrorCode::IndexerSchemaError),
        midnight::production::to_string(midnight::production::ErrorCode::LedgerBackendError),
        midnight::production::to_string(midnight::production::ErrorCode::LedgerBuildError),
        midnight::production::to_string(midnight::production::ErrorCode::LedgerStateMismatch),
        midnight::production::to_string(midnight::production::ErrorCode::ProofServerError),
        midnight::production::to_string(midnight::production::ErrorCode::NodeRpcError),
        midnight::production::to_string(midnight::production::ErrorCode::NodeRejectedTx),
        midnight::production::to_string(midnight::production::ErrorCode::IndexerError),
        midnight::production::to_string(midnight::production::ErrorCode::SubmissionRejected),
        midnight::production::to_string(midnight::production::ErrorCode::ConfirmationTimeout),
        midnight::production::to_string(midnight::production::ErrorCode::FinalityTimeout),
        midnight::production::to_string(midnight::production::ErrorCode::ArtifactError),
        midnight::production::to_string(midnight::production::ErrorCode::UnsupportedOperation),
    };

    EXPECT_EQ(errors.at("sdk_error_codes").get<std::vector<std::string>>(), expected);
    EXPECT_EQ(errors.at("node_custom_errors").at("170").at("name"), "InvalidDustSpendProof");
    EXPECT_EQ(errors.at("node_custom_errors").at("171").at("name"), "OutOfDustValidityWindow");
    EXPECT_EQ(errors.at("guard_errors").at("ledger_state_mismatch").at("sdk_error_code"),
              "ledger_state_mismatch");
}

TEST(SdkSpecFixturesTest, ArtifactLayoutFixtureStaysCompatible)
{
    const auto layout = read_json("golden-fixtures/vectors/artifact-layout.json");

    EXPECT_EQ(layout.at("path_template"),
              "midnight-artifacts/<network>/<wallet_id>/<operation>/<id-or-timestamp>/");
    EXPECT_EQ(layout.at("required_path_segments").size(), 4u);
    EXPECT_GE(layout.at("common_files").size(), 6u);
    EXPECT_NE(as_string(layout.at("example").at("path")).find("/preview/preview-local/"),
              std::string::npos);
    EXPECT_NE(as_string(layout.at("example").at("path")).find("/transfer-night/"),
              std::string::npos);
}

TEST(SdkSpecFixturesTest, LivePreviewSmokeFixtureIsPublicAndWellFormed)
{
    const auto live = read_json("golden-fixtures/live/preview-smoke.json");
    const auto versions = read_json("midnight-versions.json");

    EXPECT_EQ(live.at("network"), "preview");
    EXPECT_EQ(live.at("expected_ledger_version"), versions.at("ledger"));
    EXPECT_EQ(live.at("proof_server").at("version"), versions.at("ledger"));
    EXPECT_EQ(live.at("night_transfer").at("status"), "included");
    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(live.at("night_transfer").at("tx_hash"))));
    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(live.at("night_transfer").at("block_hash"))));
    EXPECT_TRUE(midnight::production::validation::is_contract_address_hex(
        as_string(live.at("smart_contract_smoke").at("contract_address"))));
    EXPECT_EQ(live.at("smart_contract_smoke").at("status"), "deploy_call_included");
    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(live.at("smart_contract_smoke").at("deploy").at("tx_hash"))));
    EXPECT_TRUE(midnight::production::validation::is_tx_hash(
        as_string(live.at("smart_contract_smoke").at("call").at("tx_hash"))));
}

