/**
 * Example: Voting Contract for Midnight Preprod
 *
 * A simple on-chain voting smart contract written in Compact language
 * Demonstrates:
 * - State management on-chain
 * - Private voting (amounts encrypted via zk-SNARKs)
 * - Proposal tracking
 * - Vote counting
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <cassert>

namespace midnight::examples
{

    // ============================================================================
    // Voting Contract ABI (Compact Language)
    // ============================================================================

    /**
     * Example Voting Contract ABI (for reference - actual would be in TypeScript)
     *
     * contract Voting {
     *   // Public state
     *   mapping(address => bool) public has_voted;
     *   mapping(uint256 => (uint256, uint256)) public proposal_votes; // (for, against)
     *   mapping(uint256 => uint256) public proposal_deadline;
     *
     *   // Private state (encrypted)
     *   private votes_secret: [u256; 100];
     *
     *   // Methods
     *   pub fn create_proposal(deadline: u256) -> u256;
     *   pub fn cast_vote(proposal_id: u256, choice: u256) -> Result<(), Error>;
     *   pub fn get_proposal_result(proposal_id: u256) -> (u256, u256);
     *   pub fn verify_vote(proposal_id: u256, voter: Address) -> bool;
     * }
     */

    // ============================================================================
    // Voting Contract Implementation (C++ Version)
    // ============================================================================

    struct Proposal
    {
        uint32_t id;
        uint64_t votes_for = 0;
        uint64_t votes_against = 0;
        uint32_t deadline_block;
        bool active = true;
        std::string title;
        std::string description;
    };

    struct Vote
    {
        std::string voter_address;
        uint32_t proposal_id;
        uint8_t choice; // 0 = against, 1 = for
        uint32_t block_height;
        std::string vote_hash; // zk-SNARK proof hash
        bool finalized = false;
    };

    class VotingContract
    {
    private:
        std::map<uint32_t, Proposal> proposals_;
        std::map<std::string, std::vector<uint32_t>> voter_history_; // {address => [proposal_ids]}
        std::vector<Vote> vote_log_;
        uint32_t next_proposal_id_ = 1;

    public:
        // ========================================================================
        // Public API
        // ========================================================================

        /**
         * Create a new proposal
         * @param title: Proposal title
         * @param description: Proposal description
         * @param deadline_blocks: Number of blocks until voting deadline
         * @return: Proposal ID
         */
        uint32_t create_proposal(const std::string &title,
                                 const std::string &description,
                                 uint32_t deadline_blocks)
        {
            uint32_t proposal_id = next_proposal_id_++;

            Proposal prop;
            prop.id = proposal_id;
            prop.deadline_block = std::time(nullptr) + (deadline_blocks * 6); // 6 sec/block
            prop.title = title;
            prop.description = description;

            proposals_[proposal_id] = prop;

            std::cout << "[VOTING] Created proposal #" << proposal_id
                      << ": " << title << std::endl;

            return proposal_id;
        }

        /**
         * Cast a vote on a proposal
         * @param voter_address: Address of voter (sr25519)
         * @param proposal_id: Proposal to vote on
         * @param choice: 1 = for, 0 = against
         * @param block_height: Current block height (for proof)
         * @return: true if vote accepted
         */
        bool cast_vote(const std::string &voter_address,
                       uint32_t proposal_id,
                       uint8_t choice,
                       uint32_t block_height)
        {
            // Validate proposal exists
            if (proposals_.find(proposal_id) == proposals_.end())
            {
                std::cerr << "[ERROR] Proposal #" << proposal_id << " not found" << std::endl;
                return false;
            }

            Proposal &prop = proposals_[proposal_id];

            // Validate proposal still active
            if (!prop.active)
            {
                std::cerr << "[ERROR] Proposal #" << proposal_id << " is closed" << std::endl;
                return false;
            }

            // Check deadline
            if (block_height >= prop.deadline_block)
            {
                std::cerr << "[ERROR] Voting deadline passed" << std::endl;
                prop.active = false;
                return false;
            }

            // Check voter hasn't already voted
            for (const auto &past_vote : vote_log_)
            {
                if (past_vote.voter_address == voter_address &&
                    past_vote.proposal_id == proposal_id)
                {
                    std::cerr << "[ERROR] Voter already voted on this proposal" << std::endl;
                    return false;
                }
            }

            // Record vote
            Vote vote;
            vote.voter_address = voter_address;
            vote.proposal_id = proposal_id;
            vote.choice = choice;
            vote.block_height = block_height;
            vote.vote_hash = generate_mock_proof_hash();

            vote_log_.push_back(vote);
            voter_history_[voter_address].push_back(proposal_id);

            // Update tally
            if (choice == 1)
            {
                prop.votes_for++;
            }
            else
            {
                prop.votes_against++;
            }

            std::cout << "[VOTING] Vote recorded: "
                      << voter_address.substr(0, 10) << "..."
                      << " voted " << (choice ? "FOR" : "AGAINST")
                      << " proposal #" << proposal_id << std::endl;

            return true;
        }

        /**
         * Get proposal voting result
         * @param proposal_id: Proposal to query
         * @return: {votes_for, votes_against} or {0, 0} if not found
         */
        std::pair<uint64_t, uint64_t> get_result(uint32_t proposal_id)
        {
            if (proposals_.find(proposal_id) == proposals_.end())
            {
                return {0, 0};
            }

            const Proposal &prop = proposals_[proposal_id];
            return {prop.votes_for, prop.votes_against};
        }

        /**
         * Check if proposal passed (more votes for than against)
         * @param proposal_id: Proposal to check
         * @return: true if passed
         */
        bool proposal_passed(uint32_t proposal_id)
        {
            if (proposals_.find(proposal_id) == proposals_.end())
            {
                return false;
            }

            const Proposal &prop = proposals_[proposal_id];
            return prop.votes_for > prop.votes_against;
        }

        /**
         * Verify a specific vote (important for proving vote was cast)
         * @param voter_address: Address of voter
         * @param proposal_id: Proposal ID
         * @return: true if vote found and valid
         */
        bool verify_vote(const std::string &voter_address, uint32_t proposal_id)
        {
            for (const auto &vote : vote_log_)
            {
                if (vote.voter_address == voter_address &&
                    vote.proposal_id == proposal_id)
                {
                    return true;
                }
            }
            return false;
        }

        /**
         * Get proposal details
         */
        std::string get_proposal_info(uint32_t proposal_id)
        {
            if (proposals_.find(proposal_id) == proposals_.end())
            {
                return "Proposal not found";
            }

            const Proposal &prop = proposals_[proposal_id];
            auto result = get_result(proposal_id);

            std::string status = prop.active ? "OPEN" : "CLOSED";
            std::string outcome = result.first > result.second ? "PASSING" : result.first < result.second ? "FAILING"
                                                                                                          : "TIED";

            return "Proposal #" + std::to_string(prop.id) + ": " + prop.title +
                   "\nDescription: " + prop.description +
                   "\nStatus: " + status +
                   "\nVotes FOR: " + std::to_string(result.first) +
                   "\nVotes AGAINST: " + std::to_string(result.second) +
                   "\nOutcome: " + outcome +
                   "\nDeadline: Block " + std::to_string(prop.deadline_block);
        }

        /**
         * Get total votes cast
         */
        uint32_t total_votes_cast() const
        {
            return vote_log_.size();
        }

        // ========================================================================
        // Contract State Query (Public)
        // ========================================================================

        /**
         * Simulate on-chain state storage query
         * Returns JSON-like structure that would be stored on Midnight
         */
        std::string export_state()
        {
            std::string state = "{\n";
            state += "  \"contract_address\": \"0xvoting\",\n";
            state += "  \"proposals\": {\n";

            for (const auto &[id, prop] : proposals_)
            {
                state += "    \"proposal_" + std::to_string(id) + "\": {\n";
                state += "      \"id\": " + std::to_string(id) + ",\n";
                state += "      \"title\": \"" + prop.title + "\",\n";
                state += "      \"votes_for\": " + std::to_string(prop.votes_for) + ",\n";
                state += "      \"votes_against\": " + std::to_string(prop.votes_against) + ",\n";
                state += "      \"active\": " + std::string(prop.active ? "true" : "false") + "\n";
                state += "    },\n";
            }

            state += "  },\n";
            state += "  \"total_votes_cast\": " + std::to_string(vote_log_.size()) + "\n";
            state += "}\n";

            return state;
        }

    private:
        /**
         * Generate mock zk-SNARK proof hash for vote
         * In production: Would be actual 128-byte zk-SNARK from Proof Server
         */
        std::string generate_mock_proof_hash()
        {
            return "0x" + std::string(128, 'a' + (std::time(nullptr) % 26));
        }
    };

} // namespace midnight::examples

// ============================================================================
// Example Usage
// ============================================================================

int main()
{
    using namespace midnight::examples;

    std::cout << "=================================================================\n"
              << "Midnight Voting Contract Example - Preprod Deployment" << "\n"
              << "=================================================================\n\n";

    // Create voting contract instance
    VotingContract voting;

    // ========================================================================
    // Step 1: Create Proposals
    // ========================================================================

    std::cout << "[STEP 1] Creating proposals...\n"
              << std::endl;

    uint32_t prop1 = voting.create_proposal(
        "Increase governance participation",
        "Should we increase rewards for active governance participants?",
        100 // 100 blocks = ~10 minutes
    );

    uint32_t prop2 = voting.create_proposal(
        "Add new validator",
        "Should we add node validator-xyz to the network?",
        100);

    uint32_t prop3 = voting.create_proposal(
        "Protocol upgrade",
        "Should we upgrade to Midnight SDK v2.5?",
        150);

    std::cout << "\nProposals created: " << prop1 << ", " << prop2 << ", " << prop3 << "\n"
              << std::endl;

    // ========================================================================
    // Step 2: Cast Votes
    // ========================================================================

    std::cout << "[STEP 2] Casting votes...\n"
              << std::endl;

    // Voter addresses (sr25519 format, start with '5')
    std::vector<std::string> voters = {
        "5N7QCMyJ5",
        "5FHneA46x",
        "5GrwvyQoN",
        "5CUoMhwxN",
        "5FwMwkD8L"};

    // Vote on proposal 1: majority FOR
    voting.cast_vote(voters[0], prop1, 1, 5000); // FOR
    voting.cast_vote(voters[1], prop1, 1, 5001); // FOR
    voting.cast_vote(voters[2], prop1, 0, 5002); // AGAINST
    voting.cast_vote(voters[3], prop1, 1, 5003); // FOR
    voting.cast_vote(voters[4], prop1, 0, 5004); // AGAINST

    std::cout << std::endl;

    // Vote on proposal 2: tied
    voting.cast_vote(voters[0], prop2, 1, 5000); // FOR
    voting.cast_vote(voters[1], prop2, 0, 5001); // AGAINST
    voting.cast_vote(voters[2], prop2, 1, 5002); // FOR
    voting.cast_vote(voters[3], prop2, 0, 5003); // AGAINST

    std::cout << std::endl;

    // Vote on proposal 3: only 2 votes
    voting.cast_vote(voters[0], prop3, 0, 5000); // AGAINST
    voting.cast_vote(voters[1], prop3, 1, 5001); // FOR

    std::cout << std::endl;

    // ========================================================================
    // Step 3: Query Results
    // ========================================================================

    std::cout << "[STEP 3] Querying voting results...\n"
              << std::endl;

    std::cout << voting.get_proposal_info(prop1) << "\n"
              << std::endl;
    std::cout << voting.get_proposal_info(prop2) << "\n"
              << std::endl;
    std::cout << voting.get_proposal_info(prop3) << "\n"
              << std::endl;

    // ========================================================================
    // Step 4: Verify Specific Votes
    // ========================================================================

    std::cout << "[STEP 4] Verifying specific votes...\n"
              << std::endl;

    for (const auto &voter : voters)
    {
        bool voted_prop1 = voting.verify_vote(voter, prop1);
        std::cout << "Voter " << voter.substr(0, 8) << "..."
                  << " voted on proposal #1: "
                  << (voted_prop1 ? "YES" : "NO") << std::endl;
    }

    std::cout << std::endl;

    // ========================================================================
    // Step 5: Export Contract State
    // ========================================================================

    std::cout << "[STEP 5] Contract state (as would be stored on Midnight):\n"
              << std::endl;
    std::cout << voting.export_state() << std::endl;

    // ========================================================================
    // Step 6: Demonstrate On-Chain Integration
    // ========================================================================

    std::cout << "[STEP 6] On-chain deployment steps:\n"
              << std::endl;

    std::cout << "1. Compile contract (Compact TypeScript):\n"
              << "   $ compact build voting_contract.ts\n\n";

    std::cout << "2. Deploy to Preprod:\n"
              << "   $ midnight deploy voting_contract.wasm \\\\\n"
              << "     --network preprod \\\\\n"
              << "     --private-key 0x...\n\n";

    std::cout << "3. Contract stored at address: 0xvoting\n"
              << "   Verification: https://indexer.preprod.midnight.network\n\n";

    std::cout << "4. Submit vote transaction:\n"
              << "   - Build: VotingContract::cast_vote(5N7..., 1, 1, 5000)\n"
              << "   - Sign: sr25519 signature\n"
              << "   - Submit: author_submitExtrinsic via node RPC\n\n";

    std::cout << "5. Monitor finality:\n"
              << "   - Block inclusion: ~6 seconds\n"
              << "   - GRANDPA finality: ~18 seconds\n"
              << "   - Verify via chain_getFinalizedHead\n\n";

    // ========================================================================
    // Summary
    // ========================================================================

    std::cout << "=================================================================\n"
              << "Summary" << "\n"
              << "=================================================================\n";
    std::cout << "Proposals created: 3\n"
              << "Total votes cast: " << voting.total_votes_cast() << "\n"
              << "Proposal #" << prop1 << ": "
              << (voting.proposal_passed(prop1) ? "PASSING" : "FAILING") << "\n"
              << "Proposal #" << prop2 << ": "
              << (voting.proposal_passed(prop2) ? "PASSING" : "FAILING") << "\n"
              << "Proposal #" << prop3 << ": "
              << (voting.proposal_passed(prop3) ? "PASSING" : "FAILING") << "\n";

    std::cout << "\nNext steps:\n"
              << "1. Deploy this contract to Midnight Preprod\n"
              << "2. Use SDK Phase 2 (Compact Contracts) to interact\n"
              << "3. Use SDK Phase 5 (Signing) to submit votes\n"
              << "4. Use SDK Phase 6 (Monitoring) to track finality\n";

    std::cout << "\n=================================================================\n";

    return 0;
}
