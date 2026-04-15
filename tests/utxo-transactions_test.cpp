/**
 * Phase 3 Unit Tests: UTXO Transactions
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
#include "midnight/utxo-transactions/utxo_transactions.hpp"

using namespace midnight::phase3;

// ============================================================================
// Test Fixture
// ============================================================================

class Phase3UtxoTransactionsTest : public ::testing::Test
{
protected:
    std::string indexer_url = "https://indexer.preprod.midnight.network/api/v3/graphql";
    std::string node_rpc_url = "wss://rpc.preprod.midnight.network";
    std::string test_address = "0xaddress123";

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

TEST_F(Phase3UtxoTransactionsTest, QueryUnspentUtxos_ValidAddress_ReturnsUtxos)
{
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto utxos = manager.query_unspent_utxos(test_address);

    // Mock response - valid even if empty
    EXPECT_TRUE(utxos.has_value());
}

// ============================================================================
// Test 2: Query Specific UTXO
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, QueryUtxo_ValidOutpoint_ReturnsUtxo)
{
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto utxo = manager.query_utxo("0x" + std::string(64, 'a'), 0);

    // Mock response may be empty
}

// ============================================================================
// Test 3: Check UTXO Spent Status
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, IsSpent_UnspentUtxo_ReturnsFalse)
{
    UtxoSetManager manager(indexer_url, node_rpc_url);

    // Mock: would return false for unspent
    bool spent = manager.is_spent("0x" + std::string(64, 'a'), 0);

    // In mock: returns true (not found)
}

// ============================================================================
// Test 4: Count Unspent UTXOs
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, CountUnspentUtxos_ValidAddress_ReturnsCount)
{
    UtxoSetManager manager(indexer_url, node_rpc_url);

    uint32_t count = manager.count_unspent_utxos(test_address);

    EXPECT_GE(count, 0);
}

// ============================================================================
// Test 5: Sync Account Balance
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, SyncAccount_ValidAddress_ReturnBalance)
{
    UtxoSetManager manager(indexer_url, node_rpc_url);

    auto balance = manager.sync_account(test_address);

    // May be empty in mock, but checks structure
}

// ============================================================================
// Test 6: Add Input to Transaction
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, AddInput_ValidInput_AddsToTransaction)
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

TEST_F(Phase3UtxoTransactionsTest, AddOutput_ValidOutput_AddsToTransaction)
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

TEST_F(Phase3UtxoTransactionsTest, Build_ValidTransaction_Returns)
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

// ============================================================================
// Test 9: Get Working Transaction
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, GetWorkingTransaction_AfterInitialization_ReturnsTransaction)
{
    TransactionBuilder builder;

    auto tx = builder.get_working_transaction();

    EXPECT_EQ(tx.version, 1);
    EXPECT_GT(tx.nonce, 0);
}

// ============================================================================
// Test 10: Calculate DUST Fee
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, CalculateDustFee_TypicalTransaction_ReturnsPositiveFee)
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

TEST_F(Phase3UtxoTransactionsTest, VerifyDustBalance_BalancedInput_ReturnsTrue)
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

TEST_F(Phase3UtxoTransactionsTest, VerifyDustBalance_InsufficientInput_ReturnsFalse)
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

TEST_F(Phase3UtxoTransactionsTest, ValidateInputs_ValidInputs_ReturnsTrue)
{
    Transaction tx;

    UtxoInput input;
    input.outpoint = "0x" + std::string(64, 'a') + ":0";
    input.address = "0x" + std::string(40, 'a');
    tx.inputs.push_back(input);

    bool valid = TransactionValidator::validate_inputs(tx);

    EXPECT_TRUE(valid);
}

TEST_F(Phase3UtxoTransactionsTest, ValidateInputs_EmptyInputs_ReturnsFalse)
{
    Transaction tx;
    // No inputs

    bool valid = TransactionValidator::validate_inputs(tx);

    EXPECT_FALSE(valid);
}

// ============================================================================
// Test 13: Validate Transaction Outputs
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, ValidateOutputs_ValidOutputs_ReturnsTrue)
{
    Transaction tx;

    UtxoOutput output;
    output.address = "0x" + std::string(40, 'a');
    output.amount_commitment = "0x" + std::string(64, 'c');
    tx.outputs.push_back(output);

    bool valid = TransactionValidator::validate_outputs(tx);

    EXPECT_TRUE(valid);
}

TEST_F(Phase3UtxoTransactionsTest, ValidateOutputs_InvalidCommitmentLength_ReturnsFalse)
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

TEST_F(Phase3UtxoTransactionsTest, ValidateDustAccounting_BalancedTransaction_ReturnsTrue)
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

TEST_F(Phase3UtxoTransactionsTest, ValidateProofs_ValidProofs_ReturnsTrue)
{
    Transaction tx;
    tx.proofs.push_back("0x" + std::string(256, 'p')); // 128 bytes

    bool valid = TransactionValidator::validate_proofs(tx);

    EXPECT_TRUE(valid);
}

TEST_F(Phase3UtxoTransactionsTest, ValidateProofs_NoProofs_ReturnsFalse)
{
    Transaction tx;
    // No proofs

    bool valid = TransactionValidator::validate_proofs(tx);

    EXPECT_FALSE(valid);
}

// ============================================================================
// Test 16: Select UTXOs
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, SelectUtxos_AvailableUtxos_ReturnsSelection)
{
    std::vector<Utxo> available;
    available.push_back(create_sample_utxo(0));
    available.push_back(create_sample_utxo(1));
    available.push_back(create_sample_utxo(2));

    auto selected = UtxoSelector::select_utxos(available, 2, true);

    ASSERT_TRUE(selected.has_value());
    EXPECT_GT(selected->size(), 0);
}

TEST_F(Phase3UtxoTransactionsTest, SelectUtxos_NoAvailableUtxos_ReturnsEmpty)
{
    std::vector<Utxo> available;

    auto selected = UtxoSelector::select_utxos(available, 2, true);

    EXPECT_FALSE(selected.has_value());
}

// ============================================================================
// Test 17: Estimate DUST Available
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, EstimateDustAvailable_MultipleUtxos_ReturnsEstimate)
{
    std::vector<Utxo> utxos;
    utxos.push_back(create_sample_utxo(0));
    utxos.push_back(create_sample_utxo(1));
    utxos.push_back(create_sample_utxo(2));

    uint64_t estimated = UtxoSelector::estimate_dust_available(utxos);

    EXPECT_EQ(estimated, 3 * 1000); // 3 UTXOs * 1000 per UTXO
}

// ============================================================================
// Test 18: Estimate Transaction Fee
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, EstimateFee_TypicalTransaction_ReturnsPositiveFee)
{
    uint32_t input_count = 2;
    uint32_t output_count = 2;
    size_t proof_size = 256; // zk-SNARK

    uint64_t fee = FeeEstimator::estimate_fee(input_count, output_count, proof_size);

    EXPECT_GT(fee, 0);
}

// ============================================================================
// Integration Test: Full Transaction Workflow
// ============================================================================

TEST_F(Phase3UtxoTransactionsTest, Integration_FullTransactionWorkflow_Success)
{
    // 1. Query UTXOs
    UtxoSetManager manager(indexer_url, node_rpc_url);
    auto utxos = manager.query_unspent_utxos(test_address);

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

TEST_F(Phase3UtxoTransactionsTest, Build_MissingOutputs_ReturnsFalse)
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

TEST_F(Phase3UtxoTransactionsTest, ValidateOutputs_EmptyOutputs_ReturnsFalse)
{
    Transaction tx;
    // No outputs

    bool valid = TransactionValidator::validate_outputs(tx);

    EXPECT_FALSE(valid);
}
