// Script để reg dust từ mnemonic
// Chạy: node reg_dust.js (type=module in package.json, no --experimental flags needed)

import { createRequire } from 'module';
const require = createRequire(import.meta.url);

// Check if we can use the SDK modules
try {
  const { HDWallet, Roles, mnemonicToWords, validateMnemonic } = require('./midnight-research/node_modules/@midnight-ntwrk/wallet-sdk-hd/dist/index.js');
  const { DustAddress } = require('./midnight-research/node_modules/@midnight-ntwrk/wallet-sdk-address-format/dist/index.js');
  const { DustSecretKey } = require('./midnight-research/node_modules/@midnight-ntwrk/ledger-v8/ledger-v8.js');

  console.log('SDK modules loaded successfully');
  console.log('HDWallet:', typeof HDWallet);
  console.log('DustSecretKey:', typeof DustSecretKey);
  console.log('DustAddress:', typeof DustAddress);
} catch (e) {
  console.log('Error loading modules:', e.message);
}

// Alternative: Check npm packages available
try {
  const bip39 = require('./midnight-research/node_modules/@scure/bip39');
  const hdkey = require('./midnight-research/node_modules/@scure/bip32');
  console.log('\nUsing @scure/bip39 and @scure/bip32 directly');
  console.log('bip39:', typeof bip39);
  console.log('HDKey:', typeof hdkey.HDKey);
} catch (e) {
  console.log('Error:', e.message);
}
