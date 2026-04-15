# Midnight SDK v2 - Complete Rewrite Plan

**Based On**: Official Midnight Architecture (docs.midnight.network, Apr 14, 2026)
**Goal**: Production-ready SDK that actually communicates with Midnight on-chain
**Status**: Full Implementation (Phase 1-6 + Tests)

---

## 📋 Complete Architecture (Corrected)

```
┌─────────────────────────────────────────────────────────────────┐
│  Phase 1: Midnight Node Connection (Official)                  │
│  ├─ Connect to sr25519-based AURA consensus                   │
│  ├─ 6-second block time validation                            │
│  ├─ GRANDPA finality tracking                                 │
│  └─ Verify blake2_256 hashing                                 │
└────────────────────┬────────────────────────────────────────────┘

┌────────────────────▼────────────────────────────────────────────┐
│  Phase 2: Compact Contract Interaction                          │
│  ├─ Parse Compact contract ABI                                │
│  ├─ Build contract calls                                      │
│  ├─ Support public state access                              │
│  └─ Support private state via witnesses                      │
└────────────────────┬────────────────────────────────────────────┘

┌────────────────────▼────────────────────────────────────────────┐
│  Phase 3: Transaction Building (UTXO-based)                    │
│  ├─ Query UTXOs with commitments                              │
│  ├─ Build transaction inputs/outputs                          │
│  ├─ Support dual-state updates (public + private)            │
│  └─ Calculate DUST fees                                       │
└────────────────────┬────────────────────────────────────────────┘

┌────────────────────▼────────────────────────────────────────────┐
│  Phase 4: Witness & ZK Proofs                                  │
│  ├─ Build witness context (TypeScript-compatible)            │
│  ├─ Generate zk-SNARKs (128 bytes)                           │
│  ├─ Verify proofs locally                                    │
│  └─ Embed proofs in transactions                             │
└────────────────────┬────────────────────────────────────────────┘

┌────────────────────▼────────────────────────────────────────────┐
│  Phase 5: Signing & Submission                                 │
│  ├─ Sign with sr25519 (AURA/block authorship)                │
│  ├─ Support ed25519 (finality messages)                       │
│  ├─ Submit to Midnight Node                                  │
│  └─ Handle mempool tracking                                  │
└────────────────────┬────────────────────────────────────────────┘

┌────────────────────▼────────────────────────────────────────────┐
│  Phase 6: Monitoring & State Sync                              │
│  ├─ Monitor 6-second blocks                                  │
│  ├─ Track both public & private state                        │
│  ├─ Handle PoS reorgs                                        │
│  └─ Verify finality (GRANDPA)                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🗂️ Complete File Structure

```
midnight-sdk-v2/
│
├── include/
│   ├── midnight/
│   │   ├── core/
│   │   │   ├── config.hpp              (Official network config)
│   │   │   ├── types.hpp               (Core data types)
│   │   │   ├── error.hpp               (Error handling)
│   │   │   └── logger.hpp              (Logging)
│   │   │
│   │   ├── phase1/
│   │   │   ├── node_connection.hpp     (Official node RPC)
│   │   │   ├── consensus.hpp           (AURA + GRANDPA)
│   │   │   └── network.hpp             (Network operations)
│   │   │
│   │   ├── phase2/
│   │   │   ├── compact_contract.hpp    (Compact language interface)
│   │   │   ├── contract_abi.hpp        (ABI parsing)
│   │   │   └── state_query.hpp         (Query public/private state)
│   │   │
│   │   ├── phase3/
│   │   │   ├── transaction.hpp         (UTXO-based transactions)
│   │   │   ├── utxo_manager.hpp        (UTXO handling)
│   │   │   └── fee_calculator.hpp      (DUST fee calculation)
│   │   │
│   │   ├── phase4/
│   │   │   ├── witness_builder.hpp     (Witness context)
│   │   │   ├── zk_proof.hpp            (zk-SNARK proofs)
│   │   │   └── proof_generation.hpp    (Proof system)
│   │   │
│   │   ├── phase5/
│   │   │   ├── signer_sr25519.hpp      (AURA signing)
│   │   │   ├── signer_ed25519.hpp      (Finality signing)
│   │   │   └── submission.hpp          (TX submission)
│   │   │
│   │   └── phase6/
│   │       ├── block_monitor.hpp       (6-second blocks)
│   │       ├── state_sync.hpp          (Dual-state sync)
│   │       ├── reorg_detector.hpp      (PoS reorg handling)
│   │       └── finality_tracker.hpp    (GRANDPA finality)
│   │
│   └── midnight_sdk.hpp                 (Main header)
│
├── src/
│   ├── core/
│   │   ├── config.cpp
│   │   ├── types.cpp
│   │   ├── error.cpp
│   │   └── logger.cpp
│   │
│   ├── phase1/
│   │   ├── node_connection.cpp
│   │   ├── consensus.cpp
│   │   └── network.cpp
│   │
│   ├── phase2/
│   │   ├── compact_contract.cpp
│   │   ├── contract_abi.cpp
│   │   └── state_query.cpp
│   │
│   ├── phase3/
│   │   ├── transaction.cpp
│   │   ├── utxo_manager.cpp
│   │   └── fee_calculator.cpp
│   │
│   ├── phase4/
│   │   ├── witness_builder.cpp
│   │   ├── zk_proof.cpp
│   │   └── proof_generation.cpp
│   │
│   ├── phase5/
│   │   ├── signer_sr25519.cpp
│   │   ├── signer_ed25519.cpp
│   │   └── submission.cpp
│   │
│   └── phase6/
│       ├── block_monitor.cpp
│       ├── state_sync.cpp
│       ├── reorg_detector.cpp
│       └── finality_tracker.cpp
│
├── tests/
│   ├── phase1_test.cpp               (Node connection tests)
│   ├── phase2_test.cpp               (Contract interaction)
│   ├── phase3_test.cpp               (Transaction building)
│   ├── phase4_test.cpp               (Witness & proofs)
│   ├── phase5_test.cpp               (Signing & submission)
│   ├── phase6_test.cpp               (Monitoring & finality)
│   ├── integration_preprod_test.cpp  (Live Preprod tests)
│   └── end_to_end_voting_test.cpp    (Full voting workflow)
│
├── examples/
│   ├── voting_app.cpp                (Voting on Midnight)
│   ├── token_transfer.cpp            (Private transfer)
│   └── compliance_app.cpp            (KYC/compliance)
│
├── CMakeLists.txt                    (Build system)
├── BUILD_INSTRUCTIONS.md              (How to build)
├── TEST_PLAN.md                       (Complete test specifications)
├── INTEGRATION_GUIDE.md               (Preprod integration)
└── DEPLOYMENT.md                      (Production deployment)
```

---

## ✅ Implementation Checklist

### Phase 1: Node Connection (sr25519/AURA)
- [ ] Connect to Midnight Node RPC
- [ ] Verify AURA consensus
- [ ] Track 6-second blocks
- [ ] Validate sr25519 signatures
- [ ] Test with Preprod node

### Phase 2: Compact Contracts
- [ ] Parse Compact contract ABI
- [ ] Build contract interactions
- [ ] Query public state
- [ ] Query private state (via witness)
- [ ] Test with sample contracts

### Phase 3: UTXO Transactions
- [ ] Query UTXOs with commitments
- [ ] Build UTXO inputs
- [ ] Create outputs with commitments
- [ ] Calculate DUST fees
- [ ] Support dual-state updates

### Phase 4: ZK Proofs
- [ ] Build witness contexts
- [ ] Generate zk-SNARKs (128 bytes)
- [ ] Verify proofs
- [ ] Embed in transactions
- [ ] Test proof validation

### Phase 5: Signing & Submission
- [ ] Sign with sr25519
- [ ] Support ed25519 (optional)
- [ ] Submit transactions
- [ ] Track mempool
- [ ] Handle submission errors

### Phase 6: Monitoring
- [ ] Monitor 6-second blocks
- [ ] Track state changes
- [ ] Handle PoS reorgs
- [ ] Track GRANDPA finality
- [ ] Verify state consistency

---

## 🧪 Test Plan Overview

### Unit Tests (Per Phase)
```
Phase 1: 15 tests (connection, consensus, blocks)
Phase 2: 12 tests (contract parsing, state queries)
Phase 3: 18 tests (UTXO building, fees)
Phase 4: 20 tests (witness, proofs)
Phase 5: 15 tests (signing, submission)
Phase 6: 18 tests (monitoring, finality)
Total: 98 unit tests
```

### Integration Tests
```
- Connect to Preprod node
- Query actual UTXOs
- Submit real transactions
- Monitor real blocks
- Verify state updates
- Test reorg handling
```

### End-to-End Tests
```
- Full voting workflow (local → Preprod)
- Private transfer (with proofs)
- State synchronization (public + private)
- Reorg recovery
- Finality verification
```

---

## 🚀 Implementation Order

**Week 1: Core Infrastructure**
1. Create config system (Preprod endpoints)
2. Implement Phase 1 (node connection)
3. Write Phase 1 tests
4. Test with actual Preprod node

**Week 2: Contract & Transactions**
5. Implement Phase 2 (Compact interface)
6. Implement Phase 3 (UTXO building)
7. Write Phase 2-3 tests
8. Test transaction building

**Week 3: Proofs & Signing**
9. Implement Phase 4 (witness & proofs)
10. Implement Phase 5 (signing)
11. Write Phase 4-5 tests
12. Test proof generation

**Week 4: Monitoring & Integration**
13. Implement Phase 6 (monitoring)
14. Write Phase 6 tests
15. Full integration tests
16. End-to-end voting test on Preprod

---

## 🎯 Success Criteria

### Minimum Viable (MVP)
- ✅ Connect to Midnight Preprod
- ✅ Submit transaction to Preprod
- ✅ Monitor confirmation on Preprod
- ✅ All unit tests pass
- ✅ All integration tests pass

### Production Ready
- ✅ Handle all error cases
- ✅ Reorg detection & recovery
- ✅ Finality verification
- ✅ Comprehensive logging
- ✅ Performance optimization
- ✅ Security hardening

---

## 📊 Timeline

| Phase | Duration | Status |
|-------|----------|--------|
| Planning | Done | ✅ |
| Core (Phase 1) | 3 days | ⏳ |
| Contracts (Phase 2) | 3 days | ⏳ |
| Transactions (Phase 3) | 3 days | ⏳ |
| Proofs (Phase 4) | 3 days | ⏳ |
| Signing (Phase 5) | 2 days | ⏳ |
| Monitoring (Phase 6) | 3 days | ⏳ |
| Testing | 5 days | ⏳ |
| **Total** | **22 days** | ⏳ |

---

## 💾 Data Files to Create

1. ✅ **MidnightPreprodConfig.hpp** - Official endpoints & parameters
2. ✅ **Types.hpp** - Core data structures (UTXO, TX, etc.)
3. ✅ **Phase1-6 Headers** - Component interfaces
4. ✅ **Phase1-6 Implementation** - Actual logic
5. ✅ **Test Files** - Unit + integration tests
6. ✅ **CMakeLists.txt** - Build system
7. ✅ **Documentation** - Build, test, deploy guides

---

## 🔗 Integration Points

### Midnight Preprod (Official)
```
Node RPC: (To be discovered during Phase 1)
Faucet: https://faucet.preprod.midnight.network/
Consensus: AURA + GRANDPA
Blocks: 6 seconds
Signing: sr25519 (AURA), ed25519 (finality)
Hashing: blake2_256
Proofs: zk-SNARKs (128 bytes)
```

### External Dependencies
```
OpenSSL: For sr25519 & ed25519
cURL: For HTTP(S) requests
JSON: For parsing responses
SQLite: For local proof caching (optional)
```

---

## 📝 Next Steps

1. Create config file with official parameters
2. Create core types & structures
3. Build Phase 1 (node connection)
4. Test against Preprod
5. Continue with Phases 2-6
6. Integration testing
7. End-to-end voting on Preprod

**Ready to start implementation?** ✨
