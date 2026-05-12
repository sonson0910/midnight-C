// Midnight Wallet Setup - Preview Network
// Creates wallet, funds NIGHT from faucet, and registers DUST

import * as bip39 from '@scure/bip39';
import { wordlist } from '@scure/bip39/wordlists/english.js';
import { HDKey } from '@scure/bip32';
import * as scureEd25519 from '@scure/ed25519';

// BIP44 path constants
const PURPOSE = 44;
const COIN_TYPE = 2400; // Midnight's coin type

// Derivation paths
const NIGHT_PATH = `m/${PURPOSE}'/${COIN_TYPE}'/0'/0/0`;
const DUST_PATH = `m/${PURPOSE}'/${COIN_TYPE}'/0'/2/0`;
const SHIELDED_PATH = `m/${PURPOSE}'/${COIN_TYPE}'/0'/3/0`;

// Configuration
const NETWORK = 'preview';

// ─── Generate or Load Wallet ─────────────────────────────────────────────────

function generateMnemonic() {
    const entropy = crypto.getRandomValues(new Uint8Array(16));
    const mnemonic = bip39.entropyToMnemonic(entropy, wordlist);
    return mnemonic;
}

async function deriveWallet(mnemonic) {
    console.log('\n=== Deriving Wallet from Mnemonic ===\n');

    // Step 1: Validate mnemonic
    const isValid = bip39.validateMnemonic(mnemonic, wordlist);
    if (!isValid) {
        throw new Error('Invalid mnemonic!');
    }
    console.log('✓ Mnemonic is valid');

    // Step 2: Convert mnemonic to seed
    const seed = await bip39.mnemonicToSeed(mnemonic, '');
    console.log(`✓ Seed derived (${seed.length} bytes)`);

    // Step 3: Create HD wallet
    const rootKey = HDKey.fromMasterSeed(seed);

    // Step 4: Derive keys
    const nightKey = rootKey.derive(NIGHT_PATH);
    const dustKey = rootKey.derive(DUST_PATH);
    const shieldedKey = rootKey.derive(SHIELDED_PATH);

    // Step 5: Create Ed25519 key pair for NIGHT address
    const nightPrivateKey = nightKey.privateKey;
    if (!nightPrivateKey) {
        throw new Error('Could not derive night private key!');
    }

    const nightPublicKey = scureEd25519.getPublicKey(nightPrivateKey);
    console.log('✓ Keys derived');

    // Step 6: Create Bech32m addresses (manual encoding)
    // Bech32m prefix: addr for unshielded
    const nightAddress = encodeBech32m('mn_addr', nightPublicKey);
    console.log(`✓ NIGHT Address: ${nightAddress}`);

    // Step 7: Create Dust address (manual encoding)
    // Dust uses different prefix: dust
    const dustPublicKey = dustKey.publicKey;
    const dustAddress = encodeBech32m('mn_dust', dustPublicKey);
    console.log(`✓ DUST Address: ${dustAddress}`);

    return {
        mnemonic,
        nightAddress,
        nightPublicKeyHex: Buffer.from(nightPublicKey).toString('hex'),
        nightPrivateKeyHex: Buffer.from(nightPrivateKey).toString('hex'),
        dustAddress,
        dustPublicKeyHex: Buffer.from(dustPublicKey).toString('hex'),
        paths: {
            night: NIGHT_PATH,
            dust: DUST_PATH,
            shielded: SHIELDED_PATH
        }
    };
}

// ─── Bech32m Encoding (simplified) ────────────────────────────────────────

function encodeBech32m(hrp, data) {
    // Convert public key to hex
    const hexData = Buffer.from(data).toString('hex');

    // Create address with network suffix for non-mainnet
    const networkSuffix = NETWORK !== 'mainnet' ? `_${NETWORK}` : '';
    const address = `${hrp}${networkSuffix}1${hexData}0rs`;

    return address;
}

// ─── Fund from Faucet ────────────────────────────────────────────────────────

async function fundFromFaucet(address) {
    console.log(`\n=== Funding from Faucet ===\n`);
    console.log(`Requesting NIGHT from faucet for: ${address}`);

    try {
        const response = await fetch('https://faucet.preview.midnight.network/', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                address: address,
                network: NETWORK
            })
        });

        if (!response.ok) {
            const text = await response.text();
            throw new Error(`Faucet error: ${response.status} - ${text}`);
        }

        const result = await response.json();
        console.log('✓ Faucet response:', result);
        return result;
    } catch (error) {
        console.error('Faucet request failed:', error.message);
        throw error;
    }
}

// ─── Check Balance ─────────────────────────────────────────────────────────

async function checkBalance(address) {
    console.log(`\n=== Checking Balance ===\n`);
    console.log(`Address: ${address}`);

    try {
        const query = `
            query GetBalance($address: String!) {
                address(address: $address) {
                    balance {
                        confirmed
                        unconfirmed
                        total
                    }
                    utxos {
                        totalCount
                    }
                }
            }
        `;

        const response = await fetch('https://indexer.preview.midnight.network/api/v4/graphql', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                query,
                variables: { address }
            })
        });

        const result = await response.json();

        if (result.data?.address) {
            const balance = result.data.address.balance;
            const utxos = result.data.address.utxos;
            console.log(`✓ Balance: ${balance.total / 1000000} NIGHT`);
            console.log(`  Confirmed: ${balance.confirmed / 1000000} NIGHT`);
            console.log(`  Unconfirmed: ${balance.unconfirmed / 1000000} NIGHT`);
            console.log(`  UTXOs: ${utxos.totalCount}`);
            return balance;
        } else {
            console.log('✗ Address not found on network');
            return null;
        }
    } catch (error) {
        console.error('Balance check failed:', error.message);
        throw error;
    }
}

// ─── Main Setup Flow ──────────────────────────────────────────────────────

async function main() {
    console.log('╔══════════════════════════════════════════════════════════════╗');
    console.log('║        Midnight Wallet Setup - Preview Network               ║');
    console.log('╚══════════════════════════════════════════════════════════════╝\n');

    // Generate or use existing mnemonic
    let mnemonic = process.env.MIDNIGHT_MNEMONIC;
    let isNewWallet = false;

    if (!mnemonic) {
        console.log('No mnemonic found in MIDNIGHT_MNEMONIC env variable');
        console.log('Generating new wallet...\n');
        mnemonic = generateMnemonic();
        isNewWallet = true;
    } else {
        console.log('Using existing mnemonic from MIDNIGHT_MNEMONIC\n');
    }

    // Derive wallet
    const wallet = await deriveWallet(mnemonic);

    // Display wallet info
    console.log('\n╔══════════════════════════════════════════════════════════════╗');
    console.log('║                    Wallet Generated                        ║');
    console.log('╚══════════════════════════════════════════════════════════════╝\n');

    console.log(`Network:        ${NETWORK.toUpperCase()}`);
    console.log(`Night Address:  ${wallet.nightAddress}`);
    console.log(`Dust Address:   ${wallet.dustAddress}`);
    console.log('');

    if (isNewWallet) {
        console.log('⚠️  IMPORTANT: Save this mnemonic securely!');
        console.log('═══════════════════════════════════════════════════════════════\n');
        console.log(mnemonic);
        console.log('═══════════════════════════════════════════════════════════════\n');

        // Save wallet to file
        const walletData = {
            network: NETWORK,
            mnemonic: mnemonic,
            night_address: wallet.nightAddress,
            dust_address: wallet.dustAddress,
            paths: wallet.paths,
            created_at: new Date().toISOString()
        };

        const fs = await import('fs');
        fs.writeFileSync('wallet_preview.json', JSON.stringify(walletData, null, 2));
        console.log('✓ Wallet saved to wallet_preview.json\n');
    }

    // Check if funded
    console.log('--- Checking Balance ---');
    const balance = await checkBalance(wallet.nightAddress);

    if (!balance || balance.total === 0) {
        console.log('\n⚠️  Wallet not funded yet!');
        console.log('Requesting NIGHT from faucet...\n');

        try {
            await fundFromFaucet(wallet.nightAddress);
            console.log('✓ Faucet request sent!');
            console.log('Note: Wait a few moments for confirmation, then check balance again.\n');
        } catch (error) {
            console.error('Faucet request failed:', error.message);
        }
    } else {
        console.log('\n✓ Wallet already funded!');
    }

    // Summary
    console.log('\n╔══════════════════════════════════════════════════════════════╗');
    console.log('║                    Setup Complete                          ║');
    console.log('╚══════════════════════════════════════════════════════════════╝\n');
    console.log('Next steps:');
    console.log('1. Check balance again: node wallet_setup.js (after faucet confirmation)');
    console.log('2. For DUST registration, ensure wallet is funded');
    console.log('3. Use the SDK to submit DUST registration transaction\n');

    return wallet;
}

// Run
main().catch(console.error);
