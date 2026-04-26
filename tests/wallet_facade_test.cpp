#include <gtest/gtest.h>
#include "midnight/wallet/wallet_facade.hpp"
#include "midnight/wallet/wallet_services.hpp"

using namespace midnight::wallet;
using namespace midnight::network;

static const std::string TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon about";

static const std::string TEST_INDEXER =
    "https://indexer.testnet.midnight.network/api/v4/graphql";

// ═══════════════════════════════════════════════════════════════
// NetworkId Tests (Fix #2)
// ═══════════════════════════════════════════════════════════════

TEST(NetworkIdTest, AllSevenValues)
{
    EXPECT_EQ(address::network_to_string(address::Network::Mainnet), "mainnet");
    EXPECT_EQ(address::network_to_string(address::Network::Preview), "preview");
    EXPECT_EQ(address::network_to_string(address::Network::PreProd), "preprod");
    EXPECT_EQ(address::network_to_string(address::Network::Testnet), "testnet");
    EXPECT_EQ(address::network_to_string(address::Network::DevNet), "devnet");
    EXPECT_EQ(address::network_to_string(address::Network::QaNet), "qanet");
    EXPECT_EQ(address::network_to_string(address::Network::Undeployed), "undeployed");
}

TEST(NetworkIdTest, FromString)
{
    EXPECT_EQ(address::network_from_string("mainnet"), address::Network::Mainnet);
    EXPECT_EQ(address::network_from_string("preview"), address::Network::Preview);
    EXPECT_EQ(address::network_from_string("preprod"), address::Network::PreProd);
    EXPECT_EQ(address::network_from_string("devnet"), address::Network::DevNet);
    EXPECT_EQ(address::network_from_string("qanet"), address::Network::QaNet);
    EXPECT_EQ(address::network_from_string("undeployed"), address::Network::Undeployed);
    EXPECT_EQ(address::network_from_string("PREVIEW"), address::Network::Preview); // case-insensitive
}

TEST(NetworkIdTest, HrpGeneration)
{
    EXPECT_EQ(address::get_hrp("addr", address::Network::Mainnet), "mn_addr");
    EXPECT_EQ(address::get_hrp("addr", address::Network::Preview), "mn_addr_preview");
    EXPECT_EQ(address::get_hrp("addr", address::Network::PreProd), "mn_addr_preprod");
    EXPECT_EQ(address::get_hrp("dust", address::Network::DevNet), "mn_dust_devnet");
    EXPECT_EQ(address::get_hrp("shield-addr", address::Network::QaNet), "mn_shield-addr_qanet");
}

// ═══════════════════════════════════════════════════════════════
// DustAddress Encoding Tests (Fix #1)
// ═══════════════════════════════════════════════════════════════

TEST(DustAddressTest, EncodeDust_ValidScalar)
{
    std::vector<uint8_t> scalar(32, 0x42);
    auto addr = address::encode_dust(scalar, address::Network::Preview);
    EXPECT_EQ(addr.substr(0, 16), "mn_dust_preview1");
}

TEST(DustAddressTest, EncodeDustFromPubkey_32Bytes)
{
    std::vector<uint8_t> pk(32, 0xAB);
    auto addr = address::encode_dust_from_pubkey(pk, address::Network::Preview);
    EXPECT_EQ(addr.substr(0, 16), "mn_dust_preview1");
}

TEST(DustAddressTest, DecodeDust_Roundtrip)
{
    std::vector<uint8_t> scalar(32, 0x13);
    auto addr = address::encode_dust(scalar, address::Network::Preview);
    auto decoded = address::decode_dust(addr);
    EXPECT_EQ(decoded, scalar);
}

TEST(DustAddressTest, EncodeDust_Empty_Throws)
{
    std::vector<uint8_t> empty;
    EXPECT_THROW(address::encode_dust(empty), std::runtime_error);
}

TEST(DustAddressTest, DecodeDust_WrongPrefix_Throws)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_THROW(address::decode_dust(facade.unshielded_address()), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// ShieldedAddress Decode Tests (Fix #3)
// ═══════════════════════════════════════════════════════════════

TEST(ShieldedAddressTest, EncodeDecodeRoundtrip)
{
    std::vector<uint8_t> coin_pk(32, 0xAA);
    std::vector<uint8_t> enc_pk(32, 0xBB);

    auto addr = address::encode_shielded(coin_pk, enc_pk, address::Network::Preview);
    EXPECT_EQ(addr.substr(0, 21), "mn_shield-addr_previe");

    auto decoded = address::decode_shielded(addr);
    EXPECT_EQ(decoded.coin_public_key, coin_pk);
    EXPECT_EQ(decoded.encryption_public_key, enc_pk);
}

TEST(ShieldedAddressTest, DecodeWrongPrefix_Throws)
{
    EXPECT_THROW(address::decode_shielded("mn_addr_preview1abc"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// WalletFacade Core Tests
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, FromMnemonic_CreatesValidFacade)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto addr = facade.unshielded_address();
    EXPECT_EQ(addr.substr(0, 16), "mn_addr_preview1");
    EXPECT_GT(addr.size(), 40u);
}

TEST(WalletFacadeTest, FromMnemonic_HasDustAddress)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto dust = facade.dust_address();
    EXPECT_EQ(dust.substr(0, 16), "mn_dust_preview1");
}

TEST(WalletFacadeTest, FromMnemonic_Deterministic)
{
    auto f1 = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto f2 = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_EQ(f1.unshielded_address(), f2.unshielded_address());
    EXPECT_EQ(f1.dust_address(), f2.dust_address());
}

TEST(WalletFacadeTest, State_InitiallyEmpty)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_EQ(facade.state().night_balance(), 0u);
}

TEST(WalletFacadeTest, Sign_ProducesValidSignature)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    std::vector<uint8_t> msg = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    auto sig = facade.sign(msg);
    EXPECT_EQ(sig.size(), 64u);
    EXPECT_TRUE(facade.night_key().verify(msg, sig));
}

TEST(WalletFacadeTest, AllKeys_AreDifferent)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_NE(facade.night_key().public_key, facade.dust_key().public_key);
    EXPECT_NE(facade.night_key().public_key, facade.zswap_key().public_key);
}

// ═══════════════════════════════════════════════════════════════
// Coin Selection Tests
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, SelectCoins_EmptyWallet_ReturnsEmpty)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_TRUE(facade.select_coins(1000).empty());
}

TEST(WalletFacadeTest, CoinSelectionStrategy_Change)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    facade.set_coin_selection_strategy(CoinSelectionStrategy::SmallestFirst);
    EXPECT_TRUE(facade.select_coins(1000).empty()); // Still empty, but no crash
}

// ═══════════════════════════════════════════════════════════════
// Transfer TX Builder Tests
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, BuildTransfer_EmptyOutputs_Fails)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto result = facade.build_transfer({});
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "No outputs specified");
}

TEST(WalletFacadeTest, BuildTransfer_InsufficientFunds_Fails)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    std::vector<TokenTransfer> outputs = {{1000000, "NIGHT", "mn_addr_preview1recv"}};
    auto result = facade.build_transfer(outputs);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.find("Insufficient") != std::string::npos);
}

TEST(WalletFacadeTest, SignTransaction_UnbuiltTx_Fails)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    TransferResult tx;
    tx.success = false;
    auto signed_tx = facade.sign_transaction(tx);
    EXPECT_EQ(signed_tx.error, "Cannot sign: transaction not built");
}

// ═══════════════════════════════════════════════════════════════
// Full Transfer Pipeline Tests (Fix #7)
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, TransferTransaction_NoFunds_Fails)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    std::vector<TokenTransfer> outputs = {{100, "NIGHT", "mn_addr_preview1recv"}};
    auto result = facade.transfer_transaction(outputs);
    EXPECT_FALSE(result.success);
}

TEST(WalletFacadeTest, TransferTransaction_WithConfig_NoFunds)
{
    WalletFacadeConfig config;
    config.indexer_http_url = TEST_INDEXER;
    config.relay_url = "ws://localhost:9944";
    config.proving_server_url = "http://localhost:6300";
    config.network = address::Network::Preview;

    auto facade = WalletFacade::from_mnemonic_with_config(TEST_MNEMONIC, config);
    EXPECT_TRUE(facade.submission_service() != nullptr);
    EXPECT_TRUE(facade.proving_service() != nullptr);
    EXPECT_TRUE(facade.pending_transactions_service() != nullptr);

    std::vector<TokenTransfer> outputs = {{100, "NIGHT", "mn_addr_preview1recv"}};
    auto result = facade.transfer_transaction(outputs);
    EXPECT_FALSE(result.success);
}

// ═══════════════════════════════════════════════════════════════
// Lifecycle Tests (Fix #10)
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, Lifecycle_StartStop)
{
    WalletFacadeConfig config;
    config.indexer_http_url = TEST_INDEXER;
    config.relay_url = "ws://localhost:9944";
    config.proving_server_url = "http://localhost:6300";

    auto facade = WalletFacade::from_mnemonic_with_config(TEST_MNEMONIC, config);
    EXPECT_FALSE(facade.is_running());

    facade.start();
    EXPECT_TRUE(facade.is_running());

    facade.stop();
    EXPECT_FALSE(facade.is_running());
}

// ═══════════════════════════════════════════════════════════════
// Fee Estimation Tests (Fix #15)
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, EstimateTransactionFee)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    std::vector<TokenTransfer> outputs = {{1000, "NIGHT", "addr"}};
    auto fee = facade.estimate_transaction_fee(outputs);
    EXPECT_GT(fee.tx_fee, 0u);
    EXPECT_GT(fee.total_fee, fee.tx_fee);
}

// ═══════════════════════════════════════════════════════════════
// Revert Transaction Tests (Fix #12)
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, RevertTransaction_EmptyTx)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    TransferResult tx;
    tx.tx_hash = "abc123";
    EXPECT_TRUE(facade.revert_transaction(tx));
}

// ═══════════════════════════════════════════════════════════════
// SyncProgress Tests
// ═══════════════════════════════════════════════════════════════

TEST(SyncProgressTest, DefaultValues)
{
    SyncProgress sp;
    EXPECT_FALSE(sp.is_connected);
    EXPECT_FALSE(sp.is_strictly_complete());
}

TEST(SyncProgressTest, StrictlyComplete_WhenEqual)
{
    SyncProgress sp{100, 100, true};
    EXPECT_TRUE(sp.is_strictly_complete());
}

TEST(SyncProgressTest, StrictlyComplete_NotConnected)
{
    SyncProgress sp{100, 100, false};
    EXPECT_FALSE(sp.is_strictly_complete());
}

TEST(SyncProgressTest, CompleteWithin_Gap)
{
    SyncProgress sp{95, 100, true};
    EXPECT_FALSE(sp.is_complete_within(3));
    EXPECT_TRUE(sp.is_complete_within(5));
}

// ═══════════════════════════════════════════════════════════════
// Serialization Tests
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, Serialize_ProducesValidJSON)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto serialized = facade.serialize();
    auto j = nlohmann::json::parse(serialized);
    EXPECT_EQ(j["version"], 2);
    EXPECT_EQ(j["network"], "preview");
    EXPECT_TRUE(j.contains("unshielded_address"));
    EXPECT_TRUE(j.contains("dust_address"));
}

TEST(WalletFacadeTest, Restore_ParsesState)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto serialized = facade.serialize();
    auto restored = WalletFacade::restore(serialized);
    EXPECT_EQ(restored.unshielded_address(), facade.unshielded_address());
    EXPECT_EQ(restored.dust_address(), facade.dust_address());
}

// ═══════════════════════════════════════════════════════════════
// Memory Wipe Tests
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, Clear_WipesKeys)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_FALSE(facade.is_cleared());
    facade.clear();
    EXPECT_TRUE(facade.is_cleared());
    EXPECT_TRUE(facade.night_key().secret_key.empty());
}

TEST(WalletFacadeTest, Clear_SignThrows)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    facade.clear();
    EXPECT_THROW(facade.sign({0x01}), std::runtime_error);
}

TEST(WalletFacadeTest, Clear_BuildTransferThrows)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    facade.clear();
    EXPECT_THROW(facade.build_transfer({{100, "NIGHT", "a"}}), std::runtime_error);
}

TEST(WalletFacadeTest, Clear_AddressesStillAccessible)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto addr = facade.unshielded_address();
    facade.clear();
    EXPECT_EQ(facade.unshielded_address(), addr);
}

// ═══════════════════════════════════════════════════════════════
// Dust Registration Tests
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, RegisterForDust_InsufficientFunds)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    auto result = facade.register_for_dust(1000000);
    EXPECT_FALSE(result.success);
}

TEST(WalletFacadeTest, DeregisterFromDust_NotRegistered)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_FALSE(facade.deregister_from_dust());
}

TEST(WalletFacadeTest, EstimateDustGeneration)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    EXPECT_EQ(facade.estimate_dust_generation(10000), 10u);
}

TEST(WalletFacadeTest, EstimateRegistration)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    std::vector<UtxoWithMeta> utxos = {{.utxo_hash = "a", .amount = 5000}};
    auto est = facade.estimate_registration(utxos);
    EXPECT_GT(est.total_fee, 0u);
    EXPECT_EQ(est.dust_generation_rate, 5u);
}

// ═══════════════════════════════════════════════════════════════
// SubmissionService Tests (Fix #4)
// ═══════════════════════════════════════════════════════════════

TEST(SubmissionServiceTest, CreateAndClose)
{
    SubmissionServiceConfig config;
    config.relay_url = "ws://localhost:9944";
    SubmissionService svc(config);
    EXPECT_FALSE(svc.is_connected());
    svc.close();
}

TEST(SubmissionServiceTest, SubmitEmptyTx_ReturnsError)
{
    SubmissionServiceConfig config;
    config.relay_url = "ws://localhost:9944";
    SubmissionService svc(config);

    SerializedTransaction tx;
    auto event = svc.submit_transaction(tx);
    EXPECT_TRUE(event.is_error());
}

TEST(SubmissionServiceTest, SubmitValidTx_ReturnsSubmitted)
{
    SubmissionServiceConfig config;
    config.relay_url = "ws://localhost:9944";
    SubmissionService svc(config);

    auto tx = SerializedTransaction::from_json(nlohmann::json{{"test", true}});
    auto event = svc.submit_transaction(tx, SubmissionEventTag::Submitted);
    EXPECT_EQ(event.tag, SubmissionEventTag::Submitted);
    EXPECT_FALSE(event.tx_hash.empty());
}

// ═══════════════════════════════════════════════════════════════
// ProvingService Tests (Fix #5)
// ═══════════════════════════════════════════════════════════════

TEST(ProvingServiceTest, Create)
{
    ProvingServiceConfig config;
    config.proving_server_url = "http://localhost:6300";
    ProvingService svc(config);
    EXPECT_TRUE(svc.is_available());
}

TEST(ProvingServiceTest, ProveEmptyTx_ReturnsError)
{
    ProvingServiceConfig config;
    config.proving_server_url = "http://localhost:6300";
    ProvingService svc(config);

    auto result = svc.prove({});
    EXPECT_FALSE(result.success);
}

TEST(ProvingServiceTest, ProveValidTx_Succeeds)
{
    ProvingServiceConfig config;
    config.proving_server_url = "http://localhost:6300";
    ProvingService svc(config);

    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto result = svc.prove(data);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.proof_hash.empty());
}

TEST(ProvingServiceTest, ProveJson)
{
    ProvingServiceConfig config;
    config.proving_server_url = "http://localhost:6300";
    ProvingService svc(config);

    auto result = svc.prove_json(nlohmann::json{{"tx", "test"}});
    EXPECT_TRUE(result.success);
}

// ═══════════════════════════════════════════════════════════════
// PendingTransactionsService Tests (Fix #6)
// ═══════════════════════════════════════════════════════════════

TEST(PendingTxServiceTest, CreateAndLifecycle)
{
    PendingTransactionsConfig config;
    config.indexer_http_url = TEST_INDEXER;
    PendingTransactionsService svc(config);
    EXPECT_FALSE(svc.is_running());
    svc.start();
    EXPECT_TRUE(svc.is_running());
    svc.stop();
    EXPECT_FALSE(svc.is_running());
}

TEST(PendingTxServiceTest, AddAndTrack)
{
    PendingTransactionsConfig config;
    config.indexer_http_url = TEST_INDEXER;
    PendingTransactionsService svc(config);

    svc.add_pending_transaction("abc123");
    auto pending = svc.get_pending();
    EXPECT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].tx_hash, "abc123");
    EXPECT_EQ(pending[0].status, SubmissionEventTag::Submitted);
}

TEST(PendingTxServiceTest, GetState)
{
    PendingTransactionsConfig config;
    config.indexer_http_url = TEST_INDEXER;
    PendingTransactionsService svc(config);

    svc.add_pending_transaction("tx1");
    auto state = svc.get_state("tx1");
    EXPECT_TRUE(state.has_value());
    EXPECT_EQ(state->tx_hash, "tx1");

    auto missing = svc.get_state("nonexistent");
    EXPECT_FALSE(missing.has_value());
}

TEST(PendingTxServiceTest, ClearTransaction)
{
    PendingTransactionsConfig config;
    config.indexer_http_url = TEST_INDEXER;
    PendingTransactionsService svc(config);

    svc.add_pending_transaction("tx1");
    svc.add_pending_transaction("tx2");
    EXPECT_EQ(svc.get_pending().size(), 2u);

    svc.clear("tx1");
    EXPECT_EQ(svc.get_pending().size(), 1u);
    EXPECT_EQ(svc.get_pending()[0].tx_hash, "tx2");
}

TEST(PendingTxServiceTest, OnStatusChange_Callback)
{
    PendingTransactionsConfig config;
    config.indexer_http_url = TEST_INDEXER;
    PendingTransactionsService svc(config);

    bool called = false;
    svc.on_status_change([&](const PendingTx&) { called = true; });
    // Callback not called yet since we haven't polled
    EXPECT_FALSE(called);
}

// ═══════════════════════════════════════════════════════════════
// IndexerSubscription Tests
// ═══════════════════════════════════════════════════════════════

#include "midnight/network/indexer_subscription.hpp"

TEST(IndexerSubscriptionTest, Create_DoesNotThrow)
{
    EXPECT_NO_THROW({
        IndexerSubscription sub(TEST_INDEXER, "mn_addr_preview1test");
    });
}

TEST(IndexerSubscriptionTest, IsRunning_InitiallyFalse)
{
    IndexerSubscription sub(TEST_INDEXER, "mn_addr_preview1test");
    EXPECT_FALSE(sub.is_running());
}

TEST(IndexerSubscriptionTest, StartStop_Works)
{
    IndexerSubscription sub(TEST_INDEXER, "mn_addr_preview1test");
    sub.start(60000);
    EXPECT_TRUE(sub.is_running());
    sub.stop();
    EXPECT_FALSE(sub.is_running());
}

TEST(IndexerSubscriptionTest, RegisterCallback_DoesNotThrow)
{
    IndexerSubscription sub(TEST_INDEXER, "mn_addr_preview1test");
    sub.on_balance_change([](const std::string&, uint64_t) {});
    sub.on_unshielded_tx([](const std::vector<UtxoWithMeta>&,
                            const std::vector<UtxoWithMeta>&) {});
    EXPECT_TRUE(true);
}

TEST(IndexerSubscriptionTest, MoveConstruct_Works)
{
    IndexerSubscription sub1(TEST_INDEXER, "mn_addr_preview1test");
    IndexerSubscription sub2(std::move(sub1));
    EXPECT_FALSE(sub2.is_running());
}

// ═══════════════════════════════════════════════════════════════
// Data Type Tests
// ═══════════════════════════════════════════════════════════════

TEST(UtxoWithMetaTest, DefaultConstruction)
{
    UtxoWithMeta meta;
    EXPECT_EQ(meta.amount, 0u);
    EXPECT_EQ(meta.token_type, "NIGHT");
    EXPECT_FALSE(meta.registered_for_dust);
}

TEST(TransferResultTest, DefaultState)
{
    TransferResult tr;
    EXPECT_FALSE(tr.success);
    EXPECT_TRUE(tr.tx_hash.empty());
}

TEST(DustRegistrationResultTest, DefaultState)
{
    DustRegistrationResult dr;
    EXPECT_FALSE(dr.success);
    EXPECT_EQ(dr.estimated_dust_per_epoch, 0u);
}

TEST(FeeEstimationTest, DefaultState)
{
    FeeEstimation fe;
    EXPECT_EQ(fe.tx_fee, 0u);
    EXPECT_EQ(fe.total_fee, 0u);
}

TEST(SerializedTransactionTest, FromJson)
{
    auto tx = SerializedTransaction::from_json(nlohmann::json{{"test", true}});
    EXPECT_FALSE(tx.data.empty());
    EXPECT_FALSE(tx.tx_hash.empty());
    EXPECT_EQ(tx.tx_hash.size(), 64u); // SHA-256 hex
}

TEST(SubmissionEventTest, DefaultState)
{
    SubmissionEvent ev;
    EXPECT_EQ(ev.tag, SubmissionEventTag::Submitted);
    EXPECT_FALSE(ev.is_error());
}

TEST(SubmissionEventTest, WithError)
{
    SubmissionEvent ev;
    ev.error = "failed";
    EXPECT_TRUE(ev.is_error());
}

// ═══════════════════════════════════════════════════════════════
// TermsAndConditions Type Tests (Fix #13)
// ═══════════════════════════════════════════════════════════════

TEST(TermsAndConditionsTest, DefaultState)
{
    TermsAndConditions tc;
    EXPECT_TRUE(tc.hash.empty());
    EXPECT_TRUE(tc.url.empty());
}

TEST(TermsAndConditionsTest, SetValues)
{
    TermsAndConditions tc;
    tc.hash = "abc123def456";
    tc.url = "https://midnight.network/terms";
    EXPECT_EQ(tc.hash, "abc123def456");
    EXPECT_EQ(tc.url, "https://midnight.network/terms");
}

// Note: fetch_terms_and_conditions() is a static network call — tested
// via integration tests against live indexer, not unit tests

// ═══════════════════════════════════════════════════════════════
// TokenKindsToBalance Tests (Fix #8 Types)
// ═══════════════════════════════════════════════════════════════

TEST(TokenKindsToBalanceTest, AllEnabled)
{
    auto kinds = TokenKindsToBalance::all();
    EXPECT_TRUE(kinds.should_balance_unshielded);
    EXPECT_TRUE(kinds.should_balance_shielded);
    EXPECT_TRUE(kinds.should_balance_dust);
}

TEST(TokenKindsToBalanceTest, FromKinds_OnlyDust)
{
    auto kinds = TokenKindsToBalance::from_kinds({TokenKind::Dust});
    EXPECT_FALSE(kinds.should_balance_unshielded);
    EXPECT_FALSE(kinds.should_balance_shielded);
    EXPECT_TRUE(kinds.should_balance_dust);
}

TEST(TokenKindsToBalanceTest, FromKinds_UnshieldedAndShielded)
{
    auto kinds = TokenKindsToBalance::from_kinds({TokenKind::Unshielded, TokenKind::Shielded});
    EXPECT_TRUE(kinds.should_balance_unshielded);
    EXPECT_TRUE(kinds.should_balance_shielded);
    EXPECT_FALSE(kinds.should_balance_dust);
}

TEST(TokenKindsToBalanceTest, FromKinds_Empty)
{
    auto kinds = TokenKindsToBalance::from_kinds({});
    EXPECT_FALSE(kinds.should_balance_unshielded);
    EXPECT_FALSE(kinds.should_balance_shielded);
    EXPECT_FALSE(kinds.should_balance_dust);
}

// ═══════════════════════════════════════════════════════════════
// BalancingRecipe Type Tests (Fix #8 Types)
// ═══════════════════════════════════════════════════════════════

TEST(BalancingRecipeTest, DefaultState)
{
    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::FinalizedTransaction;
    EXPECT_FALSE(recipe.success);
    EXPECT_TRUE(recipe.error.empty());
}

TEST(BalancingRecipeTest, GetTransactions_Finalized)
{
    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::FinalizedTransaction;
    recipe.original_transaction = {{"tx_hash", "orig123"}};
    recipe.balancing_transaction = {{"tx_hash", "bal456"}};
    recipe.success = true;

    auto txs = recipe.get_transactions();
    EXPECT_EQ(txs.size(), 2u);
    EXPECT_EQ(txs[0]["tx_hash"], "orig123");
    EXPECT_EQ(txs[1]["tx_hash"], "bal456");
}

TEST(BalancingRecipeTest, GetTransactions_Unbound_NoBalancing)
{
    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::UnboundTransaction;
    recipe.base_transaction = {{"tx_hash", "base789"}};
    // No balancing_transaction
    recipe.success = true;

    auto txs = recipe.get_transactions();
    EXPECT_EQ(txs.size(), 1u);
    EXPECT_EQ(txs[0]["tx_hash"], "base789");
}

TEST(BalancingRecipeTest, GetTransactions_Unproven)
{
    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::UnprovenTransaction;
    recipe.transaction = {{"tx_hash", "unproven_abc"}};
    recipe.success = true;

    auto txs = recipe.get_transactions();
    EXPECT_EQ(txs.size(), 1u);
    EXPECT_EQ(txs[0]["tx_hash"], "unproven_abc");
}

// ═══════════════════════════════════════════════════════════════
// Balance*Transaction Tests (Fix #8)
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, BalanceFinalizedTransaction_AllKinds)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    nlohmann::json tx = {{"tx_hash", "finalized_test_123"}, {"type", "test_tx"}};

    auto recipe = facade.balance_finalized_transaction(tx);
    EXPECT_TRUE(recipe.success);
    EXPECT_EQ(recipe.type, BalancingRecipeType::FinalizedTransaction);
    EXPECT_FALSE(recipe.balancing_transaction.is_null());
    EXPECT_EQ(recipe.original_transaction["tx_hash"], "finalized_test_123");

    // Should have all 3 types of balancing merged
    auto txs = recipe.get_transactions();
    EXPECT_EQ(txs.size(), 2u); // original + balancing
}

TEST(WalletFacadeTest, BalanceFinalizedTransaction_OnlyDust)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    nlohmann::json tx = {{"tx_hash", "finalized_dust_only"}};

    auto kinds = TokenKindsToBalance::from_kinds({TokenKind::Dust});
    auto recipe = facade.balance_finalized_transaction(tx, {}, kinds);
    EXPECT_TRUE(recipe.success);
    EXPECT_FALSE(recipe.balancing_transaction.is_null());
}

TEST(WalletFacadeTest, BalanceUnboundTransaction_Success)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    nlohmann::json tx = {{"tx_hash", "unbound_test"}};

    auto recipe = facade.balance_unbound_transaction(tx);
    EXPECT_TRUE(recipe.success);
    EXPECT_EQ(recipe.type, BalancingRecipeType::UnboundTransaction);
    // Unshielded balancing is in-place
    EXPECT_TRUE(recipe.base_transaction.contains("balanced_unshielded"));
}

TEST(WalletFacadeTest, BalanceUnprovenTransaction_Success)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    nlohmann::json tx = {{"tx_hash", "unproven_test"}};

    auto recipe = facade.balance_unproven_transaction(tx);
    EXPECT_TRUE(recipe.success);
    EXPECT_EQ(recipe.type, BalancingRecipeType::UnprovenTransaction);
    EXPECT_FALSE(recipe.transaction.is_null());
}

TEST(WalletFacadeTest, BalanceTransaction_ClearedThrows)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    facade.clear();
    nlohmann::json tx = {{"tx_hash", "test"}};
    EXPECT_THROW(facade.balance_finalized_transaction(tx), std::runtime_error);
    EXPECT_THROW(facade.balance_unbound_transaction(tx), std::runtime_error);
    EXPECT_THROW(facade.balance_unproven_transaction(tx), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// Atomic Swap Tests (Fix #14)
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, InitSwap_EmptyInputs_Fails)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    SwapInputs inputs;
    std::vector<SwapOutputs> outputs;

    auto recipe = facade.init_swap(inputs, outputs);
    EXPECT_FALSE(recipe.success);
    EXPECT_EQ(recipe.error, "At least one shielded or unshielded swap is required.");
}

TEST(WalletFacadeTest, InitSwap_UnshieldedOnly_Success)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);

    SwapInputs inputs;
    inputs.unshielded["NIGHT"] = 1000;

    SwapOutputs out;
    out.unshielded_outputs.push_back({500, "NIGHT", "mn_addr_preview1recv"});
    std::vector<SwapOutputs> outputs = {out};

    auto recipe = facade.init_swap(inputs, outputs);
    EXPECT_TRUE(recipe.success);
    EXPECT_EQ(recipe.type, BalancingRecipeType::UnprovenTransaction);
    EXPECT_FALSE(recipe.transaction.is_null());
    EXPECT_EQ(recipe.transaction["type"], "swap_unshielded");
}

TEST(WalletFacadeTest, InitSwap_ShieldedOnly_Success)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);

    SwapInputs inputs;
    inputs.shielded["TOKEN_A"] = 2000;

    SwapOutputs out;
    out.shielded_outputs.push_back({1000, "TOKEN_A", "mn_shield-addr_preview1recv"});
    std::vector<SwapOutputs> outputs = {out};

    auto recipe = facade.init_swap(inputs, outputs);
    EXPECT_TRUE(recipe.success);
    EXPECT_EQ(recipe.transaction["type"], "swap_shielded");
}

TEST(WalletFacadeTest, InitSwap_Combined_Success)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);

    SwapInputs inputs;
    inputs.shielded["TOKEN_A"] = 2000;
    inputs.unshielded["NIGHT"] = 1000;

    SwapOutputs out;
    out.shielded_outputs.push_back({1000, "TOKEN_A", "mn_shield-addr_preview1recv"});
    out.unshielded_outputs.push_back({500, "NIGHT", "mn_addr_preview1recv"});
    std::vector<SwapOutputs> outputs = {out};

    auto recipe = facade.init_swap(inputs, outputs);
    EXPECT_TRUE(recipe.success);
    EXPECT_TRUE(recipe.transaction.contains("merged_with")); // Merged TX
}

TEST(WalletFacadeTest, InitSwap_WithFees_Success)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);

    SwapInputs inputs;
    inputs.unshielded["NIGHT"] = 1000;

    SwapOutputs out;
    out.unshielded_outputs.push_back({500, "NIGHT", "mn_addr_preview1recv"});
    std::vector<SwapOutputs> outputs = {out};

    auto recipe = facade.init_swap(inputs, outputs, true); // pay_fees = true
    EXPECT_TRUE(recipe.success);
    EXPECT_TRUE(recipe.transaction.contains("merged_with")); // Has fee TX merged
}

TEST(WalletFacadeTest, InitSwap_ClearedThrows)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    facade.clear();
    SwapInputs inputs;
    inputs.unshielded["NIGHT"] = 1000;
    EXPECT_THROW(facade.init_swap(inputs, {}), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// Revert Recipe Tests (Fix #12 extension)
// ═══════════════════════════════════════════════════════════════

TEST(WalletFacadeTest, RevertRecipe_Success)
{
    auto facade = WalletFacade::from_mnemonic(TEST_MNEMONIC, TEST_INDEXER);
    BalancingRecipe recipe;
    recipe.type = BalancingRecipeType::FinalizedTransaction;
    recipe.original_transaction = {{"tx_hash", "orig"}};
    recipe.balancing_transaction = {{"tx_hash", "bal"}};
    recipe.success = true;

    EXPECT_TRUE(facade.revert(recipe));
}

// ═══════════════════════════════════════════════════════════════
// SwapInputs/SwapOutputs Type Tests (Fix #14 Types)
// ═══════════════════════════════════════════════════════════════

TEST(SwapTypesTest, SwapInputs_Empty)
{
    SwapInputs inputs;
    EXPECT_TRUE(inputs.shielded.empty());
    EXPECT_TRUE(inputs.unshielded.empty());
}

TEST(SwapTypesTest, SwapOutputs_Empty)
{
    SwapOutputs outputs;
    EXPECT_TRUE(outputs.shielded_outputs.empty());
    EXPECT_TRUE(outputs.unshielded_outputs.empty());
}

TEST(SwapTypesTest, SwapInputs_WithData)
{
    SwapInputs inputs;
    inputs.shielded["TOKEN_A"] = 1000;
    inputs.unshielded["NIGHT"] = 5000;
    EXPECT_EQ(inputs.shielded.size(), 1u);
    EXPECT_EQ(inputs.unshielded["NIGHT"], 5000u);
}
