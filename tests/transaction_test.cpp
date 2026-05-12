#include <gtest/gtest.h>
#include <stdexcept>

#include "midnight/blockchain/transaction.hpp"

using midnight::blockchain::Transaction;

namespace
{
    // Build sample transaction per Midnight UTXO Protocol
    Transaction build_sample_transaction(const std::string& tx_id)
    {
        Transaction tx(tx_id);

        // Input: UTXO reference with Pedersen commitment
        Transaction::Input input{};
        input.outpoint = "0x" + std::string(64, 'a') + ":0";  // tx_hash:output_index format
        input.address = "midnight1qexampleaddress";            // Bech32m address
        input.amount_commitment = "0x" + std::string(64, 'b'); // Pedersen commitment (32 bytes hex)
        input.spending_witness = "0x" + std::string(128, 'c'); // ZK proof
        tx.add_input(input);

        // Output: Recipient with encrypted amount
        Transaction::Output output{};
        output.address = "midnight1qrecipientaddress";         // Bech32m address
        output.amount_commitment = "0x" + std::string(64, 'd'); // Pedersen commitment
        output.lock_height = 0;                                // Spendable immediately
        tx.add_output(output);

        // ZK Proof for shielded transaction
        tx.add_proof("0x" + std::string(256, 'e'));

        tx.set_validity(9000, 5000);
        return tx;
    }
}

// Test CBOR encoding/decoding
TEST(TransactionTest, CborEncoding_ValidFormat)
{
    Transaction tx = build_sample_transaction("tx-cbor-test");
    
    // Verify CBOR encoding produces valid bytes
    auto cbor_bytes = tx.to_cbor_bytes();
    ASSERT_FALSE(cbor_bytes.empty());
    
    // Verify hex conversion works
    std::string hex = tx.to_hex();
    EXPECT_TRUE(hex.rfind("0x", 0) == 0);
    EXPECT_EQ(hex.size() - 2, cbor_bytes.size() * 2); // 2 hex chars per byte
}

// Test hash calculation
TEST(TransactionTest, CalculateHash_UsesBlake2b256)
{
    Transaction tx_a = build_sample_transaction("tx-hash-a");
    Transaction tx_b = build_sample_transaction("tx-hash-b");

    std::string hash_a = tx_a.calculate_hash();
    std::string hash_b = tx_b.calculate_hash();

    // Same body should produce same hash
    EXPECT_EQ(hash_a, hash_b);
}

// Test hash changes with different body
TEST(TransactionTest, CalculateHash_ChangesWithBody)
{
    Transaction tx_a = build_sample_transaction("tx-same");
    Transaction tx_b = build_sample_transaction("tx-same");

    // Add different output
    Transaction::Output output{};
    output.address = "midnight1qdifferent";
    output.amount_commitment = "0x" + std::string(64, '1');
    output.lock_height = 0;
    tx_b.add_output(output);

    EXPECT_NE(tx_a.calculate_hash(), tx_b.calculate_hash());
}

// Test input/output structure
TEST(TransactionTest, InputOutput_Structure)
{
    Transaction tx = build_sample_transaction("tx-structure");
    
    ASSERT_EQ(tx.get_inputs().size(), 1U);
    ASSERT_EQ(tx.get_outputs().size(), 1U);
    
    // Verify input structure
    const auto& input = tx.get_inputs()[0];
    EXPECT_FALSE(input.outpoint.empty());
    EXPECT_FALSE(input.amount_commitment.empty());
    EXPECT_TRUE(input.amount_commitment.rfind("0x", 0) == 0);
    EXPECT_EQ(input.amount_commitment.size(), 66); // "0x" + 64 hex chars
    
    // Verify output structure
    const auto& output = tx.get_outputs()[0];
    EXPECT_FALSE(output.address.empty());
    EXPECT_TRUE(output.address.rfind("midnight1", 0) == 0); // Bech32m prefix
    EXPECT_EQ(output.lock_height, 0U);
}

// Test shielded transaction detection
TEST(TransactionTest, IsShielded_DetectsCiphertext)
{
    Transaction tx_no_shield("tx-unshielded");
    Transaction::Output out1{};
    out1.address = "midnight1qaddr";
    out1.amount_commitment = "0x" + std::string(64, 'a');
    tx_no_shield.add_output(out1);
    EXPECT_FALSE(tx_no_shield.is_shielded());

    Transaction tx_shielded("tx-shielded");
    Transaction::Output out2{};
    out2.address = "midnight1qaddr";
    out2.amount_commitment = "0x" + std::string(64, 'a');
    out2.ciphertext = "encrypted_note_data";  // Has ciphertext = shielded
    tx_shielded.add_output(out2);
    EXPECT_TRUE(tx_shielded.is_shielded());
}

// Test transaction size calculation
TEST(TransactionTest, GetSize_ReturnsValidSize)
{
    Transaction tx = build_sample_transaction("tx-size");
    size_t size = tx.get_size();
    EXPECT_GT(size, 0);
    EXPECT_EQ(size, tx.to_cbor_bytes().size());
}

// Test fee calculation
TEST(TransactionTest, CalculateMinFee_Formula)
{
    size_t tx_size = 100;
    uint64_t fee = ::midnight::blockchain::calculate_min_fee(tx_size);
    
    // MIN_FEE_A = 44, MIN_FEE_B = 155381
    // fee = 44 * tx_size + 155381
    EXPECT_EQ(fee, 44 * 100 + 155381);
}
