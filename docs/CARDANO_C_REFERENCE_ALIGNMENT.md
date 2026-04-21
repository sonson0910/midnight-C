# Cardano C Reference Alignment for Midnight SDK

## Intent

This document uses Cardano C as a reference model for API maturity and reliability.
It is not a one-to-one parity checklist.

Midnight and Cardano are different chains with different transaction and state models.
The goal is to borrow good engineering patterns while keeping Midnight-native behavior.

## Chain-Specific Guardrails

- Do not force Cardano-era structures when Midnight uses a different primitive.
- Keep Midnight-first concepts as core: dual state, commitments, proofs, DUST, indexer sync.
- Import only reusable patterns: typed builders, strict serialization, deterministic signing, stable C ABI.

## What Is Already Strong

- Official SDK bridge covers wallet state, transfer, deploy, and indexer diagnostics:
  - include/midnight/blockchain/official_sdk_bridge.hpp:127
  - include/midnight/blockchain/official_sdk_bridge.hpp:136
  - include/midnight/blockchain/official_sdk_bridge.hpp:146
  - include/midnight/blockchain/official_sdk_bridge.hpp:156
- C ABI is present for bridge workflows, including indexer diagnostics:
  - include/midnight/blockchain/official_sdk_bridge.hpp:180
  - include/midnight/blockchain/official_sdk_bridge.hpp:204
  - src/blockchain/official_sdk_bridge.cpp:921
- Sync diagnostics parsing is implemented and tested:
  - src/blockchain/official_sdk_bridge.cpp:618
  - src/blockchain/official_sdk_bridge.cpp:651
  - tests/official_sdk_bridge_test.cpp:201
  - tests/official_sdk_bridge_test.cpp:255

## High-Impact Gaps and Status (Reference-Inspired, Midnight-Native)

### 1) Transaction Core (Resolved in P0, 2026-04-19)

- Canonical CBOR serialization/deserialization and deterministic transaction hashing are now implemented:
  - src/blockchain/transaction.cpp
  - tests/transaction_test.cpp
- Wallet-local signing path still contains placeholder behavior and should be aligned with the production signer path:
  - src/blockchain/wallet.cpp

### 2) UTXO Selection and Fee Logic (Selection Resolved, Fee Model Pending)

- UTXO selection is now deterministic and policy-driven (finality depth, age, and stable tie-breakers):
  - src/utxo-transactions/utxo_transactions.cpp
  - tests/utxo-transactions_test.cpp
- Fee and DUST estimation remain heuristic and should be replaced with chain-calibrated parameters:
  - src/utxo-transactions/utxo_transactions.cpp

### 3) Signing and Key Lifecycle Need Further Hardening

- Phase 5 signing/verification now uses a crypto-backed flow with deterministic fallback where sodium is unavailable.
- Remaining lifecycle hardening gaps:
  - Key load/save flows are still placeholder-oriented:
    - src/signing-submission/signing_submission.cpp
  - Seed import currently relies on deterministic helper derivation rather than a finalized key management pipeline:
    - src/signing-submission/signing_submission.cpp
  - Signature recovery and mempool monitoring still need production-grade implementation:
    - src/signing-submission/signing_submission.cpp

### 4) Adapter Path (Partially Resolved)

- Signed output format now uses a structured envelope (`midnight-signed-v1`) instead of raw concatenation:
  - src/blockchain/midnight_adapter.cpp
- Transaction builder path still returns synthetic transaction identifiers and should converge to canonical transaction bytes:
  - src/blockchain/midnight_adapter.cpp

### 5) Tests and Transport Behavior (Improved, Still Expanding)

- Phase 5 tests now include mock-transport happy-path submission and cache assertions.
- Additional negative/edge transport coverage should continue to expand for production confidence.

## Mapping to Cardano C Style (Reference Only)

The following are pattern imports, not protocol imports:

- Typed transaction body and witness separation:
  - Useful for deterministic build-sign-submit pipelines.
- Explicit binary serialization contract:
  - Replace string-concatenation style signed payloads.
- Coin selection and balancing as first-class components:
  - Keep Midnight-specific DUST and commitment semantics.
- Stable C ABI boundaries:
  - Expand C ABI around mature flows, not around scaffolds.

Potentially out of scope unless Midnight exposes equivalent primitives:

- Byron-style address families.
- Cardano governance and certificate taxonomy.
- Plutus-specific script object model.

## Priority Roadmap

### P0: Production Correctness

- Implement canonical Midnight transaction encoding and deterministic hash.
- Replace placeholder signing with real sr25519 path in Phase 5 signer.
- Replace adapter signed payload concatenation with a typed signed transaction container.
- Upgrade UTXO selection from select-all to a deterministic, policy-driven strategy.

## Execution Status (2026-04-19)

Completed in this pass:

- Replaced fixed mock SR25519 signature output in Phase 5 signer path with crypto-backed signing/verification on the current stack, with deterministic fallback when sodium is unavailable.
- Replaced adapter signed output from raw string concatenation to a structured envelope (`midnight-signed-v1`) while keeping backward-compatible submission payload extraction.
- Updated Phase 5 tests to include mock-transport happy-path submission and submitted-status cache assertions.
- Implemented canonical CBOR-based transaction serialization/deserialization and deterministic transaction hashing in the core transaction module.
- Replaced select-all UTXO selection with deterministic policy-driven selection (finality/age ordering + estimated fee coverage heuristics).
- Added explicit unit coverage for transaction core roundtrip/hash behavior and deterministic UTXO selection policy.
- Introduced typed transaction builder context in Phase 3 (`protocol_params + chain_tip + fee_model`) and wired auto-fee computation to context-aware estimation.
- Upgraded Phase 3 fee and DUST estimators to context-aware, protocol-parameter-driven heuristics with explicit unit coverage.
- Introduced typed witness bundle support in Phase 3 transaction flow (`signature(s) + proof references + metadata`) with backward-compatible validation behavior.
- Separated Phase 3 provider interfaces for Indexer GraphQL and Node RPC, with default provider implementations and injected-provider test coverage.
- Expanded C ABI for stable official bridge operations with wallet-state and compact-deploy entry points.
- Added ABI-focused negative/edge-case tests for buffer truncation safety and error propagation in official bridge C entry points.

Original P0 roadmap items are now completed.

### P1: API Maturity

- Introduce a typed transaction builder context (protocol params + chain tip + fee model). ✅ Completed (2026-04-19)
- Introduce typed witness bundle (signature(s) + proof references + metadata). ✅ Completed (2026-04-19)
- Separate provider interfaces for Node RPC vs Indexer GraphQL. ✅ Completed (2026-04-19)

P1 roadmap items are now completed.

### P2: ABI and Integrations

- Expand C ABI only for stable and tested operations. ✅ Completed (2026-04-19)
- Add negative/edge-case ABI tests for buffer sizing and error propagation. ✅ Completed (2026-04-19)

P2 roadmap items are now completed.

## Suggested First Implementation Slice

1. Replace sr25519 mock sign/verify in Phase 5.
2. Replace adapter sign output format with structured signed payload bytes.
3. Update tests that currently expect failure due placeholder extrinsics to validate one real happy path.

This gives the fastest path from scaffold behavior to end-to-end reliability while preserving Midnight-specific architecture.
