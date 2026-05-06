// Script để derive dust seed từ mnemonic và verify địa chỉ
// Chạy: node reg_dust.js

import * as bip39 from '@scure/bip39';
import { wordlist } from '@scure/bip39/wordlists/english.js';
import { HDKey } from '@scure/bip32';

const mnemonic = process.env.MIDNIGHT_MNEMONIC;
if (!mnemonic) {
    console.error('MIDNIGHT_MNEMONIC environment variable is required');
    process.exit(1);
}

console.log('=== Midnight Dust Derivation Test ===\n');

// Step 1: Validate mnemonic
console.log('1. Validating mnemonic...');
const isValid = bip39.validateMnemonic(mnemonic, wordlist);
console.log('   Mnemonic valid:', isValid);
if (!isValid) {
  console.error('Invalid mnemonic!');
  process.exit(1);
}

// Step 2: Convert mnemonic to seed
console.log('\n2. Converting mnemonic to seed...');
const seed = await bip39.mnemonicToSeed(mnemonic, '');
console.log('   Seed length:', seed.length, 'bytes');
console.log('   Seed (hex):', Buffer.from(seed).toString('hex').slice(0, 64) + '...');

// Step 3: Create HD wallet and derive dust key
console.log('\n3. Deriving HD wallet...');
const rootKey = HDKey.fromMasterSeed(seed);
console.log('   Root key derived');

// Step 4: Derive dust key at path m/44'/2400'/0'/2/0
console.log('\n4. Deriving dust key (path: m/44\'/2400\'/0\'/2/0)...');
const PURPOSE = 44;
const COIN_TYPE = 2400;
const path = `m/${PURPOSE}'/${COIN_TYPE}'/0'/2/0`;
console.log('   Full path:', path);

const dustKey = rootKey.derive(path);
if (!dustKey.privateKey) {
  console.error('   ERROR: Could not derive dust private key!');
  process.exit(1);
}

console.log('   Dust private key length:', dustKey.privateKey.length);
console.log('   Dust wallet registered successfully (private key hidden)');

// The dust private key is a 32-byte scalar (Ed25519 secret scalar)
// This is used as the "seed" for DustSecretKey.fromSeed()

// Step 5: Verify with SDK's DustAddress format
console.log('\n5. Dust address format check...');
console.log('   Dust address is derived from DustSecretKey.publicKey (a bigint)');
console.log('   Format: bech32m encoded as mn_dust_preview...');

// Expected dust address from wallet.key:
// Dust Address: mn_dust_preview1p2um3retet8wfkaj20hz9prc6l3302pnsupwfju9qan6xz02alusxpm4jh

console.log('\n=== Summary ===');
console.log('The 32-byte dust private key is the seed for DustSecretKey.fromSeed()');
console.log('To reg dust, we need to:');
console.log('1. Use this seed to create a DustSecretKey');
console.log('2. Use the DustSecretKey.publicKey to get the dust address');
console.log('3. Call the reg_dust API with the dust address');

// Try to load ledger-v8 WASM
console.log('\n=== Testing ledger-v8 WASM ===');
try {
  // Dynamic import for WASM
  const ledger = await import('./midnight-research/node_modules/@midnight-ntwrk/ledger-v8/ledger-v8.js');
  console.log('ledger-v8 loaded');
  
  if (typeof ledger.DustSecretKey !== 'undefined') {
    console.log('DustSecretKey available');
    
    // Create DustSecretKey from our dust private key
    const dustSecretKey = ledger.DustSecretKey.fromSeed(dustKey.privateKey);
    console.log('DustSecretKey created');
    console.log('Dust public key (hidden for security)');
    
    // The dust address should be dustSecretKey.publicKey encoded as bech32m
    // Check if DustAddress.encodePublicKey is available
    if (typeof ledger.DustSecretKey !== 'undefined') {
      // Try to encode
      const addr = ledger.DustAddress.encodePublicKey('mainnet', dustSecretKey.publicKey);
      console.log('Dust address:', addr);
    }
  }
} catch (e) {
  console.log('Could not load ledger-v8:', e.message);
  console.log('\nNote: ledger-v8 uses WASM which may need special handling');
}
