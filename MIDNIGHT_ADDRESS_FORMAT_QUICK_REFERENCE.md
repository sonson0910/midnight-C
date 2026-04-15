# Midnight Address Format - Quick Reference

## Three Address Types

### 1. Shielded Address (Private Transactions)


```
Format:     mn_shield-addr_<network>1<bech32m-encoded-data>
Example:    mn_shield-addr_preprod1q...
Encoding:   MidnightBech32m.encode(networkId, new ShieldedAddress(coinPubKey, encPubKey))
Keys:       2 public keys (coin + encryption)
Source:     Roles.Zswap from HD wallet
```


### 2. Unshielded Address (Public Transactions)

```
Format:     mn_addr_<network>1<bech32m-encoded-data>
Example:    mn_addr_preprod1q...
Retrieval:  unshieldedKeystore.getBech32Address()
Keys:       1 public key
Source:     Roles.NightExternal from HD wallet
```


### 3. Dust Address (Fees)

```
Format:     mn_dust_<network>1<bech32m-encoded-data>
Example:    mn_dust_preprod1w...
Retrieval:  state.dust.dustAddress
Computed:   Automatically from dust secret key
Source:     Roles.Dust from HD wallet
```

---

## Network Identifiers

```
preprod     → mn_addr_preprod1...
preview     → mn_addr_preview1...
undeployed  → mn_addr_undeployed1...
```

**IMPORTANT:** Network ID is embedded in address. Cannot reuse across networks.

---

## Generate from Seed

```typescript
// 1. Generate seed (64 hex chars)
const seed = toHex(Buffer.from(generateRandomSeed()));

// 2. Derive HD keys
const hdWallet = HDWallet.fromSeed(Buffer.from(seed, 'hex'));
const keys = hdWallet.hdWallet
  .selectAccount(0)
  .selectRoles([Roles.Zswap, Roles.NightExternal, Roles.Dust])
  .deriveKeysAt(0).keys;

// 3. Create wallets and get addresses
const shieldedSecretKeys = ZswapSecretKeys.fromSeed(keys[Roles.Zswap]);
const unshieldedKeystore = createKeystore(keys[Roles.NightExternal], networkId);
const dustSecretKey = DustSecretKey.fromSeed(keys[Roles.Dust]);

// 4. Get addresses
const shieldedAddr = MidnightBech32m.encode(
  networkId,
  new ShieldedAddress(coinPubKey, encPubKey)
).toString();
const unshieldedAddr = unshieldedKeystore.getBech32Address();
const dustAddr = state.dust.dustAddress;
```

---

## Generate from BIP39 Mnemonic

```typescript
import * as bip39 from 'bip39';

const mnemonic = "word1 word2 ... word24";
if (!bip39.validateMnemonic(mnemonic)) throw new Error('Invalid');

const seedBuffer = bip39.mnemonicToSeedSync(mnemonic, '');
const seed = seedBuffer.toString('hex');

// Then use seed derivation above
```

---

## Encoding Details

| Property | Value |
|----------|-------|
| Encoding Format | Bech32m (BIP 350) |
| HRP (Human Readable Part) | `mn_shield-addr_<net>1`, `mn_addr_<net>1`, `mn_dust_<net>1` |
| Checksum | Luhn-mod-32 |
| Char Set | Base32 |
| Network ID Embedded | Yes - part of encoded data |

---


## Common Mistakes

### ❌ Using Old Token Type

```typescript
// WRONG - Returns zero balance!
import { nativeToken } from '@midnight-ntwrk/ledger';

const balance = state.unshielded.balances[nativeToken()];
```

### ✅ Correct Token Type

```typescript
// CORRECT - Real balance

import { unshieldedToken } from '@midnight-ntwrk/ledger-v7';
const balance = state.unshielded.balances[unshieldedToken().raw];
```

### ❌ Accessing Unshielded Address Incorrectly


```typescript
// WRONG - Property doesn't exist
const addr = unshieldedKeystore.address;
```

### ✅ Correct Unshielded Address Access


```typescript
// CORRECT - Use method
const addr = unshieldedKeystore.getBech32Address();
```

### ❌ Reusing Address Across Networks


```typescript
// WRONG - Preprod address won't work on preview
const preprodAddr = "mn_addr_preprod1q...";
// Can't just change to preview, must regenerate
```

### ✅ Unique Address Per Network

```typescript
// Generate fresh addresses for each network
setNetworkId('preprod');
const preprodWallet = buildWallet(seed); // mn_addr_preprod1...

setNetworkId('preview');
const previewWallet = buildWallet(seed); // mn_addr_preview1...
```

---

## Libraries Required

```json
{
  "@midnight-ntwrk/wallet-sdk-address-format": "3.0.0",
  "@midnight-ntwrk/wallet-sdk-hd": "3.0.0",
  "@midnight-ntwrk/wallet-sdk-facade": "1.0.0",
  "@midnight-ntwrk/wallet-sdk-shielded": "1.0.0",
  "@midnight-ntwrk/wallet-sdk-dust-wallet": "1.0.0",
  "@midnight-ntwrk/wallet-sdk-unshielded-wallet": "1.0.0",
  "@midnight-ntwrk/ledger-v7": "^4.0.0",
  "bip39": "^2.6.0"
}
```

---

## Validation Checklist

- [ ] All three addresses generated (shielded, unshielded, dust)
- [ ] Network ID matches your target (preprod vs preview)
- [ ] Using `unshieldedToken().raw` for balance lookups (not `nativeToken()`)
- [ ] Calling `getBech32Address()` as method (not accessing `.address` property)
- [ ] Wallet synced before accessing state
- [ ] HD wallet seed cleared after key derivation
- [ ] BIP39 mnemonic validated if using mnemonic restore
- [ ] Addresses work only on their generation network

---

## Key Derivation Tree

```
Seed (64-byte / 128 hex chars)
├── HD Wallet (m/0)
│   ├── Roles.Zswap (Account 0, Index 0)
│   │   └── ZswapSecretKeys
│   │       └── ShieldedCoinPublicKey + ShieldedEncryptionPublicKey
│   │           └── mn_shield-addr_<network>1...
│   │
│   ├── Roles.NightExternal (Account 0, Index 0)
│   │   └── Public Key
│   │       └── mn_addr_<network>1...
│   │
│   └── Roles.Dust (Account 0, Index 0)
│       └── DustSecretKey
│           └── mn_dust_<network>1...
```

---

## Real Example Output

```
──────────────────────────────────────────────────────────────
  Wallet Overview                            Network: preprod
──────────────────────────────────────────────────────────────
  Seed: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

  Shielded (ZSwap)
  └─ Address: mn_shield-addr_preprod1q2m0q8ahq0wus0xjh8q8w0s9h...

  Unshielded
  ├─ Address: mn_addr_preprod1qzm2q8ahq0wus0xjh8q8w0s9h...
  └─ Balance: 1,000,000 tNight

  Dust
  └─ Address: mn_dust_pr<https://faucet.preprod.midnight.network/>

──────────────────────────────────────────────────────────────
```

---

## Faucet for Testing

**Preprod Faucet URL:** <https://faucet.preprod.midnight.network/>

Send test tokens to your **unshielded address** (`mn_addr_preprod1...`)

---

## Quick Test

```bash
# Run Counter CLI on Preprod
npm run preprod-ps

# Menu:
# [1] Create new wallet → Will show all three addresses
# Fund unshielded address via faucet
# Wallet auto-registers NIGHT for dust generation
# Ready for shielded transactions
```

---

**Source:** Reverse-engineered from real working code in texswap_smc_v2 repository
**Last Updated:** April 15, 2026
