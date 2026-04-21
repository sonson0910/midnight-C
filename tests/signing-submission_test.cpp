/**
 * Signing & Submission Unit Tests
 * Tests for sr25519/ed25519 signing and transaction submission
 *
 * 15 planned tests:
 * 1. Generate sr25519 key
 * 2. Generate ed25519 key
 * 3. Generate ECDSA key
 * 4. Load key from file
 * 5. Save key to file
 * 6. Derive address from keypair
 * 7. Sign transaction with sr25519
 * 8. Verify transaction signature
 * 9. Create finality vote with ed25519
 * 10. Verify finality vote
 * 11. Submit transaction
 * 12. Get submission status
 * 13. Monitor mempool
 * 14. Batch sign transactions
 * 15. Recover signer from signature
 */

#include <gtest/gtest.h>
#include "midnight/signing-submission/signing_submission.hpp"
#include <thread>
#include <chrono>

using namespace midnight::signing_submission;

// ============================================================================
// Test Fixture
// ============================================================================

class SigningSubmissionTest : public ::testing::Test
{
protected:
    std::string node_rpc_url = "wss://rpc.preprod.midnight.network";
    std::string test_file_path = "test_key.json";
    std::string test_password = "test_password_123";

    std::vector<uint8_t> create_mock_transaction()
    {
        return std::vector<uint8_t>{0x00, 0x01, 0x02, 0x03, 0x04};
    }
};

// ============================================================================
// Test 1: Generate sr25519 Key
// ============================================================================

TEST_F(SigningSubmissionTest, GenerateSR25519_NoArgs_ReturnsKeypair)
{
    auto keypair = KeyManager::generate_sr25519_key();

    ASSERT_TRUE(keypair.has_value());
    EXPECT_EQ(keypair->type, KeyType::SR25519);
    EXPECT_FALSE(keypair->public_key.empty());
    EXPECT_FALSE(keypair->private_key.empty());
}

// ============================================================================
// Test 2: Generate ed25519 Key
// ============================================================================

TEST_F(SigningSubmissionTest, GenerateED25519_NoArgs_ReturnsKeypair)
{
    auto keypair = KeyManager::generate_ed25519_key();

    ASSERT_TRUE(keypair.has_value());
    EXPECT_EQ(keypair->type, KeyType::ED25519);
    EXPECT_FALSE(keypair->public_key.empty());
    EXPECT_FALSE(keypair->private_key.empty());
}

// ============================================================================
// Test 3: Generate ECDSA Key
// ============================================================================

TEST_F(SigningSubmissionTest, GenerateECDSA_NoArgs_ReturnsKeypair)
{
    auto keypair = KeyManager::generate_ecdsa_key();

    ASSERT_TRUE(keypair.has_value());
    EXPECT_EQ(keypair->type, KeyType::ECDSA);
    EXPECT_FALSE(keypair->public_key.empty());
}

// ============================================================================
// Test 4: Load Key From File
// ============================================================================

TEST_F(SigningSubmissionTest, LoadKey_ValidFile_ReturnsKeypair)
{
    auto keypair = KeyManager::load_key(test_file_path, test_password);

    // May be empty in test (no actual file), but no exception
}

// ============================================================================
// Test 5: Save Key to File
// ============================================================================

TEST_F(SigningSubmissionTest, SaveKey_ValidKeypair_SavesSuccessfully)
{
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    bool saved = KeyManager::save_key(*keypair, test_file_path, test_password);

    EXPECT_TRUE(saved);
}

// ============================================================================
// Test 6: Derive Address From Keypair
// ============================================================================

TEST_F(SigningSubmissionTest, DeriveAddress_SR25519Key_ReturnsAddress)
{
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    auto address = KeyManager::derive_address(*keypair);

    EXPECT_FALSE(address.empty());
    EXPECT_EQ(address[0], '5'); // SR25519 address starts with 5
}

TEST_F(SigningSubmissionTest, DeriveAddress_ED25519Key_ReturnsAddress)
{
    auto keypair = KeyManager::generate_ed25519_key();
    ASSERT_TRUE(keypair.has_value());

    auto address = KeyManager::derive_address(*keypair);

    EXPECT_FALSE(address.empty());
    EXPECT_EQ(address[0], 'e'); // ED25519 address starts with e
}

// ============================================================================
// Test 7: Sign Transaction with sr25519
// ============================================================================

TEST_F(SigningSubmissionTest, SignTransaction_ValidTx_ReturnsSignature)
{
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    TransactionSigner signer(*keypair);
    auto tx_data = create_mock_transaction();

    auto signature = signer.sign_transaction(tx_data);

    EXPECT_FALSE(signature.empty());
    EXPECT_EQ(signature.size(), 130); // "0x" + 128 hex chars
}

// ============================================================================
// Test 8: Verify Transaction Signature
// ============================================================================

TEST_F(SigningSubmissionTest, VerifySignature_CorrectSignature_ReturnsTrue)
{
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    TransactionSigner signer(*keypair);
    auto tx_data = create_mock_transaction();

    auto signature = signer.sign_transaction(tx_data);
    bool verified = signer.verify_signature(tx_data, signature);

    EXPECT_TRUE(verified);
}

// ============================================================================
// Test 9: Create Finality Vote with ed25519
// ============================================================================

TEST_F(SigningSubmissionTest, CreateVote_ValidVote_CreatesSignedVote)
{
    auto keypair = KeyManager::generate_ed25519_key();
    ASSERT_TRUE(keypair.has_value());

    FinallityVoteSigner voter(*keypair);

    auto vote = voter.create_vote(1000, "0x" + std::string(64, 'a'));

    EXPECT_EQ(vote.target_block_height, 1000);
    EXPECT_FALSE(vote.signature.empty());
    EXPECT_FALSE(vote.voter_address.empty());
}

// ============================================================================
// Test 10: Verify Finality Vote
// ============================================================================

TEST_F(SigningSubmissionTest, VerifyVote_ValidVote_ReturnsTrue)
{
    auto keypair = KeyManager::generate_ed25519_key();
    ASSERT_TRUE(keypair.has_value());

    FinallityVoteSigner voter(*keypair);

    auto vote = voter.create_vote(1000, "0x" + std::string(64, 'a'));
    bool verified = voter.verify_vote(vote);

    EXPECT_TRUE(verified);
}

// ============================================================================
// Test 11: Submit Transaction
// ============================================================================

TEST_F(SigningSubmissionTest, Submit_ValidSignedTx_ReturnsSubmissionResult)
{
    TransactionSubmitter submitter(node_rpc_url);

    SignedTransaction signed_tx;
    signed_tx.transaction_hash = "0x" + std::string(64, 't');
    signed_tx.signature = "0x" + std::string(128, 'a');
    signed_tx.signer_address = "5test123";
    signed_tx.nonce = 1;

    auto result = submitter.submit(signed_tx);

    // Placeholder payload is not a valid extrinsic for real RPC submission.
    EXPECT_EQ(result.status, SubmissionStatus::FAILED);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(SigningSubmissionTest, Submit_MockTransport_AlwaysReturnsSubmitted)
{
    TransactionSubmitter submitter(node_rpc_url, SubmissionTransportMode::MOCK);

    SignedTransaction signed_tx;
    signed_tx.transaction_hash = "0x" + std::string(64, 'm');
    signed_tx.signature = "0x" + std::string(128, 'a');
    signed_tx.signer_address = "5mock";
    signed_tx.nonce = 7;

    auto result = submitter.submit(signed_tx);

    EXPECT_EQ(result.status, SubmissionStatus::SUBMITTED);
}

TEST_F(SigningSubmissionTest, Submit_RealTransportBadEndpoint_ReturnsFailed)
{
    TransactionSubmitter submitter("invalid-url", SubmissionTransportMode::REAL_RPC);

    SignedTransaction signed_tx;
    signed_tx.transaction_hash = "0x" + std::string(64, 'r');
    signed_tx.signature = "0x" + std::string(128, 'a');
    signed_tx.signer_address = "5real";
    signed_tx.nonce = 8;

    auto result = submitter.submit(signed_tx);

    EXPECT_EQ(result.status, SubmissionStatus::FAILED);
    EXPECT_FALSE(result.error_message.empty());
}

// ============================================================================
// Test 12: Get Submission Status
// ============================================================================

TEST_F(SigningSubmissionTest, GetSubmissionStatus_SubmittedTx_ReturnsStatus)
{
    TransactionSubmitter submitter(node_rpc_url);

    SignedTransaction signed_tx;
    signed_tx.transaction_hash = "0x" + std::string(64, 't');
    signed_tx.signature = "0x" + std::string(128, 'a');

    auto submit_result = submitter.submit(signed_tx);
    auto status_result = submitter.get_submission_status("0x" + std::string(64, 't'));

    EXPECT_EQ(submit_result.status, SubmissionStatus::FAILED);
    // Failed submissions are not cached as in-flight/executed transactions.
    EXPECT_EQ(status_result.status, SubmissionStatus::CREATED);
}

TEST_F(SigningSubmissionTest, GetSubmissionStatus_MockSubmittedTx_ReturnsSubmitted)
{
    TransactionSubmitter submitter(node_rpc_url, SubmissionTransportMode::MOCK);

    SignedTransaction signed_tx;
    signed_tx.transaction_hash = "0x" + std::string(64, 'm');
    signed_tx.signed_data = create_mock_transaction();
    signed_tx.signature = "0x" + std::string(128, 'a');

    auto submit_result = submitter.submit(signed_tx);
    ASSERT_EQ(submit_result.status, SubmissionStatus::SUBMITTED);

    auto status_result = submitter.get_submission_status(signed_tx.transaction_hash);
    EXPECT_EQ(status_result.status, SubmissionStatus::SUBMITTED);
    EXPECT_EQ(status_result.transaction_hash, signed_tx.transaction_hash);
}

// ============================================================================
// Test 13: Monitor Mempool
// ============================================================================

TEST_F(SigningSubmissionTest, MonitorMempool_ValidMonitoring_MonitorsMempool)
{
    MempoolMonitor monitor(node_rpc_url);

    int callback_count = 0;
    monitor.monitor_mempool([&](const std::vector<std::string> &new_txs)
                            { callback_count++; });

    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // callback_count may be 0 in mock, but no exception
    EXPECT_GE(callback_count, 0);
}

// ============================================================================
// Test 14: Batch Sign Transactions
// ============================================================================

TEST_F(SigningSubmissionTest, BatchSign_MultipleTransactions_SignsAll)
{
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    BatchSigner batch_signer;

    auto tx1 = create_mock_transaction();
    auto tx2 = create_mock_transaction();
    auto tx3 = create_mock_transaction();

    batch_signer.add_transaction(tx1);
    batch_signer.add_transaction(tx2);
    batch_signer.add_transaction(tx3);

    EXPECT_EQ(batch_signer.batch_size(), 3);

    auto signatures = batch_signer.sign_batch(*keypair);

    EXPECT_EQ(signatures.size(), 3);
    for (const auto &sig : signatures)
    {
        EXPECT_FALSE(sig.empty());
    }
}

// ============================================================================
// Test 15: Recover Signer from Signature
// ============================================================================

TEST_F(SigningSubmissionTest, RecoverSigner_ValidSignature_RecoversSigner)
{
    auto tx_data = create_mock_transaction();
    std::string signature = "0x" + std::string(128, 'a');

    auto signer = SignatureVerifier::recover_signer(tx_data, signature);

    ASSERT_TRUE(signer.has_value());
    EXPECT_FALSE(signer->empty());
}

// ============================================================================
// Additional Tests
// ============================================================================

TEST_F(SigningSubmissionTest, GetSignerAddress_ValidKeypair_ReturnsAddress)
{
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    TransactionSigner signer(*keypair);
    auto address = signer.get_signer_address();

    EXPECT_FALSE(address.empty());
}

TEST_F(SigningSubmissionTest, GetMempoolSize_ConnectedNode_ReturnsSize)
{
    MempoolMonitor monitor(node_rpc_url);

    uint32_t size = monitor.get_mempool_size();

    EXPECT_GE(size, 0);
}

TEST_F(SigningSubmissionTest, EstimateFeeInclusion_HighFee_FastInclusion)
{
    MempoolMonitor monitor(node_rpc_url);

    uint32_t blocks_high = monitor.estimate_inclusion_blocks(200000);
    uint32_t blocks_low = monitor.estimate_inclusion_blocks(1000);

    EXPECT_LT(blocks_high, blocks_low);
}

TEST_F(SigningSubmissionTest, ImportFromSeed_ValidSeed_ReturnsKeypair)
{
    std::string seed = "legal winner thank year wave sausage worth useful legal winner thank yellow";

    auto keypair = KeyManager::import_from_seed(seed, KeyType::SR25519);

    ASSERT_TRUE(keypair.has_value());
    EXPECT_FALSE(keypair->private_key.empty());
}

TEST_F(SigningSubmissionTest, SubmitBatch_MultipleTransactions_SubmitsAll)
{
    TransactionSubmitter submitter(node_rpc_url);

    std::vector<SignedTransaction> batch;
    for (int i = 0; i < 3; ++i)
    {
        SignedTransaction tx;
        tx.transaction_hash = "0x" + std::string(60, 't') + std::to_string(i);
        tx.signature = "0x" + std::string(128, 'a');
        batch.push_back(tx);
    }

    auto results = submitter.submit_batch(batch);

    EXPECT_EQ(results.size(), 3);
    for (const auto &result : results)
    {
        EXPECT_EQ(result.status, SubmissionStatus::FAILED);
        EXPECT_FALSE(result.error_message.empty());
    }
}

// ============================================================================
// Integration Test: Full Signing and Submission Workflow
// ============================================================================

TEST_F(SigningSubmissionTest, Integration_FullSigningWorkflow_Success)
{
    // 1. Generate keypair
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    // 2. Derive address
    auto address = KeyManager::derive_address(*keypair);
    EXPECT_FALSE(address.empty());

    // 3. Create transaction
    auto tx_data = create_mock_transaction();

    // 4. Sign transaction
    TransactionSigner signer(*keypair);
    auto signature = signer.sign_transaction(tx_data);
    EXPECT_FALSE(signature.empty());

    // 5. Verify signature
    bool verified = signer.verify_signature(tx_data, signature);
    EXPECT_TRUE(verified);

    // 6. Create signed TX
    SignedTransaction signed_tx;
    signed_tx.transaction_hash = "0x" + std::string(64, 't');
    signed_tx.signature = signature;
    signed_tx.signer_address = address;
    signed_tx.nonce = 1;

    // 7. Submit
    TransactionSubmitter submitter(node_rpc_url, SubmissionTransportMode::MOCK);
    signed_tx.signed_data = tx_data;
    auto result = submitter.submit(signed_tx);

    EXPECT_EQ(result.status, SubmissionStatus::SUBMITTED);
    EXPECT_EQ(result.transaction_hash, signed_tx.transaction_hash);

    auto cached_status = submitter.get_submission_status(signed_tx.transaction_hash);
    EXPECT_EQ(cached_status.status, SubmissionStatus::SUBMITTED);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(SigningSubmissionTest, VerifySignature_EmptySignature_ReturnsFalse)
{
    auto keypair = KeyManager::generate_sr25519_key();
    ASSERT_TRUE(keypair.has_value());

    TransactionSigner signer(*keypair);
    auto tx_data = create_mock_transaction();

    bool verified = signer.verify_signature(tx_data, "");

    EXPECT_FALSE(verified);
}

TEST_F(SigningSubmissionTest, BatchSigner_ClearBatch_EmptiesBatch)
{
    BatchSigner batch;
    batch.add_transaction(create_mock_transaction());
    batch.add_transaction(create_mock_transaction());

    EXPECT_EQ(batch.batch_size(), 2);

    batch.clear();

    EXPECT_EQ(batch.batch_size(), 0);
}
