#include <iostream>
#include "midnight/zk.hpp"

using namespace midnight::zk;
using json = nlohmann::json;

/**
 * @brief Phase 4B: Private State Management Examples
 *
 * Demonstrates:
 * 1. SecretKeyStore - Store and retrieve private keys
 * 2. PrivateStateManager - Track on/off-chain state
 * 3. WitnessContextBuilder - Build contexts for witness functions
 * 4. Integration with Phase 4A types
 */

void example_1_secret_key_store()
{
    std::cout << "\n=== Example 1: SecretKeyStore ===" << std::endl;

    // Initialize secret key store
    SecretKeyStore store;

    // Store user's secret key (32 bytes for Ed25519)
    std::vector<uint8_t> secret_key(32, 0xAA);
    store.store_secret("localSecretKey", secret_key);

    std::cout << "✓ Stored secret 'localSecretKey'" << std::endl;
    std::cout << "  Store size: " << store.size() << " secret(s)" << std::endl;

    // Check if secret exists
    if (store.has_secret("localSecretKey"))
    {
        std::cout << "✓ Secret 'localSecretKey' exists" << std::endl;
    }

    // List all secrets
    auto secrets = store.list_secrets();
    std::cout << "✓ Secrets in store:" << std::endl;
    for (const auto &name : secrets)
    {
        std::cout << "  - " << name << std::endl;
    }

    // Retrieve secret
    auto retrieved = store.retrieve_secret("localSecretKey");
    std::cout << "✓ Retrieved secret (size: " << retrieved.size() << " bytes)" << std::endl;

    // Store multiple secrets
    store.store_secret("ephemeralKey", std::vector<uint8_t>(32, 0xBB));
    store.store_secret("encryptionKey", std::vector<uint8_t>(32, 0xCC));

    std::cout << "✓ Added more secrets. Total: " << store.size() << std::endl;

    // Remove a secret
    store.remove_secret("ephemeralKey");
    std::cout << "✓ Removed 'ephemeralKey'. Total now: " << store.size() << std::endl;
}

void example_2_private_state_manager()
{
    std::cout << "\n=== Example 2: PrivateStateManager ===" << std::endl;

    PrivateStateManager manager;

    // Create initial private state for contract
    LedgerState state;
    state.contract_address = "contract_addr_bulletin_board";
    state.block_height = 1;
    state.block_hash = "hash_block_1";

    // Public fields (example for bulletin board)
    state.public_state = json::object();
    state.public_state["state"] = "vacant";
    state.public_state["owner"] = "0000...0000";
    state.public_state["message"] = "none";

    // Private fields (secrets kept off-chain)
    state.private_state = json::object();
    state.private_state["secretKey"] = "aabbccdd...";

    manager.update_private_state(state.contract_address, state);

    std::cout << "✓ Stored private state for contract" << std::endl;
    std::cout << "  Contract: " << state.contract_address << std::endl;
    std::cout << "  Block height: " << state.block_height << std::endl;

    // Check if contract has state
    if (manager.has_state(state.contract_address))
    {
        std::cout << "✓ Contract state exists" << std::endl;
    }

    // Retrieve private state
    auto retrieved = manager.get_private_state(state.contract_address);
    std::cout << "✓ Retrieved state" << std::endl;
    std::cout << "  Public state keys: " << retrieved.public_state.size() << std::endl;

    // Simulate transaction confirmation - update state
    state.block_height = 2;
    state.public_state["state"] = "occupied";
    state.public_state["owner"] = "abcd...efgh";

    manager.sync_state(state.contract_address, state);
    std::cout << "✓ Synced state to block height " << state.block_height << std::endl;

    // List all contracts
    auto contracts = manager.list_contracts();
    std::cout << "✓ Contracts with stored state: " << contracts.size() << std::endl;
}

void example_3_witness_context_builder()
{
    std::cout << "\n=== Example 3: WitnessContextBuilder ===" << std::endl;

    // Build witness context step by step
    WitnessContextBuilder builder;

    // Step 1: Set contract address
    builder.set_contract_address("contract_bulletin_board");
    std::cout << "✓ Set contract address" << std::endl;

    // Step 2: Set public ledger state
    LedgerState public_state;
    public_state.contract_address = "contract_bulletin_board";
    public_state.block_height = 1;
    public_state.public_state = json::object();
    public_state.public_state["state"] = 0; // VACANT

    builder.set_public_state(public_state);
    std::cout << "✓ Set public ledger state" << std::endl;

    // Step 3: Add private secrets
    std::vector<uint8_t> secret_key(32, 0xAA);
    builder.add_private_secret("localSecretKey", secret_key);
    std::cout << "✓ Added private secret 'localSecretKey'" << std::endl;

    // Step 4: Set block height
    builder.set_block_height(1);
    std::cout << "✓ Set block height" << std::endl;

    // Step 5: Build context
    WitnessContext context = builder.build();
    std::cout << "✓ Built witness context" << std::endl;

    // Serialize context for TypeScript bridge
    json context_json = context.to_json();
    std::cout << "✓ Serialized to JSON for TypeScript witness functions" << std::endl;
    std::cout << "  Context size: " << context_json.dump().size() << " bytes" << std::endl;
}

void example_4_witness_context_from_managers()
{
    std::cout << "\n=== Example 4: Building Context from Managers ===" << std::endl;

    // Initialize managers
    SecretKeyStore secret_store;
    PrivateStateManager state_mgr;

    // Populate secret store
    std::vector<uint8_t> secret_key(32, 0xFF);
    secret_store.store_secret("localSecretKey", secret_key);

    std::cout << "✓ Populated secret store with 'localSecretKey'" << std::endl;

    // Populate state manager
    LedgerState state;
    state.contract_address = "contract_addr";
    state.block_height = 1;
    state.public_state = json::object();
    state.public_state["board"] = "vacant";

    state_mgr.update_private_state(state.contract_address, state);
    std::cout << "✓ Populated state manager for contract" << std::endl;

    // Build context from managers (convenience method)
    try
    {
        WitnessContext context = WitnessContextBuilder::build_from_managers(
            "contract_addr",
            state_mgr,
            secret_store,
            {"localSecretKey"} // Required secrets
        );

        std::cout << "✓ Built context from managers" << std::endl;
        std::cout << "  Contract: " << context.contract_address << std::endl;
        std::cout << "  Block height: " << context.block_height << std::endl;
        std::cout << "  Private data count: " << context.private_data.size() << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "✗ Error: " << e.what() << std::endl;
    }
}

void example_5_witness_execution_flow()
{
    std::cout << "\n=== Example 5: Complete Witness Execution Flow ===" << std::endl;

    // Step 1: Prepare managers
    SecretKeyStore secrets;
    PrivateStateManager state_mgr;

    // Step 2: Load user secrets
    std::vector<uint8_t> local_secret_key = private_state_util::create_test_secret();
    secrets.store_secret("localSecretKey", local_secret_key);

    std::cout << "✓ Step 1: Loaded user secrets" << std::endl;

    // Step 3: Get current contract state
    LedgerState current_state;
    current_state.contract_address = "bulletin_board_contract";
    current_state.block_height = 10;
    state_mgr.update_private_state(current_state.contract_address, current_state);

    std::cout << "✓ Step 2: Retrieved contract state (height " << current_state.block_height << ")" << std::endl;

    // Step 4: Build witness context
    WitnessContext witness_ctx = WitnessContextBuilder::build_from_managers(
        current_state.contract_address,
        state_mgr,
        secrets,
        {"localSecretKey"});

    std::cout << "✓ Step 3: Built witness context" << std::endl;

    // Step 5: Serialize for TypeScript execution
    json witness_ctx_json = witness_ctx.to_json();
    std::cout << "✓ Step 4: Serialized witness context to JSON" << std::endl;
    std::cout << "  (This would be sent to TypeScript witness executors)" << std::endl;

    // Step 6: Simulate witness execution (in real scenario, TypeScript runs this)
    std::cout << "✓ Step 5: Witness function executes in TypeScript:" << std::endl;
    std::cout << "  - Receives WitnessContext as input" << std::endl;
    std::cout << "  - Accesses privateState.secretKey" << std::endl;
    std::cout << "  - Accesses ledger.state" << std::endl;
    std::cout << "  - Returns WitnessOutput with proof material" << std::endl;

    // Step 7: Result would contain witness outputs
    WitnessExecutionResult result;
    result.status = WitnessExecutionResult::Status::SUCCESS;
    result.execution_time_ms = 42;

    // Simulate witness output
    WitnessOutput output;
    output.witness_name = "localSecretKey";
    output.output_type = "Bytes<32>";
    output.output_bytes = local_secret_key;

    result.witness_outputs["localSecretKey"] = output;

    std::cout << "✓ Step 6: Received witness execution result" << std::endl;
    std::cout << "  Status: SUCCESS" << std::endl;
    std::cout << "  Execution time: " << result.execution_time_ms << " ms" << std::endl;
    std::cout << "  Witness outputs: " << result.witness_outputs.size() << std::endl;

    // Step 8: Use outputs for proof generation (Phase 4A)
    std::cout << "✓ Step 7: Ready for proof generation (Phase 4A)" << std::endl;
}

void example_6_utility_functions()
{
    std::cout << "\n=== Example 6: Utility Functions ===" << std::endl;

    // Create test context for development
    WitnessContext test_ctx = private_state_util::create_test_context("test_contract");
    std::cout << "✓ Created test witness context" << std::endl;
    std::cout << "  Contract: " << test_ctx.contract_address << std::endl;
    std::cout << "  Block height: " << test_ctx.block_height << std::endl;

    // Create test secret
    auto test_secret = private_state_util::create_test_secret();
    std::cout << "✓ Created test secret (32 bytes)" << std::endl;

    // Validate secret size
    bool valid = private_state_util::validate_secret_size(test_secret, 32);
    std::cout << "✓ Validated secret size: " << (valid ? "VALID" : "INVALID") << std::endl;

    // Hash secret for storage key
    std::string secret_hash = private_state_util::hash_secret(test_secret);
    std::cout << "✓ Hashed secret: " << secret_hash << std::endl;
}

int main()
{
    std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Phase 4B: Private State Management - Usage Examples       ║" << std::endl;
    std::cout << "║  Midnight Blockchain C++ SDK                             ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;

    try
    {
        example_1_secret_key_store();
        example_2_private_state_manager();
        example_3_witness_context_builder();
        example_4_witness_context_from_managers();
        example_5_witness_execution_flow();
        example_6_utility_functions();

        std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  All examples completed successfully! ✓                   ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n✗ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
