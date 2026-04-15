# Midnight Official Research - From Official Documentation & Sources

**Date**: April 15, 2026
**Source**: midnight.network, docs.midnight.network, official GitHub
**Last Updated**: From official docs (Apr 14, 2026)

---

## 📌 Official Midnight Architecture

### What Midnight Actually Is (From Official Docs)

```
Midnight = Fourth-Generation Blockchain ≠ "Cardano + Privacy"

Official Features:
✅ Programmable Privacy
✅ Selective Disclosure
✅ Zero-Knowledge Proofs (zk-SNARKs)
✅ Hybrid Model (transparent + confidential)
✅ Built on Polkadot SDK
✅ Compact programming language (TypeScript-based)
```

**Official Quote:**
> "Midnight is a data protection blockchain platform that addresses a fundamental challenge:
> how to use distributed ledgers while maintaining privacy required for sensitive data."

---

## 🏗️ Official Network Architecture

### Core Components (From docs.midnight.network/nodes)

| Parameter | Value | Source |
|-----------|-------|--------|
| **Block Time** | 6 seconds | Official (Core Parameters) |
| **Session Length** | 1200 slots | Official |
| **Hash Function** | blake2_256 | Official |
| **Account Type** | sr25519 public key | Official |
| **Consensus** | AURA + GRANDPA | Official (Polkadot SDK) |
| **Initial Validators** | 12 permissioned + community | Official (Genesis Config) |

### Signature Schemes (Official - from Cryptography docs)
```
ECDSA        → Partnerchain consensus message signing
ed25519      → Finality-related message signing
sr25519      → AURA block authorship signing (Schnorrkel/Ristretto/x25519)
```

---

## 🔐 Official Privacy Model (The Critical Part)

### Two-State System (From official docs)

**Official Architecture:**

```
┌──────────────────────────────────┐
│  PUBLIC STATE (On-chain)         │
│  - Transaction proofs            │
│  - Contract code                 │
│  - Commitments (hash values)     │
│  - Intentionally public data     │
└──────────────┬───────────────────┘

┌──────────────▼───────────────────┐
│  Zero-Knowledge Proofs           │
│  (Bridge between states)         │
│                                  │
│  Verify WITHOUT revealing data   │
│  zk-SNARKs: 128 bytes size       │
│  Verify in milliseconds          │
└──────────────┬───────────────────┘

┌──────────────▼───────────────────────────┐
│  PRIVATE STATE (Local, encrypted)       │
│  - User's sensitive data                │
│  - Personal information                 │
│  - Business secrets                     │
│  - Never exposed to network             │
└────────────────────────────────────────┘
```

**Official Quote:**
> "Using zk-SNARKs, Midnight can verify computations without seeing input data,
> prove statements true without revealing why, generate compact proofs (128 bytes)
> regardless of computation complexity, and validate proofs in milliseconds on-chain."

---

## 📝 Official Transaction Flow (From docs)

### Official Process (From What is Midnight):

```
1. COMPUTE (Local, Private)
   └─ User performs computation on private data
   └─ Data NEVER leaves user's device
   └─ Generate proof claims

2. PROVE (Proof System)
   └─ Generate ZK proof of computation
   └─ Mathematical evidence without revealing inputs
   └─ Compact proof (~128 bytes)

3. SUBMIT (Blockchain)
   └─ Submit proof + public outputs to blockchain
   └─ Private data stays local

4. VERIFY (Validators)
   └─ Validators verify proof using zk-SNARK verification
   └─ Takes milliseconds despite complexity
   └─ Both public & private states update
      - Public state: on-chain
      - Private state: local storage

5. RESULT
   └─ Transaction confirmed
   └─ Privacy maintained
```

---

## 💻 Official Compact Language

### What is Compact (From official docs)

**Official Definition:**
> "Midnight introduces Compact, a domain-specific language based on TypeScript
> that makes privacy-preserving smart contracts accessible to mainstream developers.
> Instead of requiring cryptographic expertise, developers write familiar code
> that automatically compiles to zero-knowledge circuits."

**Key Points (Official):**
- Based on TypeScript syntax
- Automatically compiles to ZK circuits
- NOT Solidity, NOT Plutus, NOT Rust
- Only way to write Midnight smart contracts
- Removes need for cryptographic expertise

### Compact Code Example (From official docs)
```compact
// Officially supported syntax pattern
contract MyContract {
    public amount: u64;              // Visible on-chain

    private secret: field;            // Hidden locally

    // Public function
    public function getAmount() -> u64 {
        return amount;
    }

    // Witness function (private computation)
    witness privateLogic() -> Result<(), String> {
        // Private computation happens here
        // Uses ZK proofs internally
    }
}
```

---

## 🌐 Official Preprod Network Configuration

### From Official Developer Hub

**RPC/Node Endpoints:**
```
Preprod Node: (Specific endpoint not in public docs)
Preprod Indexer: (To be confirmed from API docs)
```

**Faucet (Official):**
```
URL: https://faucet.preprod.midnight.network/
Test Token: tNIGHT (testnet coins only)
Purpose: Testing transactions on Preprod
Requirement: Must delegate tNIGHT to generate DUST
```

**Official Resources:**
- Developer Hub: https://midnight.network/developer-hub
- Preprod Faucet: https://faucet.preprod.midnight.network/
- Documentation: https://docs.midnight.network/
- GitHub: https://github.com/midnightntwrk

---

## 📚 Official Repositories

### Open-Source Projects (From Developer Hub)

1. **midnight-protocol** - Core protocol
2. **midnight-awesome-dapps** - Example applications
3. **midnight-zk** - Proof system
4. **midnight-js** - TypeScript SDK
5. **midnight-node-docker** - Docker node setup
6. **midnight-docs** - Official documentation
7. **midnight-indexer** - Indexing service

### All at: https://github.com/midnightntwrk

---

## 🎯 Official Use Cases (From docs)

### Healthcare and Regulated Data
```
✅ Share/validate medical data while keeping personal info private
✅ Smart contracts enforce who can see what
✅ ZK proofs confirm eligibility/participation/compliance
✅ Sensitive records never on-chain, but proofs verify conditions
```

### Finance and Compliant Privacy
```
✅ Private transfers/interactions
✅ Meet regulatory requirements (KYC, limits, screening)
✅ Enforce rules WITHOUT exposing balances
✅ Maintain confidential financial information
```

### Governance and Identity
```
✅ Selective disclosure credentials
✅ Private voting
✅ Prove membership/eligibility without revealing identity
✅ Final results publicly verifiable, individual actions private
```

### Private Trading and Liquidity
```
✅ Order size not visible
✅ Execution timing not visible
✅ Trading strategy not visible
✅ Prove every rule was met
```

---

## ⚙️ Official Genesis Configuration (From Nodes Docs)

### Ledger (Testnet only)
```
Initial Coin Supply: 100,000,000,000,000,000 units
Distribution: 4 wallets × 5 outputs × 5,000,000,000,000,000 units each
Note: This is TESTNET ONLY
Mainnet will have different configuration
```

### Consensus
```
Initial Validators: 12 permissioned (Shielded-operated) + community registered
Parameter 'D': Controls split between permissioned and registered nodes
```

### On-chain Governance
```
Master ("sudo") Key: Elevated privileges (temporary, for testing)
Tx-Pause: Governance can pause specific transaction types
Future: Will transition to proper governance system
```

---

## 📱 Official Token Model (From midnight.network)

### NIGHT Token (Unshielded Utility Token)
```
Purpose: Generate DUST resource
Usage: Governance participation
Supply Management: Configurable emission
Status: Production-ready
```

### DUST Token (Network Resource)
```
Purpose: Pay for transactions (like gas/fees)
Generation: Continuously generated by NIGHT holders
Advantage: Predictable costs vs. supply/demand volatility
Formula: NIGHT → continuous DUST generation (not transaction-based)
Revenue: Stake delegation generates DUST
```

**Official Quote:**
> "Unique dual-component tokenomics: NIGHT is the unshielded utility token
> used to generate DUST resource, separating capital assets from network resources."

---

## 🔍 Official Data Protection Layers

From official docs (What is Midnight section):

### 1. Data Minimization
```
✅ Only essential data goes on-chain
✅ Sensitive information stays in local storage
✅ Ledger never contains raw private data
```

### 2. Forward Secrecy
```
✅ Protects historical data
✅ If encryption keys compromised in future
✅ Past transactions STILL remain private
```

### 3. Selective Disclosure
```
✅ Users choose exactly what to reveal
✅ To whom it's revealed
✅ Complete control over information sharing
```

### 4. Compliance Mechanisms (Optional)
```
✅ Required reporting to authorities possible
✅ WITHOUT compromising user privacy
✅ WITHOUT exposing data to unauthorized parties
```

---

## 🚀 Official Getting Started Path

From docs.midnight.network/getting-started:

### Step 1: Install Toolchain
```bash
# Install Lace wallet
# Install Midnight toolchain
# Verify environment configuration
```

### Step 2: Create First DApp
```bash
# Create first Midnight DApp using Compact
# Deploy to local Devnet
# Fully functioning app (local testing)
```

### Step 3: Deploy to Preprod
```bash
# Deploy smart contract to Preprod
# Deploy service to Preprod
# Interact with deployed components
```

### Step 4: Learn & Iterate
```bash
# Develop on local Devnet
# Test privacy features
# Deploy to Preprod for real testing
# Production deployment when ready
```

---

## 💬 Official Support Channels

From midnight.network:

- **Discord**: https://discord.com/invite/midnightnetwork
- **Forum**: https://forum.midnight.network/
- **Developer Forum**: Technical questions
- **YouTube**: https://www.youtube.com/@midnight.network
- **Twitter/X**: https://x.com/MidnightNtwrk
- **Telegram**: https://t.me/Midnight_Network_Official
- **LinkedIn**: https://www.linkedin.com/showcase/midnight-ntwrk/

### Midnight Aliit Program
```
✅ Selective technical fellowship
✅ Deep ecosystem knowledge
✅ Connect with Midnight team
✅ Global community builders
```

---

## 📊 What We Know For Sure (Official)

| Aspect | Official Status |
|--------|-----------------|
| **Consensus** | AURA + GRANDPA (from Polkadot SDK) |
| **Block Time** | 6 seconds |
| **Privacy Model** | Public + Private states |
| **Proof System** | zk-SNARKs (128 bytes) |
| **Smart Contracts** | Compact language |
| **Token Model** | NIGHT + DUST (dual token) |
| **Testnet** | Preprod (https://faucet.preprod.midnight.network/) |
| **Node Ops** | Support both permissioned & community |
| **Cardano Bridge** | Native bridge for asset transfers |
| **Documentation** | Comprehensive at docs.midnight.network |
| **Language** | TypeScript-based (Compact) |
| **Governance** | Temporary sudo, moving to proper governance |

---

## ❓ What We DON'T Know Yet (Official Docs Missing)

| Item | Status |
|------|--------|
| **Exact RPC Endpoints** | Not in public docs |
| **Exact Indexer/GraphQL** | Not fully documented |
| **Proof Server Setup** | Not documented |
| **Exact UTXO Structure** | Not fully detailed |
| **Mainnet Launch Date** | "Coming soon" |
| **Mainnet Config** | Not yet released |
| **Exact Fee Model** | Formula not detailed |
| **Detailed API Docs** | Being expanded |

---

## 🛠️ What SDK Needs (Based On Official Info)

From analyzing official architecture:

### Phase 1: Connection
```cpp
✅ Connect to Midnight Node (AURA/GRANDPA based)
✅ Verify chain parameters (6s blocks, etc.)
✅ Request protocol info
```

### Phase 2: Transaction Building
```cpp
✅ Use Compact language patterns
✅ Build UTXO-based transactions
✅ Support ZK proof inclusion
```

### Phase 3: Witness Functions
```cpp
✅ Support private data locally
✅ Generate witness context
✅ Prepare for proof generation
```

### Phase 4: Proof Handling
```cpp
✅ Interface with proof system
✅ Generate zk-SNARK proofs
✅ Embed proofs in transactions
```

### Phase 5: Signing & Submission
```cpp
✅ Sign with sr25519 (AURA) or ed25519
✅ Support ECDSA for Partnerchain
✅ Submit to node
```

### Phase 6: State Monitoring
```cpp
✅ Monitor 6-second blocks
✅ Track both public & private state
✅ Handle reorgs (normal for PoS)
```

---

## 🎓 Official Learning Resources

- **Midnight Academy**: https://academy.midnight.network/
- **Dev Diaries**: https://docs.midnight.network/blog
- **Whitepapers**: https://midnight.network/whitepaper
- **Nightpaper**: https://midnight.network/whitepaper (architecture deep-dive)
- **Examples**: https://github.com/midnightntwrk/midnight-awesome-dapps

---

## Summary: What Changed

### Before (Assumptions):
- ❌ Assumed it was like Cardano
- ❌ Guessed at endpoints
- ❌ Made up transaction structures
- ❌ Invented Proof Server behavior

### After (From Official Docs):
- ✅ It's a 4th-gen privacy blockchain (unique)
- ✅ AURA + GRANDPA consensus
- ✅ 6-second blocks, sr25519 signing
- ✅ Compact language (TypeScript-based)
- ✅ Dual-state system (public + private)
- ✅ zk-SNARKs for proofs
- ✅ Official endpoints at faucet.preprod.midnight.network
- ✅ Official repos at github.com/midnightntwrk

---

## Next Step: Rebuild SDK Correctly

We now have **official architecture** to build against.

Ready to rewrite Phase 1-6 with correct understanding? 🚀
