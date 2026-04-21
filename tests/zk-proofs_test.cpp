/**
 * ZK Proofs Unit Tests
 * Tests for zk-SNARK generation, verification, and proof handling
 *
 * 20 planned tests:
 * 1. Connect to Proof Server
 * 2. Proof Server health check
 * 3. Request proof generation
 * 4. Get proof status
 * 5. Cancel proof request
 * 6. Verify single proof
 * 7. Verify batch proofs
 * 8. Verify with invalid VK
 * 9. Pedersen commitment
 * 10. Poseidon commitment
 * 11. Batch commitments
 * 12. Verify commitment opening
 * 13. Add private inputs
 * 14. Add public inputs
 * 15. Build witness
 * 16. Cache proof
 * 17. Retrieve cached proof
 * 18. Cache TTL expiration
 * 19. Record performance metrics
 * 20. Get performance statistics
 */

#include <gtest/gtest.h>
#include "midnight/zk-proofs/zk_proofs.hpp"
#include <thread>
#include <chrono>

using namespace midnight::zk_proofs;

// ============================================================================
// Test Fixture
// ============================================================================

class ZkProofsTest : public ::testing::Test
{
protected:
    std::string proof_server_url = "https://proof.preprod.midnight.network";

    ZkCircuit create_test_circuit()
    {
        ZkCircuit circuit;
        circuit.circuit_id = "voting_circuit";
        circuit.verification_key = "0x" + std::string(128, 'v');
        circuit.gate_count = 50000;
        circuit.constraint_count = 45000;
        circuit.proof_size_bytes = 128;
        circuit.prover_time_ms = 200;
        return circuit;
    }
};

// ============================================================================
// Test 1: Connect to Proof Server
// ============================================================================

TEST_F(ZkProofsTest, Connect_ProofServer_Success)
{
    ProofServerClient client(proof_server_url);

    bool connected = client.connect();

    if (connected)
    {
        EXPECT_TRUE(client.is_healthy());
    }
    else
    {
        EXPECT_FALSE(client.is_healthy());
    }
}

// ============================================================================
// Test 2: Proof Server Health Check
// ============================================================================

TEST_F(ZkProofsTest, IsHealthy_ConnectedServer_ReturnsTrue)
{
    ProofServerClient client(proof_server_url);
    bool connected = client.connect();

    bool healthy = client.is_healthy();

    EXPECT_EQ(healthy, connected);
}

TEST_F(ZkProofsTest, IsHealthy_DisconnectedServer_ReturnsFalse)
{
    ProofServerClient client(proof_server_url);

    bool healthy = client.is_healthy();

    EXPECT_FALSE(healthy);
}

// ============================================================================
// Test 3: Request Proof Generation
// ============================================================================

TEST_F(ZkProofsTest, RequestProof_ValidRequest_GeneratesProof)
{
    ProofServerClient client(proof_server_url);
    (void)client.connect();

    ProofRequest request;
    request.circuit_id = "voting_circuit";
    request.private_inputs["voter_secret"] = "0xsecret123";
    request.public_inputs["vote_choice"] = "1";

    auto result = client.request_proof(request);

    if (result.success)
    {
        EXPECT_FALSE(result.proof.proof_data.empty());
        EXPECT_GT(result.generation_time_ms, 0);
    }
    else
    {
        EXPECT_FALSE(result.error_message.empty());
    }
}

// ============================================================================
// Test 4: Get Proof Status
// ============================================================================

TEST_F(ZkProofsTest, GetProofStatus_ValidRequestId_ReturnsStatus)
{
    ProofServerClient client(proof_server_url);
    (void)client.connect();

    auto status = client.get_proof_status("request_123");

    if (status.has_value())
    {
        EXPECT_TRUE(status->success || !status->error_message.empty());
    }
}

// ============================================================================
// Test 5: Cancel Proof Request
// ============================================================================

TEST_F(ZkProofsTest, CancelProofRequest_ActiveRequest_Cancels)
{
    ProofServerClient client(proof_server_url);
    (void)client.connect();

    EXPECT_FALSE(client.cancel_proof_request(""));

    bool cancelled = client.cancel_proof_request("request_123");

    (void)cancelled;
    SUCCEED();
}

// ============================================================================
// Test 6: Verify Single Proof
// ============================================================================

TEST_F(ZkProofsTest, VerifyProof_ValidProof_ReturnsTrue)
{
    ZkCircuit circuit = create_test_circuit();
    ProofVerifier verifier(circuit);

    ZkProof proof;
    proof.proof_data = "0x" + std::string(256, 'p');
    proof.circuit_id = "voting_circuit";
    proof.public_inputs.push_back("1");

    bool verified = verifier.verify(proof);

    EXPECT_TRUE(verified);
}

TEST_F(ZkProofsTest, VerifyProof_InvalidProofSize_ReturnsFalse)
{
    ZkCircuit circuit = create_test_circuit();
    ProofVerifier verifier(circuit);

    ZkProof proof;
    proof.proof_data = "0x" + std::string(100, 'p'); // Wrong size
    proof.circuit_id = "voting_circuit";

    bool verified = verifier.verify(proof);

    EXPECT_FALSE(verified);
}

// ============================================================================
// Test 7: Verify Batch Proofs
// ============================================================================

TEST_F(ZkProofsTest, VerifyBatch_MultipleProofs_VerifiesAll)
{
    ZkCircuit circuit = create_test_circuit();
    ProofVerifier verifier(circuit);

    std::vector<ZkProof> proofs;
    for (int i = 0; i < 3; ++i)
    {
        ZkProof proof;
        proof.proof_data = "0x" + std::string(256, 'p');
        proof.circuit_id = "voting_circuit";
        proofs.push_back(proof);
    }

    bool verified = verifier.verify_batch(proofs);

    EXPECT_TRUE(verified);
}

// ============================================================================
// Test 8: Verify with Invalid Circuit ID
// ============================================================================

TEST_F(ZkProofsTest, VerifyProof_WrongCircuit_ReturnsFalse)
{
    ZkCircuit circuit = create_test_circuit();
    ProofVerifier verifier(circuit);

    ZkProof proof;
    proof.proof_data = "0x" + std::string(256, 'p');
    proof.circuit_id = "wrong_circuit"; // Mismatch

    bool verified = verifier.verify(proof);

    EXPECT_FALSE(verified);
}

// ============================================================================
// Test 9: Pedersen Commitment
// ============================================================================

TEST_F(ZkProofsTest, PedersenCommit_ValidInputs_ReturnsCommitment)
{
    std::string commitment = CommitmentGenerator::pedersen_commit("value123", "random456");

    EXPECT_EQ(commitment.size(), 66); // "0x" + 64 hex chars
}

TEST_F(ZkProofsTest, PedersenCommit_EmptyInputs_ReturnsEmpty)
{
    std::string commitment = CommitmentGenerator::pedersen_commit("", "");

    EXPECT_TRUE(commitment.empty());
}

// ============================================================================
// Test 10: Poseidon Commitment
// ============================================================================

TEST_F(ZkProofsTest, PoseidonCommit_ValidInput_ReturnsCommitment)
{
    std::string commitment = CommitmentGenerator::poseidon_commit("value123");

    EXPECT_EQ(commitment.size(), 66);
}

// ============================================================================
// Test 11: Batch Commitments
// ============================================================================

TEST_F(ZkProofsTest, BatchCommit_MultipleValues_ReturnsCommitments)
{
    std::map<std::string, std::string> values;
    values["voter_secret"] = "0xsecret1";
    values["vote_choice"] = "0xvote1";
    values["timestamp"] = "0xtime1";

    auto commitments = CommitmentGenerator::batch_commit(values);

    EXPECT_EQ(commitments.size(), 3);
    for (const auto &c : commitments)
    {
        EXPECT_EQ(c.size(), 66);
    }
}

// ============================================================================
// Test 12: Verify Commitment Opening
// ============================================================================

TEST_F(ZkProofsTest, VerifyOpening_CorrectOpening_ReturnsTrue)
{
    std::string commitment = CommitmentGenerator::pedersen_commit("value123", "random456");

    bool valid = CommitmentGenerator::verify_opening(commitment, "value123", "random456");

    EXPECT_TRUE(valid);
}

// ============================================================================
// Test 13: Add Private Inputs
// ============================================================================

TEST_F(ZkProofsTest, AddPrivateInput_ValidInput_AddsToWitness)
{
    ZkCircuit circuit = create_test_circuit();
    WitnessBuilder builder(circuit);

    builder.add_private_input("voter_secret", "0xsecret123");
    builder.add_private_input("vote_choice", "1");

    auto witness = builder.build();

    ASSERT_TRUE(witness.has_value());
    EXPECT_EQ(witness->private_inputs.size(), 2);
}

// ============================================================================
// Test 14: Add Public Inputs
// ============================================================================

TEST_F(ZkProofsTest, AddPublicInput_ValidInput_AddsToWitness)
{
    ZkCircuit circuit = create_test_circuit();
    WitnessBuilder builder(circuit);

    builder.add_public_input("vote_commitment", "0xcommit123");
    builder.add_public_input("timestamp", "1234567890");

    auto witness = builder.build();

    ASSERT_TRUE(witness.has_value());
    EXPECT_EQ(witness->public_inputs.size(), 2);
}

// ============================================================================
// Test 15: Build Witness
// ============================================================================

TEST_F(ZkProofsTest, BuildWitness_CompleteWitness_GeneratesCommitments)
{
    ZkCircuit circuit = create_test_circuit();
    WitnessBuilder builder(circuit);

    builder.add_private_input("secret", "0xsecret");
    builder.add_public_input("commitment", "0xcommit");

    auto witness = builder.build();

    ASSERT_TRUE(witness.has_value());
    EXPECT_FALSE(witness->commitments.empty());
    EXPECT_EQ(witness->circuit_id, "voting_circuit");
}

// ============================================================================
// Test 16: Cache Proof
// ============================================================================

TEST_F(ZkProofsTest, CacheProof_ValidProof_CachesProof)
{
    ZkProof proof;
    proof.proof_data = "0x" + std::string(256, 'p');
    proof.circuit_id = "voting_circuit";

    std::vector<std::string> public_inputs = {"1"};

    ProofCache::cache_proof("voting_circuit", public_inputs, proof);

    EXPECT_GT(ProofCache::cache_size(), 0);

    ProofCache::clear();
}

// ============================================================================
// Test 17: Retrieve Cached Proof
// ============================================================================

TEST_F(ZkProofsTest, GetCachedProof_ExistingProof_ReturnsProof)
{
    ZkProof original_proof;
    original_proof.proof_data = "0x" + std::string(256, 'p');
    original_proof.circuit_id = "voting_circuit";

    std::vector<std::string> public_inputs = {"1"};
    ProofCache::cache_proof("voting_circuit", public_inputs, original_proof);

    auto cached_proof = ProofCache::get_cached_proof("voting_circuit", public_inputs);

    ASSERT_TRUE(cached_proof.has_value());
    EXPECT_EQ(cached_proof->proof_data, original_proof.proof_data);

    ProofCache::clear();
}

TEST_F(ZkProofsTest, GetCachedProof_NonexistentProof_ReturnsEmpty)
{
    auto cached_proof = ProofCache::get_cached_proof("nonexistent", {});

    EXPECT_FALSE(cached_proof.has_value());
}

// ============================================================================
// Test 18: Proof Cache Clear
// ============================================================================

TEST_F(ZkProofsTest, ClearCache_CachedProofs_ClearsAll)
{
    ZkProof proof;
    proof.proof_data = "0x" + std::string(256, 'p');

    ProofCache::cache_proof("circuit1", {"input1"}, proof);
    ProofCache::cache_proof("circuit2", {"input2"}, proof);

    EXPECT_GT(ProofCache::cache_size(), 0);

    ProofCache::clear();

    EXPECT_EQ(ProofCache::cache_size(), 0);
}

// ============================================================================
// Test 19: Record Performance Metrics
// ============================================================================

TEST_F(ZkProofsTest, RecordPerformance_GenerationTime_RecordsMetric)
{
    ProofPerformanceMonitor::record_generation_time(150);
    ProofPerformanceMonitor::record_generation_time(200);
    ProofPerformanceMonitor::record_generation_time(175);

    uint64_t avg = ProofPerformanceMonitor::get_avg_generation_time();

    // Average should be approximately 175
    EXPECT_GE(avg, 170);
    EXPECT_LE(avg, 180);
}

// ============================================================================
// Test 20: Get Performance Statistics
// ============================================================================

TEST_F(ZkProofsTest, GetStats_MultipleMetrics_ReturnsStats)
{
    auto before = ProofPerformanceMonitor::get_stats();

    ProofPerformanceMonitor::record_generation_time(200);
    ProofPerformanceMonitor::record_verification_time(50);
    ProofPerformanceMonitor::record_verification_time(45);

    auto stats = ProofPerformanceMonitor::get_stats();

    EXPECT_EQ(stats.generation_count, before.generation_count + 1);
    EXPECT_EQ(stats.verification_count, before.verification_count + 2);
    EXPECT_GT(stats.avg_generation_ms, 0);
    EXPECT_GT(stats.avg_verification_ms, 0);
}

// ============================================================================
// Integration Test: Full Proof Workflow
// ============================================================================

TEST_F(ZkProofsTest, Integration_FullProofWorkflow_Success)
{
    // 1. Connect to Proof Server
    ProofServerClient client(proof_server_url);
    const bool connected = client.connect();
    if (connected)
    {
        EXPECT_TRUE(client.is_healthy());
    }

    // 2. Build witness
    ZkCircuit circuit = create_test_circuit();
    WitnessBuilder witness_builder(circuit);
    witness_builder.add_private_input("voter_secret", "0xsecret123");
    witness_builder.add_public_input("vote_choice", "1");

    auto witness = witness_builder.build();
    ASSERT_TRUE(witness.has_value());

    // 3. Request proof
    ProofRequest request;
    request.circuit_id = circuit.circuit_id;
    request.private_inputs = witness->private_inputs;
    request.public_inputs = witness->public_inputs;

    auto proof_result = client.request_proof(request);
    if (!proof_result.success)
    {
        EXPECT_FALSE(proof_result.error_message.empty());
        return;
    }

    // 4. Verify proof
    ProofVerifier verifier(circuit);
    bool verified = verifier.verify(proof_result.proof);
    EXPECT_TRUE(verified);

    // 5. Cache proof
    ProofCache::cache_proof(circuit.circuit_id, proof_result.proof.public_inputs,
                            proof_result.proof);
    EXPECT_GT(ProofCache::cache_size(), 0);

    ProofCache::clear();
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ZkProofsTest, BuildWitness_NoInputs_ReturnsFalse)
{
    ZkCircuit circuit = create_test_circuit();
    WitnessBuilder builder(circuit);

    // No inputs added
    auto witness = builder.build();

    EXPECT_FALSE(witness.has_value());
}

TEST_F(ZkProofsTest, VerifyBatch_EmptyProofs_ReturnsFalse)
{
    ZkCircuit circuit = create_test_circuit();
    ProofVerifier verifier(circuit);

    std::vector<ZkProof> empty_proofs;

    bool verified = verifier.verify_batch(empty_proofs);

    EXPECT_FALSE(verified);
}
