#include <iostream>
#include "midnight/zk.hpp"
#include "nlohmann/json.hpp"

using namespace midnight::zk;
using json = nlohmann::json;

/**
 * @brief Example demonstrating Phase 4A: ZK Proof Types
 *
 * This example shows:
 * 1. Creating ZK proof types
 * 2. Serializing to JSON
 * 3. Connecting to Proof Server
 * 4. Preparing proof-enabled transactions
 *
 * To run this example:
 * 1. Start Proof Server: docker run -p 6300:6300 midnightntwrk/proof-server:8.0.3
 * 2. Build: cmake --build . --config Release
 * 3. Run: ./phase4a_zk_types_example
 */

void example_1_create_proof_types()
{
    std::cout << "\n=== Example 1: Creating ZK Proof Types ===" << std::endl;

    // Create public inputs (disclosed values on-chain)
    PublicInputs inputs;
    inputs.add_input("state", "0000000000000000000000000000000000000000000000000000000000000000");
    inputs.add_input("owner", "2481c4b47d0afe21d5f4fb8f14bc81e9b945b65dc988eb2cef0223a2cc6063f5");
    inputs.add_input("message", "48656c6c6f20576f726c64"); // "Hello World" in hex

    std::cout << "✓ Created public inputs with " << inputs.inputs.size() << " fields" << std::endl;

    // Create witness output (stays private)
    WitnessOutput witness;
    witness.witness_name = "localSecretKey";
    witness.output_type = "Bytes<32>";
    witness.output_bytes = std::vector<uint8_t>(32, 0xAA); // Dummy secret key

    std::cout << "✓ Created witness '" << witness.witness_name << "' of type " << witness.output_type << std::endl;

    // Create proof metadata
    CircuitProofMetadata metadata;
    metadata.circuit_name = "post";
    metadata.circuit_hash = "abc123def456";
    metadata.num_constraints = 4569;
    metadata.compilation_version = "0.22.0";

    std::cout << "✓ Created circuit metadata for circuit '" << metadata.circuit_name << "'" << std::endl;
}

void example_2_serialize_to_json()
{
    std::cout << "\n=== Example 2: JSON Serialization ===" << std::endl;

    // Create a complete CircuitProof
    CircuitProof proof;

    // Set proof data
    ProofData pd;
    pd.proof_bytes.resize(128, 0xAA);
    proof.proof = pd;

    // Set public inputs
    proof.public_inputs.add_input("state", "0000000000000000000000000000000000000000000000000000000000000001");
    proof.public_inputs.add_input("owner", "1111111111111111111111111111111111111111111111111111111111111111");

    // Set metadata
    proof.metadata.circuit_name = "takeDown";
    proof.metadata.circuit_hash = "hash_takedown";
    proof.metadata.num_constraints = 4580;
    proof.metadata.compilation_version = "0.22.0";

    // Set timestamps
    proof.generated_at_timestamp = std::time(nullptr) * 1000;
    proof.proof_server_endpoint = "http://localhost:6300";
    proof.verification_status = CircuitProof::VerificationStatus::UNVERIFIED;

    // Serialize to JSON
    json proof_json = proof.to_json();

    std::cout << "✓ Serialized CircuitProof to JSON" << std::endl;
    std::cout << "  Proof size: " << proof.proof.size() << " bytes" << std::endl;
    std::cout << "  Public inputs: " << proof.public_inputs.inputs.size() << " fields" << std::endl;
    std::cout << "  JSON output (first 200 chars):" << std::endl;
    std::string json_str = proof_json.dump();
    std::cout << "  " << json_str.substr(0, 200) << "..." << std::endl;

    // Deserialize from JSON (round-trip test)
    CircuitProof proof2 = CircuitProof::from_json(proof_json);
    std::cout << "✓ Deserialized back from JSON (round-trip successful)" << std::endl;
    std::cout << "  Circuit name matches: " << (proof2.metadata.circuit_name == "takeDown" ? "YES" : "NO") << std::endl;
}

void example_3_proof_server_connection()
{
    std::cout << "\n=== Example 3: Proof Server Connection ===" << std::endl;

    // Create Proof Server client with custom configuration
    ProofServerClient::Config config;
    config.host = "localhost";
    config.port = 6300;
    config.timeout_ms = std::chrono::milliseconds(5000);
    config.use_https = false;

    ProofServerClient client(config);

    std::cout << "✓ Proof Server URL: " << config.base_url() << std::endl;

    // Attempt connection
    if (client.connect())
    {
        std::cout << "✓ Successfully connected to Proof Server!" << std::endl;

        // Get server status
        json status = client.get_server_status();
        std::cout << "✓ Server status: " << status.dump() << std::endl;

        // Try to get circuit metadata
        try
        {
            CircuitProofMetadata metadata = client.get_circuit_metadata("post");
            std::cout << "✓ Retrieved metadata for circuit 'post'" << std::endl;
            std::cout << "  Constraints: " << metadata.num_constraints << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cout << "⚠ Circuit metadata not available: " << e.what() << std::endl;
        }
    }
    else
    {
        std::cout << "⚠ Proof Server not available (this is expected if Docker not running)" << std::endl;
        std::cout << "  To enable Proof Server:" << std::endl;
        std::cout << "  docker run -p 6300:6300 midnightntwrk/proof-server:8.0.3 midnight-proof-server" << std::endl;

        // Demo with test proof instead
        std::cout << "\n  Creating test proof instead..." << std::endl;
        CircuitProof test_proof = client.create_test_proof("post");
        std::cout << "✓ Created test proof for circuit 'post'" << std::endl;
    }
}

void example_4_create_proof_enabled_transaction()
{
    std::cout << "\n=== Example 4: Creating Proof-Enabled Transaction ===" << std::endl;

    // Create a proof-enabled transaction (ready for blockchain submission)
    ProofEnabledTransaction tx;

    // Set transaction identifiers
    tx.transaction_id = "tx_001_bulletin_board_post";
    tx.transaction_hex = "0123456789abcdef"; // Dummy transaction data

    // Attach ZK proof
    CircuitProof circuit_proof;
    ProofData pd;
    pd.proof_bytes.resize(256, 0xBB);
    circuit_proof.proof = pd;
    circuit_proof.public_inputs.add_input("board_state", "0000000000000000000000000000000000000000000000000000000000000000");
    circuit_proof.metadata.circuit_name = "post";
    circuit_proof.generated_at_timestamp = std::time(nullptr) * 1000;
    circuit_proof.proof_server_endpoint = "http://localhost:6300";

    tx.circuit_proof = circuit_proof;

    // Optional: Add Ed25519 signature (from Phase 3)
    tx.ed25519_signature = "ed25519_sig_hex_here";
    tx.public_key = "ed25519_pubkey_hex_here";

    // Add expected ledger updates
    tx.ledger_updates["state"] = "1"; // Board now occupied
    tx.ledger_updates["owner"] = "...owner_commitment...";
    tx.ledger_updates["sequence"] = "1";

    std::cout << "✓ Created ProofEnabledTransaction" << std::endl;
    std::cout << "  Transaction ID: " << tx.transaction_id << std::endl;
    std::cout << "  Circuit: " << tx.circuit_proof.metadata.circuit_name << std::endl;
    std::cout << "  Ledger updates: " << tx.ledger_updates.size() << " fields" << std::endl;

    // Serialize for blockchain submission
    json tx_json = tx.to_json();
    std::cout << "✓ Serialized for blockchain submission" << std::endl;
    std::cout << "  JSON size: " << tx_json.dump().size() << " bytes" << std::endl;
}

void example_5_proof_validation()
{
    std::cout << "\n=== Example 5: Proof Validation ===" << std::endl;

    // Test proof size validation
    std::cout << "Proof size validation:" << std::endl;
    std::cout << "  64 bytes (min): " << (types_util::is_valid_proof_size(64) ? "✓ VALID" : "✗ INVALID") << std::endl;
    std::cout << "  128 bytes: " << (types_util::is_valid_proof_size(128) ? "✓ VALID" : "✗ INVALID") << std::endl;
    std::cout << "  4096 bytes: " << (types_util::is_valid_proof_size(4096) ? "✓ VALID" : "✗ INVALID") << std::endl;
    std::cout << "  50 bytes (too small): " << (types_util::is_valid_proof_size(50) ? "✓ VALID" : "✗ INVALID") << std::endl;
    std::cout << "  10 MB (too large): " << (types_util::is_valid_proof_size(10 * 1024 * 1024) ? "✓ VALID" : "✗ INVALID") << std::endl;

    // Test public inputs validation
    std::cout << "\nPublic inputs validation:" << std::endl;

    PublicInputs valid_inputs;
    valid_inputs.add_input("state", "abcd1234");
    std::cout << "  Non-empty inputs: " << (types_util::is_valid_public_inputs(valid_inputs) ? "✓ VALID" : "✗ INVALID") << std::endl;

    PublicInputs empty_inputs;
    std::cout << "  Empty inputs: " << (types_util::is_valid_public_inputs(empty_inputs) ? "✓ VALID" : "✗ INVALID") << std::endl;

    // Test hex validation
    PublicInputs invalid_hex;
    invalid_hex.add_input("bad", "not_hex_xyz"); // Invalid hex characters
    std::cout << "  Invalid hex: " << (types_util::is_valid_public_inputs(invalid_hex) ? "✓ VALID" : "✗ INVALID") << std::endl;
}

void example_6_witness_output_handling()
{
    std::cout << "\n=== Example 6: Witness Output Handling ===" << std::endl;

    // Demonstrate witness output creation and serialization

    // Scenario: Generate witness from TypeScript function
    // TypeScript: localSecretKey() => returns 32-byte secret
    // C++ receives this as hex string

    std::string witness_hex = "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899";

    WitnessOutput witness = WitnessOutput::from_hex(witness_hex);
    witness.witness_name = "localSecretKey";
    witness.output_type = "Bytes<32>";

    std::cout << "✓ Created witness from hex string" << std::endl;
    std::cout << "  Name: " << witness.witness_name << std::endl;
    std::cout << "  Type: " << witness.output_type << std::endl;
    std::cout << "  Size: " << witness.output_bytes.size() << " bytes" << std::endl;
    std::cout << "  Hex: " << witness.to_hex() << std::endl;

    // Serialize witness for Proof Server
    json witness_json = witness.to_json();
    std::cout << "✓ Serialized witness to JSON:" << std::endl;
    std::cout << "  " << witness_json.dump() << std::endl;
}

int main()
{
    std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Phase 4A: ZK Proof Types - Usage Examples                ║" << std::endl;
    std::cout << "║  Midnight Blockchain C++ SDK                             ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;

    try
    {
        example_1_create_proof_types();
        example_2_serialize_to_json();
        example_3_proof_server_connection();
        example_4_create_proof_enabled_transaction();
        example_5_proof_validation();
        example_6_witness_output_handling();

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
