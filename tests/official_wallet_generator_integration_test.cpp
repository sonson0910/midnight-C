#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::filesystem::path get_repo_root()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path();
    }

    std::filesystem::path find_wallet_generator_executable(const std::filesystem::path &repo_root)
    {
#ifdef _WIN32
        const std::string exe_name = "wallet-generator.exe";
#else
        const std::string exe_name = "wallet-generator";
#endif

        const std::filesystem::path cwd = std::filesystem::current_path();

        std::vector<std::filesystem::path> candidates = {
            repo_root / "build_verify" / "bin" / "Release" / exe_name,
            repo_root / "build_verify" / "bin" / "Debug" / exe_name,
            repo_root / "build" / "bin" / "Release" / exe_name,
            repo_root / "build" / "bin" / "Debug" / exe_name,
            cwd / exe_name,
            cwd / ".." / "bin" / "Release" / exe_name,
            cwd / ".." / "bin" / "Debug" / exe_name,
        };

        for (const auto &candidate : candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec)
            {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
        }

        return {};
    }

    std::string quote(const std::filesystem::path &path)
    {
        return std::string("\"") + path.string() + "\"";
    }
} // namespace

TEST(OfficialWalletGeneratorIntegrationTest, OfficialModeJsonContainsAddressAndDustRegistrationFields)
{
    const std::filesystem::path repo_root = get_repo_root();
    const std::filesystem::path bridge_script = repo_root / "tools" / "official-wallet-address.mjs";
    const std::filesystem::path node_modules = repo_root / "midnight-research" / "node_modules";

    if (!std::filesystem::exists(bridge_script))
    {
        GTEST_SKIP() << "Bridge script not found: " << bridge_script.string();
    }

    if (!std::filesystem::exists(node_modules))
    {
        GTEST_SKIP() << "Official SDK dependencies missing at: " << node_modules.string();
    }

    const std::filesystem::path wallet_generator = find_wallet_generator_executable(repo_root);
    if (wallet_generator.empty())
    {
        GTEST_SKIP() << "wallet-generator executable not found. Build examples target first.";
    }

    const std::filesystem::path out_file = std::filesystem::temp_directory_path() / "midnight_official_wallet_integration.json";
    std::error_code remove_ec;
    std::filesystem::remove(out_file, remove_ec);

    std::ostringstream cmd;
#ifdef _WIN32
    cmd << "cmd /C \"cd /d " << quote(repo_root)
        << " && " << quote(wallet_generator)
        << " --official-sdk"
        << " --network mainnet"
        << " --seed-hex 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        << " --register-dust"
        << " --sync-timeout 1"
        << " --output-file " << quote(out_file)
        << "\"";
#else
    cmd << "cd " << quote(repo_root)
        << " && " << quote(wallet_generator)
        << " --official-sdk"
        << " --network mainnet"
        << " --seed-hex 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        << " --register-dust"
        << " --sync-timeout 1"
        << " --output-file " << quote(out_file);
#endif

    const int rc = std::system(cmd.str().c_str());
    ASSERT_EQ(rc, 0) << "wallet-generator command failed: " << cmd.str();

    std::ifstream in(out_file);
    ASSERT_TRUE(in.good()) << "Expected output file was not created: " << out_file.string();

    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(json.find("\"unshield\""), std::string::npos);
    EXPECT_NE(json.find("\"shielded\""), std::string::npos);
    EXPECT_NE(json.find("\"dust\""), std::string::npos);
    EXPECT_NE(json.find("\"dust_registration\""), std::string::npos);
    EXPECT_NE(json.find("\"requested\": true"), std::string::npos);

    std::error_code cleanup_ec;
    std::filesystem::remove(out_file, cleanup_ec);
}
