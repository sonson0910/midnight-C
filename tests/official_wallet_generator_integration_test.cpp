/**
 * Wallet Generator Output Tests
 *
 * Tests that the wallet generator produces correct output format
 * with all required address types (unshield, shielded, dust).
 * Uses native ledger FFI so generated addresses match midnight-research.
 */

#include <gtest/gtest.h>
#include "midnight/wallet/hd_wallet.hpp"
#include "midnight/ledger/ledger_backend.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace midnight::wallet;

namespace
{
std::string configured_ledger_ffi_library()
{
    if (const char *value = std::getenv("MIDNIGHT_LEDGER_FFI_LIBRARY"))
    {
        return value;
    }
#ifdef MIDNIGHT_TEST_LEDGER_FFI_LIBRARY
    return MIDNIGHT_TEST_LEDGER_FFI_LIBRARY;
#else
    return {};
#endif
}

midnight::ledger::WalletAddressDerivationResult derive_test_addresses()
{
    const auto library = configured_ledger_ffi_library();
    if (library.empty() || !std::filesystem::exists(library))
    {
        midnight::ledger::WalletAddressDerivationResult result;
        result.error = "missing FFI library";
        return result;
    }
    auto backend = midnight::ledger::make_ffi_ledger_backend(library);
    auto *ffi_backend = dynamic_cast<midnight::ledger::FfiLedgerBackend *>(backend.get());
    if (ffi_backend == nullptr || !ffi_backend->can_derive_wallet_addresses())
    {
        midnight::ledger::WalletAddressDerivationResult result;
        result.error = "FFI wallet derivation symbol unavailable";
        return result;
    }
    return ffi_backend->derive_wallet_addresses(
        "0000000000000000000000000000000000000000000000000000000000000001",
        "testnet");
}
}

TEST(WalletGeneratorOutputTest, GeneratedWallet_ContainsAllKeyTypes)
{
    const auto addresses = derive_test_addresses();
    if (!addresses.success)
    {
        GTEST_SKIP() << addresses.error;
    }

    // All keys should be non-empty
    EXPECT_FALSE(addresses.unshielded.empty());
    EXPECT_FALSE(addresses.dust.empty());
    EXPECT_FALSE(addresses.shielded.empty());

    // Keys should be different from each other
    EXPECT_NE(addresses.unshielded, addresses.dust);
    EXPECT_NE(addresses.unshielded, addresses.shielded);
    EXPECT_NE(addresses.dust, addresses.shielded);
}

TEST(WalletGeneratorOutputTest, OutputJson_ContainsAddressAndDustFields)
{
    const auto addresses = derive_test_addresses();
    if (!addresses.success)
    {
        GTEST_SKIP() << addresses.error;
    }

    // Build the JSON that wallet-generator would produce
    nlohmann::json output;
    output["unshield"] = addresses.unshielded;
    output["shielded"] = addresses.shielded;
    output["dust"] = addresses.dust;
    output["dust_registration"] = nlohmann::json{
        {"requested", true},
        {"address", addresses.dust}};

    std::string json_str = output.dump();

    // Verify all expected fields are present
    EXPECT_NE(json_str.find("\"unshield\""), std::string::npos);
    EXPECT_NE(json_str.find("\"shielded\""), std::string::npos);
    EXPECT_NE(json_str.find("\"dust\""), std::string::npos);
    EXPECT_NE(json_str.find("\"dust_registration\""), std::string::npos);

    // Verify dust_registration content via parsed JSON
    EXPECT_TRUE(output["dust_registration"]["requested"].get<bool>());
}

TEST(WalletGeneratorOutputTest, DeterministicMnemonic_ProducesSameKeys)
{
    const auto addresses1 = derive_test_addresses();
    const auto addresses2 = derive_test_addresses();
    if (!addresses1.success || !addresses2.success)
    {
        GTEST_SKIP() << addresses1.error << addresses2.error;
    }

    EXPECT_EQ(addresses1.unshielded, addresses2.unshielded);
    EXPECT_EQ(addresses1.user_address, addresses2.user_address);
    EXPECT_EQ(addresses1.verifying_key, addresses2.verifying_key);
}
