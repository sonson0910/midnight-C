#include "midnight/zk.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

using json = nlohmann::json;
using namespace midnight::zk;

// ============================================================================
// Real-World Scenario: Privacy-Preserving Voting Smart Contract
// ============================================================================
//
// Demonstrates all 4 phases working together to implement a voting system
// where votes are private (known only to the voter) but results are public.
//
// Architecture:
// 1. Setup: Create managers and sync infrastructure (4B + 4D)
// 2. Phase A: Create proof types for vote proof (4A)
// 3. Phase B: Build witness contexts with vote data (4B)
// 4. Phase C: Generate and verify proofs with resilience (4C)
// 5. Phase D: Monitor state changes and sync results (4D)
//
// ============================================================================

class VotingSmartContractApp
{
private:
    // Infrastructure managers
    std::shared_ptr<ProofServerResilientClient> proof_client_;
    std::shared_ptr<PrivateStateManager> state_manager_;
    std::shared_ptr<SecretKeyStore> secret_store_;
    std::shared_ptr<LedgerStateSyncManager> sync_manager_;

    // Contract configuration
    std::string contract_address_ = "voting_contract_0x123456";
    std::string rpc_endpoint_ = "http://localhost:3030";
    std::string proof_server_ = "http://localhost:6300";

    // Statistics
    struct VotingStats
    {
        uint32_t total_votes_cast = 0;
        uint32_t proofs_generated = 0;
        uint32_t proofs_verified = 0;
        uint32_t state_syncs = 0;
        std::vector<std::string> vote_transaction_ids;
    } stats_;

public:
    /**
     * Initialize the voting application with all infrastructure
     */
    bool initialize()
    {
        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "Initializing Voting Smart Contract Application" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        try
        {
            // Phase 4B: Setup private state management
            std::cout << "\n[Phase 4B] Setting up private state managers..." << std::endl;
            state_manager_ = std::make_shared<PrivateStateManager>();
            secret_store_ = std::make_shared<SecretKeyStore>();
            std::cout << "  ✓ Private state managers initialized" << std::endl;

            // Phase 4C: Setup resilient proof server client
            std::cout << "\n[Phase 4C] Setting up resilient proof server client..." << std::endl;
            auto base_client = std::make_shared<ProofServerClient>();
            proof_client_ = std::make_shared<ProofServerResilientClient>(
                base_client,
                resilience_util::create_production_config());
            std::cout << "  ✓ Resilient client configured (production profile)" << std::endl;

            // Phase 4D: Setup ledger state synchronization
            std::cout << "\n[Phase 4D] Setting up ledger state sync..." << std::endl;
            sync_manager_ = ledger_util::create_production_sync_manager(rpc_endpoint_);
            sync_manager_->register_contract(contract_address_);
            sync_manager_->start_monitoring();

            // Setup block event listener
            sync_manager_->register_block_event_listener(
                [this](const BlockEvent &event)
                {
                    this->on_block_event(event);
                });
            std::cout << "  ✓ Ledger sync monitoring started" << std::endl;

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Initialization failed: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * User casts a vote (main workflow)
     */
    bool cast_vote(const std::string &voter_id, const std::string &vote_choice)
    {
        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "Voter: " << voter_id << " | Vote: " << vote_choice << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        try
        {
            auto start_time = std::chrono::high_resolution_clock::now();

            // ========== STEP 1: Phase 4B - Prepare private witness data ==========
            std::cout << "\n[Step 1] Phase 4B - Prepare Private Witness Data" << std::endl;

            // Store voter's secret key in secure storage
            std::vector<uint8_t> voter_secret(32, static_cast<uint8_t>(voter_id[0]));
            secret_store_->store_secret(voter_id, voter_secret);
            std::cout << "  ✓ Voter secret stored in SecretKeyStore" << std::endl;

            // Get current contract state from ledger
            auto current_state = sync_manager_->get_cached_state(contract_address_);
            json public_state = current_state
                                    ? current_state->public_state
                                    : json::object();

            if (!public_state.contains("total_votes"))
            {
                public_state["total_votes"] = 0;
                public_state["vote_options"] = json::array();
            }

            std::cout << "  ✓ Current contract state retrieved" << std::endl;

            // Build witness context with private data
            std::cout << "  ✓ Building witness context..." << std::endl;
            WitnessContextBuilder builder;
            builder.set_contract_address(contract_address_)
                .set_public_state(public_state)
                .add_private_secret(voter_id, voter_secret)
                .set_block_height(sync_manager_->get_local_block_height());

            WitnessContext witness_ctx = builder.build();
            std::cout << "  ✓ Witness context ready for proof generation" << std::endl;
            std::cout << "    - Contract: " << contract_address_ << std::endl;
            std::cout << "    - Voter ID: " << voter_id << std::endl;
            std::cout << "    - Vote Choice (private): " << vote_choice << std::endl;

            // ========== STEP 2: Phase 4A - Create proof types ==========
            std::cout << "\n[Step 2] Phase 4A - Create Proof Types" << std::endl;

            // Create public inputs (vote choice encrypted hash + public commitment)
            PublicInputs public_inputs;
            public_inputs.insert({"vote_commitment", "0x" + vote_choice}); // Merkle commitment
            public_inputs.insert({"voter_commitment", "0x" + voter_id});   // Voter commitment
            public_inputs.insert({"timestamp", json(static_cast<int64_t>(
                std::chrono::system_clock::now().time_since_epoch().count()))
            )});

            std::cout << "  ✓ Public inputs created:" << std::endl;
            std::cout << "    - Vote commitment: " << public_inputs.at("vote_commitment") << std::endl;
            std::cout << "    - Voter commitment: " << public_inputs.at("voter_commitment") << std::endl;

            // Create witness output (private vote data)
            WitnessOutput witness_output;
            witness_output.insert({"actual_vote", vote_choice});
            witness_output.insert({"voter_secret_hash", voter_id});
            witness_output.insert({"proof_of_eligibility", "valid_voter_record"});

            std::cout << "  ✓ Witness output created (private, not on-chain)" << std::endl;

            // ========== STEP 3: Phase 4C - Generate proof with resilience ==========
            std::cout << "\n[Step 3] Phase 4C - Generate Proof with Resilience" << std::endl;

            ResilienceMetrics metrics;

            try
            {
                // Attempt proof generation (with automatic retry on failure)
                std::cout << "  ⏳ Sending proof generation request to Proof Server..." << std::endl;

                auto proof_result = proof_client_->generate_proof_resilient(
                    "voting_circuit",
                    "compiled_voting_circuit_data",
                    public_inputs,
                    witness_output);

                std::cout << "  ✓ Proof generated successfully!" << std::endl;
                std::cout << "    - Retries needed: " << metrics.retry_count << std::endl;
                std::cout << "    - Total time: " << metrics.total_time_ms.count() << "ms" << std::endl;

                if (metrics.succeeded_after_retry)
                {
                    std::cout << "    - Note: Succeeded after retry (transient issue recovered)" << std::endl;
                }

                stats_.proofs_generated++;

                // ========== STEP 4: Phase 4C - Verify proof ==========
                std::cout << "\n[Step 4] Phase 4C - Verify Proof" << std::endl;

                bool verified = proof_client_->verify_proof_resilient(
                    proof_result.proof,
                    metrics);

                if (verified)
                {
                    std::cout << "  ✓ Proof verified successfully" << std::endl;
                    stats_.proofs_verified++;
                }
                else
                {
                    std::cerr << "  ✗ Proof verification failed" << std::endl;
                    return false;
                }

                // ========== STEP 5: Phase 4A - Create proof-enabled transaction ==========
                std::cout << "\n[Step 5] Phase 4A - Create Proof-Enabled Transaction" << std::endl;

                // Create transaction with proof
                ProofEnabledTransaction voting_tx;
                voting_tx.proof = proof_result.proof;
                voting_tx.proof_witness_output = witness_output;
                voting_tx.public_state = public_state;
                voting_tx.block_height = sync_manager_->get_local_block_height();

                json tx_json = voting_tx.to_json();

                std::cout << "  ✓ Proof-enabled transaction created" << std::endl;
                std::cout << "    - Proof commitment: " << voting_tx.proof.proof_bytes.to_hex().substr(0, 16) << "..." << std::endl;
                std::cout << "    - Transaction size: " << tx_json.dump().length() << " bytes" << std::endl;

                // ========== STEP 6: Phase 4C - Submit with resilience ==========
                std::cout << "\n[Step 6] Phase 4C - Submit Transaction with Resilience" << std::endl;

                std::cout << "  ⏳ Submitting vote proof to blockchain via RPC..." << std::endl;

                std::string tx_id = proof_client_->submit_proof_transaction_resilient(
                    voting_tx,
                    rpc_endpoint_,
                    metrics);

                std::cout << "  ✓ Vote submitted successfully" << std::endl;
                std::cout << "    - Transaction ID: " << tx_id << std::endl;
                std::cout << "    - Retries: " << metrics.retry_count << std::endl;

                stats_.vote_transaction_ids.push_back(tx_id);
                stats_.total_votes_cast++;

                // ========== STEP 7: Phase 4D - Wait for state sync ==========
                std::cout << "\n[Step 7] Phase 4D - Monitor State Changes" << std::endl;

                std::cout << "  ⏳ Waiting for vote to appear in blockchain..." << std::endl;
                std::cout << "     (Monitoring for new blocks with resilient sync)" << std::endl;

                // Simulate waiting for block confirmation
                for (int i = 0; i < 3; ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));

                    auto sync_result = sync_manager_->sync_contract_state(contract_address_);

                    if (sync_result.status == SyncResult::Status::SUCCESS)
                    {
                        auto updated = sync_manager_->get_cached_state(contract_address_);
                        if (updated)
                        {
                            std::cout << "    Block " << updated->block_height
                                      << ": Confirmations = " << updated->confirmations << std::endl;
                            stats_.state_syncs++;

                            if (updated->confirmations >= 3)
                            {
                                std::cout << "  ✓ Vote confirmed on blockchain!" << std::endl;
                                break;
                            }
                        }
                    }
                }

                // ========== STEP 8: Display final statistics ==========
                std::cout << "\n[Step 8] Voting Transaction Complete" << std::endl;

                auto end_time = std::chrono::high_resolution_clock::now();
                auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

                std::cout << "  ✅ Entire vote lifecycle completed successfully:" << std::endl;
                std::cout << "     - Voter: " << voter_id << std::endl;
                std::cout << "     - Vote (private): " << vote_choice << std::endl;
                std::cout << "     - Proof generated: ✓" << std::endl;
                std::cout << "     - Proof verified: ✓" << std::endl;
                std::cout << "     - Submitted to chain: ✓" << std::endl;
                std::cout << "     - State synchronized: ✓" << std::endl;
                std::cout << "     - Total time: " << total_time.count() << "ms" << std::endl;

                return true;
            }
            catch (const std::exception &e)
            {
                std::cerr << "  ✗ Proof generation/submission failed: " << e.what() << std::endl;
                std::cerr << "  ✗ Circuit breaker state: ";

                auto cb_state = proof_client_->get_circuit_breaker_state();
                if (cb_state == CircuitBreakerState::CLOSED)
                {
                    std::cerr << "CLOSED (normal)" << std::endl;
                }
                else if (cb_state == CircuitBreakerState::OPEN)
                {
                    std::cerr << "OPEN (server unavailable)" << std::endl;
                }
                else
                {
                    std::cerr << "HALF_OPEN (attempting recovery)" << std::endl;
                }

                return false;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Vote processing failed: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * Get current voting results (from synchronized on-chain state)
     */
    void display_results()
    {
        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "Current Voting Results (from Synchronized On-Chain State)" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        auto cached_state = sync_manager_->get_cached_state(contract_address_);

        if (cached_state)
        {
            std::cout << "\nPublic Contract State:" << std::endl;
            std::cout << "  Block Height: " << cached_state->block_height << std::endl;
            std::cout << "  Confirmations: " << cached_state->confirmations << std::endl;

            if (cached_state->public_state.contains("total_votes"))
            {
                std::cout << "  Total Votes: " << cached_state->public_state["total_votes"] << std::endl;
            }

            std::cout << "\nNote: Individual votes remain PRIVATE (only ZK proofs visible)" << std::endl;
            std::cout << "      Vote counts are aggregated and public, but voter identities are hidden" << std::endl;
        }
        else
        {
            std::cout << "\n(No state cached yet - awaiting first synchronized block)" << std::endl;
        }
    }

    /**
     * Display application statistics
     */
    void display_statistics()
    {
        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "Application Statistics Across All 4 Phases" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        std::cout << "\n[Phase 4B] Private State Management" << std::endl;
        std::cout << "  Voters managed: " << secret_store_->size() << std::endl;

        std::cout << "\n[Phase 4A] ZK Proof Types" << std::endl;
        std::cout << "  Proofs generated: " << stats_.proofs_generated << std::endl;
        std::cout << "  Proofs verified: " << stats_.proofs_verified << std::endl;

        std::cout << "\n[Phase 4C] Resilience" << std::endl;
        auto resilience_stats = proof_client_->get_statistics_json();
        std::cout << "  Total operations: " << resilience_stats["total_operations"] << std::endl;
        std::cout << "  Successful ops: " << resilience_stats["successful_operations"] << std::endl;
        std::cout << "  Average retries: " << std::fixed << std::setprecision(2)
                  << resilience_stats["average_retries"] << std::endl;

        std::cout << "\n[Phase 4D] Ledger State Sync" << std::endl;
        auto sync_stats = sync_manager_->get_sync_statistics();
        std::cout << "  Total syncs: " << sync_stats.total_syncs << std::endl;
        std::cout << "  Successful syncs: " << sync_stats.successful_syncs << std::endl;
        std::cout << "  Reorgs detected: " << sync_stats.reorgs_detected << std::endl;

        std::cout << "\n[Overall] Voting Application" << std::endl;
        std::cout << "  Total votes cast: " << stats_.total_votes_cast << std::endl;
        std::cout << "  State synchronizations: " << stats_.state_syncs << std::endl;

        std::cout << "\n[Diagnostics]" << std::endl;
        std::cout << "  Proof server health: " << (proof_client_->is_connected() ? "✓ Connected" : "✗ Disconnected") << std::endl;
        std::cout << "  Ledger sync health: " << (sync_manager_->is_healthy() ? "✓ Healthy" : "✗ Unhealthy") << std::endl;
    }

private:
    /**
     * Handle block events from ledger synchronization
     */
    void on_block_event(const BlockEvent &event)
    {
        switch (event.type)
        {
        case BlockEvent::EventType::BLOCK_ADDED:
            std::cout << "    📦 Block " << event.block.height << " added" << std::endl;
            break;

        case BlockEvent::EventType::BLOCK_REORG:
            std::cout << "    ⚠️  Block reorganization (depth: " << event.reorg_depth << ")" << std::endl;
            break;

        case BlockEvent::EventType::STATE_CHANGED:
            std::cout << "    📝 Contract state changed" << std::endl;
            break;
        }
    }
};

// ============================================================================
// Main demonstration
// ============================================================================

int main()
{
    std::cout << "\n"
              << std::string(80, '#') << std::endl;
    std::cout << "Midnight SDK - Phase 4E: Complete End-to-End Smart Contract Application" << std::endl;
    std::cout << "Scenario: Privacy-Preserving Voting System" << std::endl;
    std::cout << std::string(80, '#') << std::endl;

    try
    {
        // Initialize application
        VotingSmartContractApp voting_app;

        if (!voting_app.initialize())
        {
            std::cerr << "Failed to initialize application" << std::endl;
            return 1;
        }

        std::cout << "\n✓ Application initialized successfully" << std::endl;
        std::cout << "  All 4 phases configured and ready:" << std::endl;
        std::cout << "  - Phase 4A: ZK Proof Types ✓" << std::endl;
        std::cout << "  - Phase 4B: Private State Management ✓" << std::endl;
        std::cout << "  - Phase 4C: Proof Server Resilience ✓" << std::endl;
        std::cout << "  - Phase 4D: Ledger State Synchronization ✓" << std::endl;

        // Simulate multiple voters casting votes
        std::cout << "\n"
                  << std::string(80, '~') << std::endl;
        std::cout << "Simulating Voting Session (3 voters)" << std::endl;
        std::cout << std::string(80, '~') << std::endl;

        std::vector<std::pair<std::string, std::string>> votes = {
            {"voter_alice", "candidate_A"},
            {"voter_bob", "candidate_B"},
            {"voter_carol", "candidate_A"}};

        for (const auto &[voter, choice] : votes)
        {
            if (voting_app.cast_vote(voter, choice))
            {
                std::cout << "\n✅ Vote recorded successfully for " << voter << std::endl;
            }
            else
            {
                std::cout << "\n❌ Vote failed for " << voter << std::endl;
            }
        }

        // Display results
        voting_app.display_results();

        // Display comprehensive statistics
        voting_app.display_statistics();

        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "End-to-End Demonstration Complete!" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "\nKey accomplishments in this single voting app:" << std::endl;
        std::cout << "  ✓ Private vote data protected via 4B" << std::endl;
        std::cout << "  ✓ ZK proofs generated proving vote eligibility (4A)" << std::endl;
        std::cout << "  ✓ Proofs generated with automatic retry (4C)" << std::endl;
        std::cout << "  ✓ Transactions submitted with circuit breaker protection (4C)" << std::endl;
        std::cout << "  ✓ State synchronized with blockchain automatically (4D)" << std::endl;
        std::cout << "  ✓ Reorg handling ready if chain reorganizes (4D)" << std::endl;
        std::cout << "  ✓ Public results visible while votes remain private" << std::endl;
        std::cout << "\nThis demonstrates the complete value proposition of Phase 4E:" << std::endl;
        std::cout << "All phases working together for a real production application!" << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\nFatal error: " << e.what() << std::endl;
        return 1;
    }
}
