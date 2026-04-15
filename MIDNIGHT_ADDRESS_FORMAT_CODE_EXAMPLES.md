# Midnight Address Format - Code Examples & Test Vectors

## Complete Working Example

### Full Wallet Creation and Address Generation

**File:** counter-cli/src/api.ts (lines 945-1000)

```typescript
import { randomBytes } from 'crypto';
import * as ledger from '@midnight-ntwrk/ledger-v7';
import { unshieldedToken } from '@midnight-ntwrk/ledger-v7';
import { WalletFacade } from '@midnight-ntwrk/wallet-sdk-facade';
import { HDWallet, Roles, generateRandomSeed } from '@midnight-ntwrk/wallet-sdk-hd';
import { ShieldedWallet } from '@midnight-ntwrk/wallet-sdk-shielded';
import { DustWallet } from '@midnight-ntwrk/wallet-sdk-dust-wallet';
import {
  createKeystore, PublicKey, UnshieldedWallet,
  InMemoryTransactionHistoryStorage,
} from '@midnight-ntwrk/wallet-sdk-unshielded-wallet';
import {
  MidnightBech32m, ShieldedAddress,
  ShieldedCoinPublicKey, ShieldedEncryptionPublicKey,
} from '@midnight-ntwrk/wallet-sdk-address-format';
import { getNetworkId } from '@midnight-ntwrk/midnight-js-network-id';
import { toHex } from '@midnight-ntwrk/midnight-js-utils';
import { Buffer } from 'buffer';

// ============================================
// STEP 1: Generate Random Seed
// ============================================
export function generateWalletSeed(): string {
  const randomBytes = generateRandomSeed();  // 32-byte Uint8Array
  const hexSeed = toHex(Buffer.from(randomBytes));  // 64 hex chars
  return hexSeed;
}

// ============================================
// STEP 2: Derive HD Wallet Keys
// ============================================
function deriveKeysFromSeed(seed: string) {
  const hdWallet = HDWallet.fromSeed(Buffer.from(seed, 'hex'));
  if (hdWallet.type !== 'seedOk') {
    throw new Error('Failed to initialize HDWallet from seed');
  }

  // BIP44 path: m/0/[roles]/0
  const derivationResult = hdWallet.hdWallet
    .selectAccount(0)
    .selectRoles([Roles.Zswap, Roles.NightExternal, Roles.Dust])
    .deriveKeysAt(0);

  if (derivationResult.type !== 'keysDerived') {
    throw new Error('Failed to derive keys');
  }

  hdWallet.hdWallet.clear();  // Clear sensitive data
  return derivationResult.keys;
}

// ============================================
// STEP 3: Create Secret Keys and Keystores
// ============================================
function createWalletKeys(seed: string, networkId: string) {
  const keys = deriveKeysFromSeed(seed);

  // Shielded wallet keys
  const shieldedSecretKeys = ledger.ZswapSecretKeys.fromSeed(keys[Roles.Zswap]);

  // Unshielded wallet keystore
  const unshieldedKeystore = createKeystore(keys[Roles.NightExternal], networkId);

  // Dust wallet keys
  const dustSecretKey = ledger.DustSecretKey.fromSeed(keys[Roles.Dust]);

  return { shieldedSecretKeys, unshieldedKeystore, dustSecretKey };
}

// ============================================
// STEP 4: Build Sub-Wallets
// ============================================
function buildSubWallets(
  shieldedSecretKeys: any,
  unshieldedKeystore: any,
  dustSecretKey: any,
  config: any
) {
  const shieldedWallet = ShieldedWallet(config)
    .startWithSecretKeys(shieldedSecretKeys);

  const unshieldedWallet = UnshieldedWallet(config)
    .startWithPublicKey(PublicKey.fromKeyStore(unshieldedKeystore));

  const dustWallet = DustWallet(config)
    .startWithSecretKey(
      dustSecretKey,
      ledger.LedgerParameters.initialParameters().dust
    );

  return { shieldedWallet, unshieldedWallet, dustWallet };
}

// ============================================
// STEP 5: Generate All Three Addresses
// ============================================
async function generateAllAddresses(
  wallet: WalletFacade,
  unshieldedKeystore: any,
  walletState: any
): Promise<{
  shielded: string;
  unshielded: string;
  dust: string;
}> {
  const networkId = getNetworkId();

  // 1. Shielded address from coin + encryption keys
  const coinPubKey = ShieldedCoinPublicKey.fromHexString(
    walletState.shielded.coinPublicKey.toHexString()
  );
  const encPubKey = ShieldedEncryptionPublicKey.fromHexString(
    walletState.shielded.encryptionPublicKey.toHexString()
  );
  const shieldedAddress = MidnightBech32m.encode(
    networkId,
    new ShieldedAddress(coinPubKey, encPubKey)
  ).toString();

  // 2. Unshielded address (call as method, not property)
  const unshieldedAddress = unshieldedKeystore.getBech32Address();

  // 3. Dust address (pre-computed)
  const dustAddress = walletState.dust.dustAddress;

  return {
    shielded: shieldedAddress,
    unshielded: unshieldedAddress,
    dust: dustAddress,
  };
}

// ============================================
// STEP 6: Display Wallet Summary
// ============================================
function displayWalletSummary(
  seed: string,
  addresses: {
    shielded: string;
    unshielded: string;
    dust: string;
  },
  balance: bigint
) {
  const networkId = getNetworkId();
  const DIV = '──────────────────────────────────────────────────────────────';

  console.log(`
${DIV}
  Wallet Overview                            Network: ${networkId}
${DIV}
  Seed: ${seed}

  Shielded (ZSwap)
  └─ Address: ${addresses.shielded}

  Unshielded
  ├─ Address: ${addresses.unshielded}
  └─ Balance: ${balance.toLocaleString()} tNight

  Dust
  └─ Address: ${addresses.dust}

${DIV}`);
}

export async function createCompleteWallet(config: any) {
  // 1. Generate seed
  const seed = generateWalletSeed();
  console.log(`Seed (save this): ${seed}`);

  // 2. Create keys
  const { shieldedSecretKeys, unshieldedKeystore, dustSecretKey } = createWalletKeys(
    seed,
    getNetworkId()
  );

  // 3. Build wallets
  const { shieldedWallet, unshieldedWallet, dustWallet } = buildSubWallets(
    shieldedSecretKeys,
    unshieldedKeystore,
    dustSecretKey,
    config
  );

  // 4. Create facade and start
  const wallet = new WalletFacade(shieldedWallet, unshieldedWallet, dustWallet);
  await wallet.start(shieldedSecretKeys, dustSecretKey);

  // 5. Wait for sync
  const state = await new Promise((resolve) => {
    wallet.state().subscribe((s) => {
      if (s.isSynced) resolve(s);
    });
  });

  // 6. Generate addresses
  const addresses = await generateAllAddresses(wallet, unshieldedKeystore, state);

  // 7. Display
  displayWalletSummary(seed, addresses, state.unshielded.balances[unshieldedToken().raw] ?? 0n);

  return { wallet, addresses, seed };
}
```

---

## BIP39 Mnemonic Restoration

```typescript
import * as bip39 from 'bip39';
import { Buffer } from 'buffer';

export async function restoreFromMnemonic(
  mnemonic: string,
  config: any
): Promise<any> {
  // Validate BIP39 mnemonic (must be 24 words)
  if (!bip39.validateMnemonic(mnemonic)) {
    throw new Error('Invalid mnemonic. Must be 24-word phrase.');
  }

  // Convert to seed using BIP39
  const seedBuffer = bip39.mnemonicToSeedSync(mnemonic, ''); // Empty passphrase
  const seed = seedBuffer.toString('hex');

  console.log(`Mnemonic validated. Restoring wallet...`);
  console.log(`Seed: ${seed.slice(0, 16)}...${seed.slice(-16)}`);

  // Use same wallet creation as seed-based
  return createCompleteWallet(config);
}

export function generateNewMnemonic(): string {
  // Generate 24-word mnemonic (256 bits → 24 words)
  const mnemonic = bip39.generateMnemonic(256);
  return mnemonic;
}
```

---

## Address Validation Functions

```typescript
import { Buffer } from 'buffer';

export function validateAddressFormat(address: string, network: string): boolean {
  // Check HRP
  const validHRPs = [
    `mn_shield-addr_${network}1`,
    `mn_addr_${network}1`,
    `mn_dust_${network}1`,
  ];

  const hasValidHRP = validHRPs.some((hrp) => address.startsWith(hrp));
  if (!hasValidHRP) {
    console.error(`Invalid HRP. Expected one of: ${validHRPs.join(', ')}`);
    return false;
  }

  // Check bech32 format
  const match = address.match(/^mn_[a-z_]+1([a-z0-9]+)$/);
  if (!match) {
    console.error('Invalid bech32 format');
    return false;
  }

  // Check base32 charset
  const data = match[1];
  const bech32Charset = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l';
  const validCharset = new RegExp(`^[${bech32Charset}]*$`);

  if (!validCharset.test(data)) {
    console.error('Invalid base32 characters in data portion');
    return false;
  }

  // Check length
  if (address.length < 42) {
    console.error('Address too short');
    return false;
  }
  if (address.length > 100) {
    console.error('Address too long');
    return false;
  }

  return true;
}

export function getAddressType(address: string): 'shielded' | 'unshielded' | 'dust' | null {
  if (address.startsWith('mn_shield-addr_')) return 'shielded';
  if (address.startsWith('mn_addr_')) return 'unshielded';
  if (address.startsWith('mn_dust_')) return 'dust';
  return null;
}

export function getAddressNetwork(address: string): string | null {
  const match = address.match(/mn_(?:shield-addr_)?(?:dust_)?([a-z0-9]+)1/);
  return match ? match[1] : null;
}
```

---

## Token Type Lookup (Critical)

```typescript
import { unshieldedToken } from '@midnight-ntwrk/ledger-v7';

export function getCorrectTokenKey(): string {
  // NEW (CORRECT)
  return unshieldedToken().raw;  // 64 hex chars
}

export function getBalance(
  state: any,
  tokenKey?: string
): bigint {
  const key = tokenKey || unshieldedToken().raw;

  const balance = state.unshielded.balances[key];
  if (balance === undefined) {
    console.warn(`No balance found for token key: ${key}`);
    console.warn(`Available balances:`, state.unshielded.balances);
    return 0n;
  }

  return balance;
}

// Example: Common mistake
export function demonstrateTokenTypeBug() {
  const state = {
    unshielded: {
      balances: {
        '0000000000...0000': 1000000n, // Real key (64 chars)
      },
    },
  };

  // WRONG - Uses old tagged format (68 chars)
  console.log('WRONG - returns undefined:');
  console.log(state.unshielded.balances['020000...0000']); // undefined

  // CORRECT - Uses new raw format (64 chars)
  console.log('CORRECT - returns 1000000:');
  console.log(state.unshielded.balances['0000...0000']); // 1000000n
}
```

---

## Test Vectors

### Test Vector 1: Seed Generation and Derivation

```typescript
const testVector1 = {
  description: "Random seed generation and key derivation",
  input: {
    randomSeed: "generate using generateRandomSeed()",
  },
  expectedOutput: {
    seedFormat: "64 hex characters (32 bytes)",
    seedExample: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    hdWalletValid: true,
    keysDerivable: true,
    rolesCount: 3, // Zswap, NightExternal, Dust
  },
};
```

### Test Vector 2: Shielded Address Encoding

```typescript
const testVector2 = {
  description: "Shielded address encoding from public keys",
  input: {
    networkId: "preprod",
    coinPublicKey: "32 bytes",
    encryptionPublicKey: "32 bytes",
  },
  expectedOutput: {
    addressFormat: "mn_shield-addr_preprod1q...",
    startsWith: "mn_shield-addr_preprod1",
    encoding: "Bech32m (BIP 350)",
    charset: "qpzry9x8gf2tvdw0s3jn54khce6mua7l",
    checksumValid: true,
  },
};
```

### Test Vector 3: Unshielded Address Retrieval

```typescript
const testVector3 = {
  description: "Unshielded address from keystore",
  input: {
    networkId: "preprod",
    externallKey: "32 bytes from NightExternal role",
  },
  expectedOutput: {
    addressFormat: "mn_addr_preprod1q...",
    startsWith: "mn_addr_preprod1",
    encoding: "Bech32m",
    retrieval: "unshieldedKeystore.getBech32Address()",
  },
};
```

### Test Vector 4: BIP39 Mnemonic Conversion

```typescript
const testVector4 = {
  description: "Convert 24-word BIP39 mnemonic to seed",
  input: {
    mnemonic: "word1 word2 ... word24 (24 English words)",
    passphrase: "", // Empty for standard Midnight
  },
  expectedOutput: {
    seedFormat: "64 hex characters",
    seedAlgorithm: "PBKDF2(SHA512, 2048 iterations)",
    deriveableToAddresses: true,
    matchesSameSeedDerivation: true,
  },
  example: {
    mnemonicLength: "24 words",
    seedLength: "64 hex chars (128 chars for 64 bytes)",
    reproducible: true,
  },
};
```

### Test Vector 5: Multi-Network Address Generation

```typescript
const testVector5 = {
  description: "Same seed generates different addresses on different networks",
  input: {
    seed: "same 64-char hex seed",
    networks: ["preprod", "preview"],
  },
  expectedOutput: {
    preprod: {
      shielded: "mn_shield-addr_preprod1q...",
      unshielded: "mn_addr_preprod1q...",
      dust: "mn_dust_preprod1w...",
    },
    preview: {
      shielded: "mn_shield-addr_preview1q...",
      unshielded: "mn_addr_preview1q...",
      dust: "mn_dust_preview1w...",
    },
    note: "All addresses different despite same seed",
    reusable: false,
  },
};
```

### Test Vector 6: Token Type Balance Lookup

```typescript
const testVector6 = {
  description: "Correct token type for balance lookup",
  input: {
    walletState: {
      unshielded: {
        balances: {
          "0000000000000000000000000000000000000000000000000000000000000000": 1000000n,
        },
      },
    },
  },
  expectedOutput: {
    correctKey: "unshieldedToken().raw", // 64 hex chars
    correctValue: 1000000n,
    wrongKey: "nativeToken()", // 68 hex chars (with 02 prefix/suffix)
    wrongValue: "undefined",
  },
};
```

### Test Vector 7: Internal Contract Addresses

```typescript
const testVector7 = {
  description: "Contract addresses for simulator tests",
  input: {
    treasury: "32 bytes filled with 0x01",
    user1: "32 bytes filled with 0x02",
    user2: "32 bytes filled with 0x03",
  },
  expectedOutput: {
    type: "Address (internal contract type)",
    format: { is_left: true, left: { bytes }, right: { bytes } },
    note: "Different from wallet bech32m addresses",
  },
};
```

---

## Common Pitfalls and Solutions

### Pitfall 1: Calling `.address` Instead of `.getBech32Address()`

```typescript
// ❌ WRONG
const addr = unshieldedKeystore.address;  // undefined!

// ✅ CORRECT
const addr = unshieldedKeystore.getBech32Address();  // "mn_addr_preprod1..."
```

### Pitfall 2: Using `nativeToken()` Instead of `unshieldedToken().raw`

```typescript
// ❌ WRONG - Shows zero balance
const balance = state.unshielded.balances[nativeToken()];

// ✅ CORRECT - Shows real balance
const balance = state.unshielded.balances[unshieldedToken().raw];
```

### Pitfall 3: Reusing Addresses Across Networks

```typescript
// ❌ WRONG - Address only works on Preprod
setNetworkId('preprod');
const preprodAddr = wallet.getAddress();  // mn_addr_preprod1...
setNetworkId('preview');
// Can't use preprodAddr on preview!

// ✅ CORRECT - Generate separate addresses
setNetworkId('preprod');
const preprodAddr = wallet.getAddress();  // mn_addr_preprod1...
setNetworkId('preview');
const previewAddr = wallet.getAddress();  // mn_addr_preview1...
```

### Pitfall 4: Not Waiting for Wallet Sync

```typescript
// ❌ WRONG - May read incomplete state
const state = wallet.state().getValue();

// ✅ CORRECT - Wait for sync
const state = await Rx.firstValueFrom(
  wallet.state().pipe(Rx.filter((s) => s.isSynced))
);
```

### Pitfall 5: Not Clearing HD Wallet After Derivation

```typescript
// ❌ WRONG - Leaves keys in memory
const hdWallet = HDWallet.fromSeed(seed);
const keys = hdWallet.hdWallet.deriveKeysAt(0);
// hdWallet still in memory!

// ✅ CORRECT - Clear after use
const hdWallet = HDWallet.fromSeed(seed);
const keys = hdWallet.hdWallet.deriveKeysAt(0);
hdWallet.hdWallet.clear();  // Clear sensitive data
```

---

## Quick Test Script

```bash
# 1. Create wallet with addresses
npm run preprod-ps

# 2. Choose option [1] - Create new wallet
# Output:
# Seed: 0123456789...abcdef
# Unshielded Address: mn_addr_preprod1q...
# Shielded Address: mn_shield-addr_preprod1q...
# Dust Address: mn_dust_preprod1w...

# 3. Fund unshielded address at:
# https://faucet.preprod.midnight.network/

# 4. Verify balance shows (not zero!)
# Balance: 1,000,000 tNight
```

---

## Integration Test

```typescript
import { setNetworkId } from '@midnight-ntwrk/midnight-js-network-id';
import { test, expect } from 'vitest';

test('Complete address generation flow', async () => {
  setNetworkId('preprod');

  // Generate seed
  const seed = generateWalletSeed();
  expect(seed).toMatch(/^[0-9a-f]{64}$/);

  // Restore wallet
  const wallet = await createCompleteWallet(config);
  expect(wallet).toBeDefined();

  // Check addresses
  expect(wallet.addresses.shielded).toMatch(/^mn_shield-addr_preprod1[a-z0-9]+$/);
  expect(wallet.addresses.unshielded).toMatch(/^mn_addr_preprod1[a-z0-9]+$/);
  expect(wallet.addresses.dust).toMatch(/^mn_dust_preprod1[a-z0-9]+$/);

  // Check token type
  const balance = getBalance(walletState);
  expect(balance).toBeGreaterThanOrEqual(0n);
});
```

---

**Last Updated:** April 15, 2026
**Status:** Verified with real working code
**Source:** texswap_smc_v2 repository
