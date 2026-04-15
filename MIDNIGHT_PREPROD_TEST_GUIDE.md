# Midnight Preprod - Real Transaction Test Guide

## 📍 Configuration: Preview → Preprod

### Preview Config (Demo)
```typescript
export class PreviewConfig {
    node = 'wss://rpc.preview.midnight.network';
    indexer = 'https://indexer.preview.midnight.network/api/v3/graphql';
    proofServer = 'http://127.0.0.1:6300';
    networkId = 'preview';
}
```

### Preprod Config (Real Testing)
```typescript
export class PreprodConfig {
    node = 'wss://rpc.preprod.midnight.network';
    indexer = 'https://indexer.preprod.midnight.network/api/v3/graphql';
    proofServer = 'http://127.0.0.1:6300';
    networkId = 'preprod';
    chainId = 'midnight-preprod-1';
    faucet = 'https://faucet.preprod.midnight.network';
}
```

### Key Differences
| Aspect | Preview | Preprod |
|--------|---------|---------|
| **Purpose** | Demo/Development | Pre-production Testing |
| **Faucet** | Available | Available |
| **RPC** | `rpc.preview` | `rpc.preprod` |
| **Indexer** | `indexer.preview` | `indexer.preprod` |
| **Persistence** | Limited | Persistent |
| **Data** | Resets frequently | Long-lived |

---

## 🔧 Setup Steps

### Step 1: Install Dependencies
```bash
npm install axios crypto ethers
```

### Step 2: Get Test Tokens from Faucet
```bash
# 1. Generate wallet (run the script)
npx ts-node MidnightPreprodTest.ts

# 2. Copy the generated address
# Example: 0xabcd1234...

# 3. Visit Preprod Faucet
# https://faucet.preprod.midnight.network

# 4. Paste wallet address and request tokens
# Wait for confirmation (usually 1-2 minutes)

# 5. Check balance (will show in logs when you run the script again)
```

---

## 🚀 Running the Test

### Full Transaction Flow (6 Steps)

```bash
# Install TypeScript runtime
npm install -g ts-node typescript

# Run the complete test
npx ts-node MidnightPreprodTest.ts
```

### Expected Output

```
╔════════════════════════════════════════════════════════╗
║  Midnight Preprod - Real Transaction Test             ║
║  Testing end-to-end blockchain integration            ║
╚════════════════════════════════════════════════════════╝

📝 STEP 1: Generating Ed25519 Wallet...
✅ Wallet Generated!
   Address:    0xabcd1234567890...
   PublicKey:  3045022100abcd...
   PrivateKey: 308204820201...

💰 STEP 2: Querying Balance for 0xabcd1234567890...
✅ Balance Retrieved!
   Balance: 1000000000 (smallest unit)
   Nonce:   0

📋 STEP 3: Building Transaction...
✅ Transaction Built!
   From:     0xabcd1234567890...
   To:       0xdef5678...
   Amount:   1000000
   GasLimit: 21000
   Nonce:    0

🔐 STEP 4: Signing Transaction with Ed25519...
   TX Hash: 5f8d3a9c2b...
✅ Transaction Signed!
   Signature: 7f9e4c8a2d...

📤 STEP 5: Submitting Transaction to Midnight Preprod...
   Connecting to: wss://rpc.preprod.midnight.network
   RPC Method: eth_sendRawTransaction
✅ Transaction Submitted!
   TX Hash: 0x8f9e4d8a2c...

⏳ STEP 6: Monitoring Transaction 0x8f9e4d8a2c...
   Attempt 1/30 - Still pending...
   Attempt 2/30 - Still pending...
   Attempt 3/30 - Still pending...
✅ Transaction Confirmed!
   Block Number: 12345
   Block Hash:   0xabcd1234...
   Gas Used:     21000

╔════════════════════════════════════════════════════════╗
║  ✅ FULL TRANSACTION CYCLE COMPLETED!                  ║
╚════════════════════════════════════════════════════════╝

📊 Transaction Summary:
   Wallet Address: 0xabcd1234567890...
   Transaction:    0x8f9e4d8a2c...
   Recipient:      0xdef5678...
   Amount:         1000000
   Status:         ✅ Confirmed on Preprod
```

---

## 📊 What Gets Tested

### ✅ STEP 1: Wallet Generation
- **Component**: Ed25519 Cryptography (Phase 3)
- **Tests**: Keypair generation, address derivation
- **Output**: Public key, private key, address

### ✅ STEP 2: Balance Query
- **Component**: Indexer Integration (Phase 1)
- **Tests**: GraphQL query to Indexer
- **Output**: Account balance, nonce

### ✅ STEP 3: Transaction Building
- **Component**: Transaction Builder (Phase 1)
- **Tests**: TX structure, nonce handling, gas calculation
- **Output**: Signed transaction object

### ✅ STEP 4: Transaction Signing
- **Component**: Ed25519 Signer (Phase 3)
- **Tests**: Digital signature generation
- **Output**: Signature (hex string)

### ✅ STEP 5: Transaction Submission
- **Component**: RPC Client (Phase 1 + 2)
- **Tests**: WebSocket connection, transaction broadcast
- **Output**: Transaction hash

### ✅ STEP 6: Confirmation Monitoring
- **Component**: Block Event Monitor (Phase 4D)
- **Tests**: Block polling, state sync, confirmation detection
- **Output**: Block number, gas used

---

## 🔐 Security Notes

### Private Key Handling
```typescript
// ❌ NEVER in production
const privateKey = wallet.privateKey; // Raw hex

// ✅ ALWAYS in production
const encryptedKey = encrypt(privateKey, masterPassword);
```

### Transaction Verification
```typescript
// ✅ Always verify before signing
1. Check recipient address format
2. Verify amount > 0
3. Validate gas limit
4. Confirm nonce is correct
```

### Network Verification
```typescript
// ✅ Always verify you're on correct network
1. Check networkId === 'preprod' (not 'preview')
2. Verify RPC endpoint matches
3. Confirm chain ID in transaction
```

---

## 🆘 Troubleshooting

### "Account not found (new account)"
**Problem**: Wallet has no funds on Preprod
**Solution**:
```bash
# 1. Get wallet address from output
# 2. Visit Preprod Faucet: https://faucet.preprod.midnight.network
# 3. Request test tokens
# 4. Wait 1-2 minutes
# 5. Run test again
```

### "Connection refused to rpc.preprod.midnight.network"
**Problem**: Network connectivity issue
**Solution**:
```bash
# Check if endpoint is accessible
curl -i https://rpc.preprod.midnight.network

# Try with proxy
export HTTP_PROXY=http://proxy.example.com:8080
npx ts-node MidnightPreprodTest.ts
```

### "Transaction confirmation timeout"
**Problem**: Transaction stuck in mempool or chain is slow
**Solution**:
```bash
# 1. Check gas price
# 2. Verify nonce is correct
# 3. Check Preprod network status:
#    https://status.midnight.network

# 4. Try with higher gas price
tx.gasPrice = '2';  // Increase from 1
```

### "GraphQL query failed"
**Problem**: Indexer endpoint issue
**Solution**:
```bash
# Test indexer directly
curl -X POST https://indexer.preprod.midnight.network/api/v3/graphql \
  -H "Content-Type: application/json" \
  -d '{"query": "{ __typename }"}'

# Should return something like:
# {"data":{"__typename":"Query"}}
```

---

## 📈 Monitoring & Analytics

### View Transaction on Explorer
```
https://explorer.preprod.midnight.network/tx/{TX_HASH}
```

### View Account on Explorer
```
https://explorer.preprod.midnight.network/address/{WALLET_ADDRESS}
```

### Monitor in Real-time
```bash
# Watch transaction changes
watch -n 1 'curl https://indexer.preprod.midnight.network/api/v3/graphql \
  -d "{\"query\": \"{ transaction(hash: \\\"$TX_HASH\\\") { status blockNumber } }\"}"
```

---

## 🎯 Integration with SDK

### Using Preprod Config in SDK
```typescript
import { PreprodConfig } from './MidnightPreprodConfig';
import { LedgerStateSyncManager } from './midnight/phase4d';

const config = new PreprodConfig();

// Initialize sync manager for Preprod
const syncMgr = new LedgerStateSyncManager(config.node);

// Automatically monitors Preprod blocks
syncMgr.on_block_event((event) => {
    console.log(`New block on Preprod: ${event.block_number}`);
});

syncMgr.start_monitoring();
```

### Proof Generation on Preprod
```typescript
import { ProofServerClient } from './midnight/phase4a';

const proofClient = new ProofServerClient(config.proofServer);

// Generate privacy-preserving proof
const proof = await proofClient.generate_proof(witness);

// Verify on Preprod
const isValid = await proofClient.verify_proof(proof);
```

---

## 📝 Test Report Template

```
Midnight Preprod Transaction Test Report
==========================================

Date: [YOUR_DATE]
Tester: [YOUR_NAME]
Network: Preprod

Results:
--------
✅ Wallet Generation: PASS
✅ Balance Query: PASS
✅ Transaction Build: PASS
✅ Transaction Sign: PASS
✅ Transaction Submit: PASS
✅ Confirmation Monitor: PASS

Transaction Hash: [TX_HASH]
Block Number: [BLOCK]
Gas Used: [GAS]

Observations:
- Average confirmation time: [TIME]
- Network status: [NORMAL/SLOW/ERROR]
- Any issues encountered: [NONE/DETAILS]

Recommendations:
- [SUGGESTIONS FOR IMPROVEMENT]
```

---

## 🚀 Next Steps

### After Successful Test

1. **Document Results**
   - Save transaction hash
   - Note confirmation time
   - Record gas usage

2. **Test with SDK**
   - Use LedgerStateSyncManager to monitor
   - Test proof generation with ProofServer
   - Verify resilience (Phase 4C) with failures

3. **Move to Preview Testing**
   - Repeat tests on Preview
   - Compare performance
   - Document differences

4. **Production Readiness**
   - Review security checklist
   - Performance baselines
   - Error handling

---

## 📚 Related Documentation

- **API Reference**: [API_REFERENCE.md](API_REFERENCE.md)
- **Build Guide**: [BUILD_AND_DEPLOYMENT_GUIDE.md](BUILD_AND_DEPLOYMENT_GUIDE.md)
- **Phase 4E Guide**: [PHASE4E_COMPLETE_GUIDE.md](PHASE4E_COMPLETE_GUIDE.md)
- **Config File**: [MidnightPreprodConfig.ts](MidnightPreprodConfig.ts)
- **Test Script**: [MidnightPreprodTest.ts](MidnightPreprodTest.ts)

---

**Status**: ✅ Midnight SDK Ready for Preprod Testing

Happy testing on Midnight Preprod! 🎉
