#pragma once

/**
 * @brief Midnight Zero-Knowledge Proof Module
 *
 * This module provides comprehensive support for zero-knowledge proof integration
 * in the Midnight blockchain SDK. It includes:
 *
 * - ZK Proof Types: Data structures for proofs, inputs, witnesses, and transactions
 * - Proof Server Client: HTTP communication with the Proof Server (localhost:6300)
 * - Private State Management: Witness functions and private data handling
 * - Circuit Metadata: Circuit definitions and constraint information
 *
 * @section architecture_overview Architecture Overview
 *
 * Midnight uses zero-knowledge proofs for privacy-preserving smart contracts:
 *
 * ```
 * Circuit Execution (Compact)
 *        ↓
 * Witness Functions (TypeScript)
 *        ↓
 * Proof Generation (Proof Server) → ZK Proof
 *        ↓
 * Proof Verification
 *        ↓
 * Proof Submission (C++ SDK) → Blockchain
 * ```
 *
 * This module handles steps 4-5 in C++.
 *
 * @section usage_example Usage Example
 *
 * ```cpp
 * #include "midnight/zk.hpp"
 * using namespace midnight::zk;
 *
 * // Create Proof Server client
 * ProofServerClient::Config config;
 * config.host = "localhost";
 * config.port = 6300;
 * ProofServerClient client(config);
 *
 * // Connect to Proof Server
 * if (!client.connect()) {
 *     std::cerr << "Proof Server not available" << std::endl;
 *     return;
 * }
 *
 * // Submit ledger-built payload bytes to the proof server
 * auto proven = client.post_prove_tx_payload(prove_tx_payload);
 * ```
 *
 * @section witness_functions Witness Functions
 *
 * Witness functions are implemented in TypeScript/JavaScript and provide
 * private inputs to circuits:
 *
 * ```typescript
 * export const witnesses = {
 *   localSecretKey({ privateState }: WitnessContext): [BBoardPrivateState, Uint8Array] {
 *     return [privateState, privateState.secretKey];
 *   }
 * };
 * ```
 *
 * The C++ SDK receives witness outputs and prepares them for proof generation.
 *
 * @section proof_server Proof Server Integration
 *
 * Proof Server (Docker image: midnightntwrk/proof-server:8.0.3) runs locally:
 *
 * ```bash
 * docker run -p 6300:6300 midnightntwrk/proof-server:8.0.3 midnight-proof-server
 * ```
 *
 * The ProofServerClient communicates via HTTP to the local binary proof API:
 * - Check serialized payloads: POST /check
 * - Generate proof bytes: POST /prove
 * - Prove serialized transactions: POST /prove-tx
 * - Check server health: GET /health
 *
 * The public proof server does not accept ad-hoc JSON /generate requests. Compact
 * circuits must first be converted into ledger-built binary payloads.
 *
 * @section private_vs_public Private State vs Public Ledger
 *
 * Midnight distinguishes between:
 *
 * **Public Ledger (On-Chain)**:
 * - All state visible on blockchain
 * - Commitments instead of raw secrets
 * - Enables proof verification without exposing witness
 *
 * **Private State (Off-Chain)**:
 * - Secret keys and private data
 * - Stored in wallet/application
 * - Never exposed on-chain
 *
 * @section phase_4_architecture Phase 4 Architecture
 *
 * This module (Phase 4) adds to the existing SDK:
 *
 * | Component | Phase | Status |
 * |-----------|-------|--------|
 * | RPC Client | 1 | ✅ |
 * | HTTP Transport | 2 | ✅ |
 * | Ed25519 Signing | 3 | ✅ |
 * | **ZK Proof Types** | **4** | **✅** |
 * | **Proof Server Client** | **4** | **✅** |
 * | **Private State Mgmt** | 5 | Planned |
 * | **Ledger State Mgmt** | 5 | Planned |
 */

#include "midnight/zk/proof_types.hpp"
#include "midnight/zk/proof_server_client.hpp"
#include "midnight/zk/private_state.hpp"
