# Midnight Blockchain Address Format - Technical Specification

## Bech32m Address Encoding Specification

### 1. Encoding Standards

**Standard:** BIP 350 (Bech32m v1.1.0)
**Character Set:** qpzry9x8gf2tvdw0s3jn54khce6mua7l (base32)
**Checksum Algorithm:** Luhn-mod-32
**Separator:** '1' character

### 2. Address Structure

```
<HRP> + '1' + <BECH32M_ENCODED_DATA>

Where:
  <HRP>                 = Human Readable Part (network + address type)
  '1'                   = Separator character
  <BECH32M_ENCODED_DATA> = 5-bit packed data with checksum
```

### 3. HRP Format

```
<HRPBASE>_<NETWORK>1

Where:
  <HRPBASE> ∈ { "mn_shield-addr", "mn_addr", "mn_dust" }
  <NETWORK> ∈ { "preprod", "preview", "undeployed" }
```

**Examples:**
- Shielded/Preprod: `mn_shield-addr_preprod1`
- Unshielded/Preprod: `mn_addr_preprod1`
- Dust/Preprod: `mn_dust_preprod1`

---

## Address Type Specifications

### Type 1: Shielded Address

**Source Location:** `@midnight-ntwrk/wallet-sdk-address-format::ShieldedAddress`

**Data Layout:**
```
Input: ShieldedAddress(coinPubKey: PublicKey, encPubKey: PublicKey)
Where:
  coinPubKey = 32-byte elliptic curve point (compressed)
  encPubKey  = 32-byte elliptic curve point (compressed)

Total: 64 bytes input data
```

**Encoding Process:**
```
1. Extract 64 bytes: coin_pubkey (32) + enc_pubkey (32)
2. Convert from 8-bit to 5-bit groups (Bech32m conversion)
3. Append 6-byte checksum (Luhn-mod-32)
4. Prepend HRP + '1'
5. Convert 5-bit groups to base32 charset

Result: mn_shield-addr_<network>1<60-char-encoded-data>
Typical length: 63-65 characters total
```

**Retrieval Code:**
```typescript
import { MidnightBech32m, ShieldedAddress,
         ShieldedCoinPublicKey, ShieldedEncryptionPublicKey }
from '@midnight-ntwrk/wallet-sdk-address-format';

const coinPubKey = ShieldedCoinPublicKey.fromHexString(
  state.shielded.coinPublicKey.toHexString()
);
const encPubKey = ShieldedEncryptionPublicKey.fromHexString(
  state.shielded.encryptionPublicKey.toHexString()
);
const addr = MidnightBech32m.encode(
  networkId,
  new ShieldedAddress(coinPubKey, encPubKey)
).toString();
```

**Example (Hypothetical):**
```
mn_shield-addr_preprod1q2m0q8ahq0wus0xjh8q8w0s9h0sq2m0q8ahq0wus0x
│                    │ │                                        │
│                    HRP separator └─ encoded 64-byte key data─┘
└─────── HRP ────────┘
```

### Type 2: Unshielded Address

**Source Location:** `@midnight-ntwrk/wallet-sdk-unshielded-wallet::UnshieldedKeystore`

**Derivation:**
```
Unshielded public key (32 bytes)
  ↓
[BIP173/BIP350 encoding with version byte]
  ↓
Bech32m address with mn_addr_<network>1 HRP
```

**Retrieval Code:**
```typescript
const addr = unshieldedKeystore.getBech32Address();
// Returns: mn_addr_<network>1<encoded-pubkey>
```

**Key Difference:** Unlike shielded addresses (which encode curve points directly), unshielded addresses may include version/type information in the encoding.

**Example (Hypothetical):**
```
mn_addr_preprod1qzm2q8ahq0wus0xjh8q8w0s9h0sq2m0q8ahq0w
│                │ │                                  │
│                HRP separator  └─ encoded key data──┘
└──── HRP ───────┘
```

### Type 3: Dust Address

**Source Location:** `@midnight-ntwrk/wallet-sdk-dust-wallet::DustWallet`

**Generation:**
```
DustSecretKey (32 bytes, from HD derivation)
  ↓
[Derive public key from secret key]
  ↓
[Bech32m encoding with version/type byte]
  ↓
Address with mn_dust_<network>1 HRP
```

**Retrieval Code:**
```typescript
const addr = state.dust.dustAddress;
// Returns: mn_dust_<network>1<encoded-pubkey>
```

**Note:** Pre-computed and stored in wallet state, not called directly like shielded.

**Example (Hypothetical):**
```
mn_dust_preprod1wh8q8w0s9h0sq2m0q8ahq0wus0xjh8q8w0s9h
│               │ │                                   │
│               HRP separator   └─ encoded data ───────┘
└─── HRP ───────┘
```

---

## HD Wallet Key Derivation Path

### BIP44 Derivation Tree

```
root (BIP32 seed)
  ↓
m (master key pair)
  ↓
m/44' (BIP44 purpose: Midnight wallet)
  ↓
m/44'/1' (coin_type: 1 = Midnight testnet)
  ↓
m/44'/1'/0' (account: 0)
  ↓
m/44'/1'/0'/0' Branch (External)
  ├── Roles.Zswap (index 0)         → coin_pubkey + enc_pubkey
  ├── Roles.NightExternal (index 0) → unshielded_pubkey
  └── Roles.Dust (index 0)          → dust_pubkey
```

### Implementation Code

**File:** counter-cli/src/api.ts, lines 795-810

```typescript
import { HDWallet, Roles } from '@midnight-ntwrk/wallet-sdk-hd';

const hdWallet = HDWallet.fromSeed(Buffer.from(hexSeed, 'hex'));
// Equivalent to: BIP39 seed → BIP32 master → BIP44 tree

const derivationResult = hdWallet.hdWallet
  .selectAccount(0)        // m/44'/1'/0'
  .selectRoles([
    Roles.Zswap,           // m/44'/1'/0'/0/0 → coin + enc keys
    Roles.NightExternal,   // m/44'/1'/0'/1/0 → unshielded key
    Roles.Dust             // m/44'/1'/0'/2/0 → dust key
  ])
  .deriveKeysAt(0);        // index 0 for all roles

// Result: keys[Roles.Zswap], keys[Roles.NightExternal], keys[Roles.Dust]
```

### Key Role Mapping

| Role | Path Index | Produces | Wallet | Address Type |
|------|-----------|----------|--------|--------------|
| Zswap | 0 | 64-byte keypair | Shielded | `mn_shield-addr_<net>1` |
| NightExternal | 1 | 32-byte pubkey | Unshielded | `mn_addr_<net>1` |
| Dust | 2 | 32-byte keypair | Dust | `mn_dust_<net>1` |

---

## Seed Generation and BIP39

### BIP39 Specification

**Standard:** BIP 0039 - Mnemonic code for generating deterministic keys

**Mnemonic:** 24-word phrase
**Word List:** English word list (2048 words)
**Entropy:** 256 bits (24 words × 11 bits - 8 checksum bits)

### Mnemonic to Seed Conversion

```typescript
import * as bip39 from 'bip39';

// Validation
const isValid = bip39.validateMnemonic(mnemonic24);

// Conversion to seed
const seedBuffer = bip39.mnemonicToSeedSync(
  mnemonic24,
  ''  // Empty passphrase for Midnight
);
const seed = seedBuffer.toString('hex');

// Result: 64-character hex string (128 hex chars = 64 bytes)
// Format: sha512(PBKDF2("BIP39 seed", passphrase, 2048, 64 bytes))
```

### Random Seed Generation

```typescript
import { generateRandomSeed } from '@midnight-ntwrk/wallet-sdk-hd';
import { toHex } from '@midnight-ntwrk/midnight-js-utils';
import { Buffer } from 'buffer';

const randomBytes = generateRandomSeed();  // 32-byte Uint8Array
const hexSeed = toHex(Buffer.from(randomBytes));  // 64-char hex string
```

---

## Network ID Embedding

### Network ID and Address Relationship

**CRITICAL:** Network ID is part of the bech32m encoded data, NOT just a string prefix

```typescript
// Different network IDs produce different encodings
setNetworkId('preprod');
const preprodAddr = MidnightBech32m.encode(networkId, shieldedAddr).toString();
// Result: mn_shield-addr_preprod1q...

setNetworkId('preview');
const previewAddr = MidnightBech32m.encode(networkId, shieldedAddr).toString();
// Result: mn_shield-addr_preview1q...
// NOTE: Even with same key, the encoded data differs!
```

### Why Addresses Can't Be Reused

```typescript
// Same seed, different networks
const seed = "0123456789...";
const coinPubKey = derived_coin_pubkey;
const encPubKey = derived_enc_pubkey;

// Preprod encoding (networkId = "preprod")
const preprodAddress = MidnightBech32m.encode("preprod",
  new ShieldedAddress(coinPubKey, encPubKey)).toString();
// → mn_shield-addr_preprod1q2m0q8ahq0wus0x...

// Preview encoding (networkId = "preview")
const previewAddress = MidnightBech32m.encode("preview",
  new ShieldedAddress(coinPubKey, encPubKey)).toString();
// → mn_shield-addr_preview1q2m0q8ahq0wus0x...
// DIFFERENT address even though keys are same!
```

The encoding algorithm incorporates the network ID into the checksum and encoding process.

---

## Token Types (Critical Implementation Detail)

### Unshielded Token Type

**OLD (Broken) - @midnight-ntwrk/ledger v4:**
```typescript
nativeToken()  // Returns: "020000...0000" (68 hex chars)
// This is tagged: 02 = prefix + 64 chars of token hash + 02 = suffix
```

**NEW (Correct) - @midnight-ntwrk/ledger-v7:**
```typescript
unshieldedToken().raw  // Returns: "000000...0000" (64 hex chars)
// This is raw: pure token hash without 02 prefix/suffix
```

**Why This Matters:**
```typescript
state.unshielded.balances = {
  "0000000000...0000": 1000000n,  // Keyed by raw token type (64 chars)
}

// WRONG - returns undefined
const balance = state.unshielded.balances[nativeToken()];
// Searching for: "02000000...0000" (68 chars) - doesn't exist!

// CORRECT - returns 1000000n
const balance = state.unshielded.balances[unshieldedToken().raw];
// Searching for: "00000000...0000" (64 chars) - found!
```

---

## Wallet State Structure

### Complete Wallet State

```typescript
interface WalletState {
  isSynced: boolean;

  shielded: {
    coinPublicKey: ByteArray;           // 32 bytes
    encryptionPublicKey: ByteArray;     // 32 bytes
    availableCoins: {
      coin: { type: string; value: bigint; nonce: ByteArray };
      coin_pubkey: ByteArray;
    }[];
    balances: Record<string, bigint>;   // Keyed by token type (64 hex chars)
  };

  unshielded: {
    balances: Record<string, bigint>;   // Keyed by token type (64 hex chars)
    availableCoins: UTXOInfo[];
  };

  dust: {
    dustAddress: string;                // mn_dust_<net>1...
    availableCoins: DustCoin[];
    walletBalance(date: Date): bigint;
  };
}
```

### Address Generation from State

```typescript
// Get addresses from wallet state after sync
const state = await wallet.state();

// Shielded address (computed on-the-fly from keys)
const shieldedAddr = MidnightBech32m.encode(
  networkId,
  new ShieldedAddress(
    ShieldedCoinPublicKey.fromHexString(state.shielded.coinPublicKey.toHexString()),
    ShieldedEncryptionPublicKey.fromHexString(state.shielded.encryptionPublicKey.toHexString())
  )
).toString();

// Unshielded address (from keystore)
const unshieldedAddr = unshieldedKeystore.getBech32Address();

// Dust address (pre-computed in state)
const dustAddr = state.dust.dustAddress;
```

---

## Validation Rules

### Address Format Validation

```typescript
function validateMidnightAddress(addr: string, expectedNetwork: string): boolean {
  const networks = { preprod: true, preview: true, undeployed: true };

  // Check HRP
  const validHRPs = [
    `mn_shield-addr_${expectedNetwork}1`,
    `mn_addr_${expectedNetwork}1`,
    `mn_dust_${expectedNetwork}1`,
  ];

  if (!validHRPs.some(hrp => addr.startsWith(hrp))) {
    return false;  // Invalid HRP or wrong network
  }

  // Check Bech32m format
  const match = addr.match(/^mn_[a-z_]+1([a-z0-9]+)$/);
  if (!match) {
    return false;  // Invalid bech32 format
  }

  // Validate base32 charset
  const data = match[1];
  if (!/^[qpzry9x8gf2tvdw0s3jn54khce6mua7l]*$/.test(data)) {
    return false;  // Invalid base32 characters
  }

  return true;
}
```

### Common Validation Failures

| Error | Cause | Fix |
|-------|-------|-----|
| Network mismatch | Preprod address used on preview | Regenerate on correct network |
| Missing HRP | Address format corrupted | Re-export from wallet |
| Invalid checksum | Address manually edited | Use wallet state |
| Invalid base32 | Character outside qpzry9x8... | Check copy-paste |
| Address length | Truncated or extended | Use full address |

---

## Reference Implementation

**Complete working implementation:** [counter-cli/src/api.ts](https://github.com/sonson0910/texswap_smc_v2/blob/main/counter-cli/src/api.ts)

**Critical functions:**
- Line 795-810: HD wallet derivation
- Line 907-911: Shielded address encoding
- Line 926: Unshielded address retrieval
- Line 925, 930: Dust address access
- Line 1024-1039: BIP39 mnemonic restoration

---

## Performance Notes

### Address Generation Performance

- **Bech32m encoding:** ~0.1-1ms per address
- **HD wallet derivation:** ~10-50ms depending on key material
- **Wallet state sync:** ~1-5s network roundtrip

### Caching Recommendations

```typescript
// Cache pre-computed addresses
const addressCache = {
  shielded: MidnightBech32m.encode(networkId, shieldedAddr).toString(),
  unshielded: unshieldedKeystore.getBech32Address(),
  dust: state.dust.dustAddress,
};

// Reuse instead of recomputing
console.log(addressCache.shielded);  // Fast - no recomputation
```

---

## Security Considerations

1. **Seed Storage:** Store 64-char hex seed securely (never hardcode)
2. **Key Derivation:** Always clear HD wallet after derivation (`hdWallet.clear()`)
3. **Address Reuse:** Use unique addresses for each transaction on Midnight
4. **Network Separation:** Never mix addresses between preprod/preview/mainnet
5. **BIP39 Passphrase:** Keep empty for standard Midnight wallets

---

**Technical Specification Version:** 1.0
**Last Updated:** April 15, 2026
**Source:** Reverse-engineered from texswap_smc_v2 and @midnight-ntwrk packages
