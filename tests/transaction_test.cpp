#include <gtest/gtest.h>
#include <stdexcept>

#include "midnight/blockchain/transaction.hpp"

using midnight::blockchain::Transaction;

namespace
{
    Transaction build_sample_transaction(const std::string &tx_id)
    {
        Transaction tx(tx_id);

        Transaction::Input input{};
        input.tx_hash = "0x" + std::string(64, 'a');
        input.output_index = 3;
        tx.add_input(input);

        Transaction::Output output{};
        output.address = "mn1qexampleaddress";
        output.amount = 123456;
        output.assets["policy1"]["assetA"] = 10;
        output.assets["policy1"]["assetB"] = 20;
        tx.add_output(output);

        Transaction::Certificate cert{};
        cert.type = Transaction::Certificate::Type::STAKE_ADDRESS_DELEGATION;
        cert.stake_key_hash = "stake-key-hash";
        cert.pool_id = "pool-id";
        tx.add_certificate(cert);

        tx.set_validity(9000, 5000);
        return tx;
    }
}

TEST(TransactionTest, CborRoundTrip_PreservesTransactionBody)
{
    Transaction tx = build_sample_transaction("tx-roundtrip");

    const std::string encoded = tx.to_cbor_hex();
    const Transaction decoded = Transaction::from_cbor_hex(encoded);

    ASSERT_EQ(decoded.get_tx_id(), "tx-roundtrip");
    ASSERT_EQ(decoded.get_inputs().size(), 1U);
    ASSERT_EQ(decoded.get_outputs().size(), 1U);
    ASSERT_EQ(decoded.get_certificates().size(), 1U);

    EXPECT_EQ(decoded.get_inputs()[0].tx_hash, tx.get_inputs()[0].tx_hash);
    EXPECT_EQ(decoded.get_inputs()[0].output_index, tx.get_inputs()[0].output_index);
    EXPECT_EQ(decoded.get_outputs()[0].address, tx.get_outputs()[0].address);
    EXPECT_EQ(decoded.get_outputs()[0].amount, tx.get_outputs()[0].amount);
    EXPECT_EQ(decoded.get_outputs()[0].assets, tx.get_outputs()[0].assets);
    EXPECT_EQ(static_cast<int>(decoded.get_certificates()[0].type),
              static_cast<int>(tx.get_certificates()[0].type));
}

TEST(TransactionTest, CalculateHash_IgnoresDisplayTxId)
{
    Transaction tx_a = build_sample_transaction("tx-a");
    Transaction tx_b = build_sample_transaction("tx-b");

    const std::string hash_a = tx_a.calculate_hash();
    const std::string hash_b = tx_b.calculate_hash();

    EXPECT_EQ(hash_a, hash_b);
}

TEST(TransactionTest, CalculateHash_ChangesWhenBodyChanges)
{
    Transaction tx_a = build_sample_transaction("tx-a");
    Transaction tx_b = build_sample_transaction("tx-a");

    Transaction::Output second_output{};
    second_output.address = "mn1qanotheraddress";
    second_output.amount = 1;
    tx_b.add_output(second_output);

    EXPECT_NE(tx_a.calculate_hash(), tx_b.calculate_hash());
}

TEST(TransactionTest, FromCborHex_InvalidHex_Throws)
{
    EXPECT_THROW((void)Transaction::from_cbor_hex("xyz"), std::invalid_argument);
}
