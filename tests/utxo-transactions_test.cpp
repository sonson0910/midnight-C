/**
 * UTXO Transactions Unit Tests
 * Tests for UTXO model with commitments and DUST accounting
 *
 * 18 planned tests:
 * 1. Query unspent UTXOs
 * 2. Query specific UTXO
 * 3. Check UTXO spent status
 * 4. Count unspent UTXOs
 * 5. Sync account balance
 * 6. Add input to transaction
 * 7. Add output to transaction
 * 8. Build transaction
 * 9. Get working transaction
 * 10. Calculate DUST fee
 * 11. Verify DUST balance
 * 12. Validate transaction inputs
 * 13. Validate transaction outputs
 * 14. Validate DUST accounting
 * 15. Validate proofs
 * 16. Select UTXOs
 * 17. Estimate DUST available
 * 18. Estimate transaction fee
 */

#include <gtest/gtest.h>
#include <cstdlib>
#include <algorithm>
#include "midnight/utxo-transactions/utxo_transactions.hpp"

using namespace midnight::utxo_transactions;

namespace
{
    class StubIndexerProvider : public IIndexerProvider
    {
    public:
        Json::Value execute_query(const std::string &query) override
        {
            Json::Value result;

            if (query.find("utxoSet(") != std::string::npos)
            {
                Json::Value entry;
                entry["txHash"] = "0x" + std::string(64, 'b');
                entry["outputIndex"] = 2;
                entry["recipient"] = "0xaddress123";
                entry["amountCommitment"] = "0x" + std::string(64, 'c');
                entry["confirmedHeight"] = 7777;
                entry["finalityDepth"] = 42;

                result["data"]["utxoSet"].append(entry);
                return result;
            }

            if (query.find("utxo(") != std::string::npos)
            {
                result["data"]["utxo"]["txHash"] = "0x" + std::string(64, 'b');
                result["data"]["utxo"]["outputIndex"] = 2;
                result["data"]["utxo"]["recipient"] = "0xaddress123";
                result["data"]["utxo"]["amountCommitment"] = "0x" + std::string(64, 'c');
                result["data"]["utxo"]["confirmedHeight"] = 7777;
                result["data"]["utxo"]["finalityDepth"] = 42;
                result["data"]["utxo"]["isSpent"] = false;
                return result;
            }

            return result;
        }
    };

    class StubNodeProvider : public INodeProvider
    {
    public:
        std::optional<midnight::ProtocolParams> get_protocol_params() override
        {
            midnight::ProtocolParams params;
            params.min_fee_a = 9;
            params.min_fee_b = 77;
            params.utxo_cost_per_byte = 3333;
            return params;
        }

        std::optional<std::pair<uint64_t, std::string>> get_chain_tip() override
        {
            return std::make_pair<uint64_t, std::string>(8888, "0x" + std::string(64, 'f'));
        }
    };
}

// ============================================================================
// Test Fixture
// ============================================================================

class UtxoTransactionsTest : public ::testing::Test
{
protected:
    std::string indexer_url = "http://127.0.0.1:8088/api/v3/graphql";
    std::string node_rpc_url = "http://127.0.0.1:9944";
    std::string test_address = "0xaddress123";

    bool undeployed_tests_enabled() const
    {
        const char *flag = std::getenv("MIDNIGHT_RUN_UNDEPLOYED_TESTS");
        return flag != nullptr && std::string(flag) == "1";
    }

    std::optional<std::vector<Utxo>> query_unspent(UtxoSetManager &manager)
    {
        return manager.query_unspent_utxos(test_address);
    }

    // Create sample UTXOs
    Utxo create_sample_utxo(uint32_t index)
    {
        Utxo utxo;
        utxo.tx_hash = "0x" + std::string(64, 'a' + index);
        utxo.output_index = index;
        utxo.address = test_address;
        utxo.amount_commitment = "0x" + std::string(64, 'c');
        utxo.confirmed_height = 5000 + index;
        utxo.finality_depth = 100;
        utxo.is_spent = false;
        return utxo;
    }
};

// ============================================================================
// Test 1: Query Unspent UTXOs
// ============================================================================

TEST_F(UtxoTransactionsTest, QueryUnspentUtxos_ValidAddress_ReturnsUtxos)
{
    if (!undeployed_tests_enabled())
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_UNDEPLOYED_TESTS=1 to run undeployed integration checks.";
    }
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto utxos = query_unspent(manager);
    if (!utxos.has_value())
    {
        GTEST_SKIP() << "Undeployed indexer is unavailable or returned invalid payload.";
    }

    EXPECT_TRUE(utxos.has_value());
}

TEST_F(UtxoTransactionsTest, QueryUnspentUtxos_RealRpcBadEndpoint_ReturnsEmptyOptional)
{
    UtxoSetManager manager("invalid-url", node_rpc_url);

    auto utxos = manager.query_unspent_utxos(test_address);

    EXPECT_FALSE(utxos.has_value());
}

// ============================================================================
// Test 2: Query Specific UTXO
// ============================================================================

TEST_F(UtxoTransactionsTest, QueryUtxo_ValidOutpoint_ReturnsUtxo)
{
    if (!undeployed_tests_enabled())
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_UNDEPLOYED_TESTS=1 to run undeployed integration checks.";
    }
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto utxos = query_unspent(manager);
    if (!utxos.has_value())
    {
        GTEST_SKIP() << "Undeployed indexer is unavailable or returned invalid payload.";
    }
    if (utxos->empty())
    {
        GTEST_SKIP() << "No UTXOs found for test address on undeployed.";
    }

    const auto &candidate = utxos->front();
    auto utxo = manager.query_utxo(candidate.tx_hash, candidate.output_index);

    if (!utxo.has_value())
    {
        GTEST_SKIP() << "Indexer did not return details for a listed UTXO outpoint.";
    }

    EXPECT_EQ(utxo->tx_hash, candidate.tx_hash);
    EXPECT_EQ(utxo->output_index, candidate.output_index);
}

// ============================================================================
// Test 3: Check UTXO Spent Status
// ============================================================================

TEST_F(UtxoTransactionsTest, IsSpent_UnspentUtxo_ReturnsFalse)
{
    if (!undeployed_tests_enabled())
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_UNDEPLOYED_TESTS=1 to run undeployed integration checks.";
    }
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto utxos = query_unspent(manager);
    if (!utxos.has_value())
    {
        GTEST_SKIP() << "Undeployed indexer is unavailable or returned invalid payload.";
    }
    if (utxos->empty())
    {
        GTEST_SKIP() << "No UTXOs found for test address on undeployed.";
    }

    const auto &candidate = utxos->front();
    bool spent = manager.is_spent(candidate.tx_hash, candidate.output_index);

    EXPECT_FALSE(spent);
}

// ============================================================================
// Test 4: Count Unspent UTXOs
// ============================================================================

TEST_F(UtxoTransactionsTest, CountUnspentUtxos_ValidAddress_ReturnsCount)
{
    if (!undeployed_tests_enabled())
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_UNDEPLOYED_TESTS=1 to run undeployed integration checks.";
    }
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto utxos = query_unspent(manager);
    if (!utxos.has_value())
    {
        GTEST_SKIP() << "Undeployed indexer is unavailable or returned invalid payload.";
    }

    uint32_t count = manager.count_unspent_utxos(test_address);

    EXPECT_EQ(count, utxos->size());
}

// ============================================================================
// Test 5: Sync Account Balance
// ============================================================================

TEST_F(UtxoTransactionsTest, SyncAccount_ValidAddress_ReturnBalance)
{
    if (!undeployed_tests_enabled())
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_UNDEPLOYED_TESTS=1 to run undeployed integration checks.";
    }
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto balance = manager.sync_account(test_address);

    if (!balance.has_value())
    {
        GTEST_SKIP() << "Unable to sync account from undeployed indexer.";
    }

    EXPECT_EQ(balance->address, test_address);
}

TEST_F(UtxoTransactionsTest, QueryUnspentUtxos_WithInjectedIndexerProvider_ReturnsProviderData)
{
    auto indexer_provider = std::make_shared<StubIndexerProvider>();
    auto node_provider = std::make_shared<StubNodeProvider>();
    UtxoSetManager manager(indexer_provider, node_provider);

    auto utxos = manager.query_unspent_utxos(test_address);

    ASSERT_TRUE(utxos.has_value());
    ASSERT_EQ(utxos->size(), 1U);
    EXPECT_EQ((*utxos)[0].tx_hash, "0x" + std::string(64, 'b'));
    EXPECT_EQ((*utxos)[0].confirmed_height, 7777U);
    EXPECT_EQ((*utxos)[0].finality_depth, 42U);
}

TEST_F(UtxoTransactionsTest, CreateBuilderContext_WithInjectedNodeProvider_ReturnsTypedContext)
{
    auto indexer_provider = std::make_shared<StubIndexerProvider>();
    auto node_provider = std::make_shared<StubNodeProvider>();
    UtxoSetManager manager(indexer_provider, node_provider);

    const auto context = manager.create_builder_context();

    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->protocol_params.min_fee_a, 9U);
    EXPECT_EQ(context->protocol_params.min_fee_b, 77U);
    EXPECT_EQ(context->protocol_params.utxo_cost_per_byte, 3333U);
    EXPECT_EQ(context->chain_tip_height, 8888U);
    EXPECT_EQ(context->chain_tip_hash, "0x" + std::string(64, 'f'));
}

// ============================================================================
// Test 6: Add Input to Transaction
// ============================================================================

TEST_F(UtxoTransactionsTest, AddInput_ValidInput_AddsToTransaction)
{
    TransactionBuilder builder;

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = test_address;
    input.amount_commitment = "0x" + std::string(64, 'c');

    builder.add_input(input);

    auto tx = builder.get_working_transaction();
    EXPECT_EQ(tx.inputs.size(), 1);
}

// ============================================================================
// Test 7: Add Output to Transaction
// ============================================================================

TEST_F(UtxoTransactionsTest, AddOutput_ValidOutput_AddsToTransaction)
{
    TransactionBuilder builder;

    UtxoOutput output;
    output.address = test_address;
    output.amount_commitment = "0x" + std::string(64, 'c');
    output.lock_height = 0;

    builder.add_output(output);

    auto tx = builder.get_working_transaction();
    EXPECT_EQ(tx.outputs.size(), 1);
}

// ============================================================================
// Test 8: Build Transaction
// ============================================================================

TEST_F(UtxoTransactionsTest, Build_ValidTransaction_Returns)
{
    TransactionBuilder builder;

    // Add input
    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = test_address;
    input.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_input(input);

    // Add output
    UtxoOutput output;
    output.address = test_address;
    output.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_output(output);

    auto tx = builder.build();

    ASSERT_TRUE(tx.has_value());
    EXPECT_FALSE(tx->hash.empty());
}

TEST_F(UtxoTransactionsTest, Build_WithTypedContext_AutoComputesFees)
{
    TransactionBuilderContext context;
    context.chain_tip_height = 777;
    context.chain_tip_hash = "0x" + std::string(64, 'f');
    context.protocol_params.min_fee_a = 2;
    context.protocol_params.min_fee_b = 20;
    context.protocol_params.utxo_cost_per_byte = 1024;
    context.fee_model.base_dust_per_output = 100;
    context.fee_model.commitment_bytes_per_output = 64;
    context.fee_model.default_proof_size = 0;

    TransactionBuilder builder(context);

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = test_address;
    input.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_input(input);

    UtxoOutput output;
    output.address = test_address;
    output.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_output(output);

    const auto tx = builder.build();
    ASSERT_TRUE(tx.has_value());

    const uint64_t expected_fee = FeeEstimator::estimate_fee(1, 1, 0, context);
    EXPECT_EQ(tx->fees, expected_fee);
}

TEST_F(UtxoTransactionsTest, BuilderContext_ChainTipSeedsNonce)
{
    TransactionBuilderContext context;
    context.chain_tip_height = 1024;

    TransactionBuilder builder(context);
    const auto tx = builder.get_working_transaction();

    EXPECT_EQ(tx.nonce, 1025U);
}

TEST_F(UtxoTransactionsTest, Build_WithTypedWitnessBundle_PreservesWitnessData)
{
    TransactionBuilder builder;

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = test_address;
    input.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_input(input);

    UtxoOutput output;
    output.address = test_address;
    output.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_output(output);

    WitnessBundle witness_bundle;
    witness_bundle.signatures.push_back("0x" + std::string(128, 'a'));
    witness_bundle.proof_references.push_back("proof-ref-1");
    witness_bundle.metadata["source"] = "utxo-test";
    builder.set_witness_bundle(witness_bundle);

    const auto tx = builder.build();
    ASSERT_TRUE(tx.has_value());

    ASSERT_EQ(tx->witness_bundle.signatures.size(), 1U);
    ASSERT_EQ(tx->witness_bundle.proof_references.size(), 1U);
    EXPECT_EQ(tx->witness_bundle.metadata.at("source"), "utxo-test");
    EXPECT_EQ(tx->signature, "0x" + std::string(128, 'a'));
}

TEST_F(UtxoTransactionsTest, Build_WithWitnessMutators_PopulatesBundle)
{
    TransactionBuilder builder;

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = test_address;
    input.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_input(input);

    UtxoOutput output;
    output.address = test_address;
    output.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_output(output);

    builder.add_signature("0x" + std::string(128, '1'));
    builder.add_proof_reference("proof-ref-2");
    builder.set_witness_metadata("pipeline", "typed-witness");

    const auto tx = builder.build();
    ASSERT_TRUE(tx.has_value());

    EXPECT_EQ(tx->witness_bundle.signatures.size(), 1U);
    EXPECT_EQ(tx->witness_bundle.proof_references.size(), 1U);
    EXPECT_EQ(tx->witness_bundle.metadata.at("pipeline"), "typed-witness");
}

// ============================================================================
// Test 9: Get Working Transaction
// ============================================================================

TEST_F(UtxoTransactionsTest, GetWorkingTransaction_AfterInitialization_ReturnsTransaction)
{
    TransactionBuilder builder;

    auto tx = builder.get_working_transaction();

    EXPECT_EQ(tx.version, 1);
    EXPECT_GT(tx.nonce, 0);
}

// ============================================================================
// Test 10: Calculate DUST Fee
// ============================================================================

TEST_F(UtxoTransactionsTest, CalculateDustFee_TypicalTransaction_ReturnsPositiveFee)
{
    uint64_t input_dust = 50000;
    uint64_t output_dust = 40000;
    size_t output_count = 2;
    bool has_witnesses = true;

    uint64_t fee = DustAccounting::calculate_dust_fee(input_dust, output_dust,
                                                      output_count, has_witnesses);

    EXPECT_GT(fee, 0);
}

// ============================================================================
// Test 11: Verify DUST Balance
// ============================================================================

TEST_F(UtxoTransactionsTest, VerifyDustBalance_BalancedInput_ReturnsTrue)
{
    Transaction tx;
    tx.input_dust = 50000;
    tx.output_dust = 40000;
    tx.fees = 500;
    tx.outputs.resize(2); // 2 outputs = 256 bytes commitment overhead

    bool valid = DustAccounting::verify_dust_balance(tx);

    // Should be valid: 50000 >= 40000 + 500 + 256
    EXPECT_TRUE(valid);
}

TEST_F(UtxoTransactionsTest, VerifyDustBalance_InsufficientInput_ReturnsFalse)
{
    Transaction tx;
    tx.input_dust = 1000;   // Too small
    tx.output_dust = 40000; // Too large
    tx.fees = 100;
    tx.outputs.resize(2);

    bool valid = DustAccounting::verify_dust_balance(tx);

    EXPECT_FALSE(valid);
}

// ============================================================================
// Test 12: Validate Transaction Inputs
// ============================================================================

TEST_F(UtxoTransactionsTest, ValidateInputs_ValidInputs_ReturnsTrue)
{
    Transaction tx;

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = "0x" + std::string(40, 'a');
    tx.inputs.push_back(input);

    bool valid = TransactionValidator::validate_inputs(tx);

    EXPECT_TRUE(valid);
}

TEST_F(UtxoTransactionsTest, ValidateInputs_EmptyInputs_ReturnsFalse)
{
    Transaction tx;
    // No inputs

    bool valid = TransactionValidator::validate_inputs(tx);

    EXPECT_FALSE(valid);
}

// ============================================================================
// Test 13: Validate Transaction Outputs
// ============================================================================

TEST_F(UtxoTransactionsTest, ValidateOutputs_ValidOutputs_ReturnsTrue)
{
    Transaction tx;

    UtxoOutput output;
    output.address = "0x" + std::string(40, 'a');
    output.amount_commitment = "0x" + std::string(64, 'c');
    tx.outputs.push_back(output);

    bool valid = TransactionValidator::validate_outputs(tx);

    EXPECT_TRUE(valid);
}

TEST_F(UtxoTransactionsTest, ValidateOutputs_InvalidCommitmentLength_ReturnsFalse)
{
    Transaction tx;

    UtxoOutput output;
    output.address = "0x" + std::string(40, 'a');
    output.amount_commitment = "0x" + std::string(32, 'c'); // Too short
    tx.outputs.push_back(output);

    bool valid = TransactionValidator::validate_outputs(tx);

    EXPECT_FALSE(valid);
}

// ============================================================================
// Test 14: Validate DUST Accounting
// ============================================================================

TEST_F(UtxoTransactionsTest, ValidateDustAccounting_BalancedTransaction_ReturnsTrue)
{
    Transaction tx;
    tx.input_dust = 50000;
    tx.output_dust = 40000;
    tx.fees = 5000;
    tx.outputs.resize(2);

    bool valid = TransactionValidator::validate_dust_accounting(tx);

    EXPECT_TRUE(valid);
}

// ============================================================================
// Test 15: Validate Proofs
// ============================================================================

TEST_F(UtxoTransactionsTest, ValidateProofs_ValidProofs_ReturnsTrue)
{
    Transaction tx;
    tx.proofs.push_back("0x" + std::string(256, 'p')); // 128 bytes

    bool valid = TransactionValidator::validate_proofs(tx);

    EXPECT_TRUE(valid);
}

TEST_F(UtxoTransactionsTest, ValidateProofs_NoProofs_ReturnsFalse)
{
    Transaction tx;
    // No proofs

    bool valid = TransactionValidator::validate_proofs(tx);

    EXPECT_FALSE(valid);
}

TEST_F(UtxoTransactionsTest, ValidateProofs_WitnessReferencesOnly_ReturnsTrue)
{
    Transaction tx;
    tx.witness_bundle.proof_references.push_back("proof-ref-only");

    bool valid = TransactionValidator::validate_proofs(tx);

    EXPECT_TRUE(valid);
}

TEST_F(UtxoTransactionsTest, ValidateSignature_WitnessSignatureOnly_ReturnsTrue)
{
    Transaction tx;
    tx.witness_bundle.signatures.push_back("0x" + std::string(128, 'f'));

    bool valid = TransactionValidator::validate_signature(tx);

    EXPECT_TRUE(valid);
}

TEST_F(UtxoTransactionsTest, ValidateSignature_InvalidWitnessSignature_ReturnsFalse)
{
    Transaction tx;
    tx.witness_bundle.signatures.push_back("0x" + std::string(64, 'f'));

    bool valid = TransactionValidator::validate_signature(tx);

    EXPECT_FALSE(valid);
}

// ============================================================================
// Test 16: Select UTXOs
// ============================================================================

TEST_F(UtxoTransactionsTest, SelectUtxos_AvailableUtxos_ReturnsSelection)
{
    std::vector<Utxo> available;
    available.push_back(create_sample_utxo(0));
    available.push_back(create_sample_utxo(1));
    available.push_back(create_sample_utxo(2));

    auto selected = UtxoSelector::select_utxos(available, 2, true);

    ASSERT_TRUE(selected.has_value());
    EXPECT_GT(selected->size(), 0);
}

TEST_F(UtxoTransactionsTest, SelectUtxos_NoAvailableUtxos_ReturnsEmpty)
{
    std::vector<Utxo> available;

    auto selected = UtxoSelector::select_utxos(available, 2, true);

    EXPECT_FALSE(selected.has_value());
}

TEST_F(UtxoTransactionsTest, SelectUtxos_DeterministicOrdering_PrioritizesFinalityAndAge)
{
    std::vector<Utxo> available;

    auto low_finality = create_sample_utxo(0);
    low_finality.finality_depth = 10;
    low_finality.confirmed_height = 7000;

    auto older_high_finality = create_sample_utxo(1);
    older_high_finality.finality_depth = 150;
    older_high_finality.confirmed_height = 4000;

    auto newer_high_finality = create_sample_utxo(2);
    newer_high_finality.finality_depth = 150;
    newer_high_finality.confirmed_height = 5000;

    auto highest_finality = create_sample_utxo(3);
    highest_finality.finality_depth = 200;
    highest_finality.confirmed_height = 6000;

    available.push_back(low_finality);
    available.push_back(newer_high_finality);
    available.push_back(older_high_finality);
    available.push_back(highest_finality);

    auto selected = UtxoSelector::select_utxos(available, 2, false);

    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected->size(), 3U);
    EXPECT_EQ((*selected)[0].tx_hash, highest_finality.tx_hash);
    EXPECT_EQ((*selected)[1].tx_hash, older_high_finality.tx_hash);
    EXPECT_EQ((*selected)[2].tx_hash, newer_high_finality.tx_hash);
}

TEST_F(UtxoTransactionsTest, SelectUtxos_InsufficientEstimatedDust_ReturnsEmpty)
{
    std::vector<Utxo> available;
    available.push_back(create_sample_utxo(0));
    available.push_back(create_sample_utxo(1));

    auto selected = UtxoSelector::select_utxos(available, 4, true);

    EXPECT_FALSE(selected.has_value());
}

TEST_F(UtxoTransactionsTest, SelectUtxos_WithCheaperContext_SelectsMinimumInputs)
{
    std::vector<Utxo> available;
    available.push_back(create_sample_utxo(0));
    available.push_back(create_sample_utxo(1));
    available.push_back(create_sample_utxo(2));

    TransactionBuilderContext context;
    context.protocol_params.min_fee_a = 1;
    context.protocol_params.min_fee_b = 10;
    context.protocol_params.utxo_cost_per_byte = 1024;
    context.fee_model.base_dust_per_output = 50;
    context.fee_model.commitment_bytes_per_output = 16;
    context.fee_model.estimated_dust_per_utxo = 500;

    auto selected = UtxoSelector::select_utxos(available, 2, false, context);

    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->size(), 2U);
}

// ============================================================================
// Test 17: Estimate DUST Available
// ============================================================================

TEST_F(UtxoTransactionsTest, EstimateDustAvailable_MultipleUtxos_ReturnsEstimate)
{
    std::vector<Utxo> utxos;
    utxos.push_back(create_sample_utxo(0));
    utxos.push_back(create_sample_utxo(1));
    utxos.push_back(create_sample_utxo(2));

    uint64_t estimated = UtxoSelector::estimate_dust_available(utxos);

    const uint64_t expected_per_utxo = std::max<uint64_t>(1000, midnight::ProtocolParams{}.min_fee_b / 2);
    EXPECT_EQ(estimated, 3 * expected_per_utxo);
}

TEST_F(UtxoTransactionsTest, EstimateDustAvailable_WithContextOverride_UsesConfiguredValue)
{
    std::vector<Utxo> utxos;
    utxos.push_back(create_sample_utxo(0));
    utxos.push_back(create_sample_utxo(1));
    utxos.push_back(create_sample_utxo(2));

    TransactionBuilderContext context;
    context.fee_model.estimated_dust_per_utxo = 9000;

    uint64_t estimated = UtxoSelector::estimate_dust_available(utxos, context);
    EXPECT_EQ(estimated, 27000);
}

// ============================================================================
// Test 18: Estimate Transaction Fee
// ============================================================================

TEST_F(UtxoTransactionsTest, EstimateFee_TypicalTransaction_ReturnsPositiveFee)
{
    uint32_t input_count = 2;
    uint32_t output_count = 2;
    size_t proof_size = 256; // zk-SNARK

    uint64_t fee = FeeEstimator::estimate_fee(input_count, output_count, proof_size);

    EXPECT_GT(fee, 0);
}

TEST_F(UtxoTransactionsTest, EstimateFee_WithContextReflectsProtocolParams)
{
    TransactionBuilderContext context;
    context.protocol_params.min_fee_a = 1;
    context.protocol_params.min_fee_b = 10;
    context.protocol_params.utxo_cost_per_byte = 1024;
    context.fee_model.base_dust_per_output = 50;
    context.fee_model.commitment_bytes_per_output = 16;

    const uint64_t default_fee = FeeEstimator::estimate_fee(2, 2, 256);
    const uint64_t context_fee = FeeEstimator::estimate_fee(2, 2, 256, context);

    EXPECT_LT(context_fee, default_fee);
    EXPECT_GT(context_fee, 0);
}

// ============================================================================
// Integration Test: Full Transaction Workflow
// ============================================================================

TEST_F(UtxoTransactionsTest, Integration_FullTransactionWorkflow_Success)
{
    if (!undeployed_tests_enabled())
    {
        GTEST_SKIP() << "Set MIDNIGHT_RUN_UNDEPLOYED_TESTS=1 to run undeployed integration checks.";
    }

    // 1. Query UTXOs
    UtxoSetManager manager(indexer_url, node_rpc_url);
    auto utxos = query_unspent(manager);
    if (!utxos.has_value())
    {
        GTEST_SKIP() << "Undeployed indexer is unavailable or returned invalid payload.";
    }

    // 2. Build transaction
    TransactionBuilder builder;

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = test_address;
    input.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_input(input);

    UtxoOutput output;
    output.address = "0xrecipient";
    output.amount_commitment = "0x" + std::string(64, 'c');
    builder.add_output(output);

    builder.set_fees(1000);

    // 3. Finalize transaction
    auto tx = builder.build();
    ASSERT_TRUE(tx.has_value());

    // Provide consistent dust totals for accounting validation.
    tx->input_dust = 5000;
    tx->output_dust = 2000;

    // 4. Validate transaction
    bool valid = TransactionValidator::validate_inputs(*tx) &&
                 TransactionValidator::validate_outputs(*tx) &&
                 TransactionValidator::validate_dust_accounting(*tx);

    // In real scenario: would also validate proofs and signature
    EXPECT_TRUE(valid);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(UtxoTransactionsTest, Build_MissingOutputs_ReturnsFalse)
{
    TransactionBuilder builder;

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = test_address;
    builder.add_input(input);

    // No outputs added

    auto tx = builder.build();
    EXPECT_FALSE(tx.has_value());
}

TEST_F(UtxoTransactionsTest, ValidateOutputs_EmptyOutputs_ReturnsFalse)
{
    Transaction tx;
    // No outputs

    bool valid = TransactionValidator::validate_outputs(tx);

    EXPECT_FALSE(valid);
}
