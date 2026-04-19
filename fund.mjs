import { setNetworkId } from '@midnight-ntwrk/midnight-js-network-id';
import { initWalletWithSeed, waitForSync, waitForFunds, registerNightForDust, closeWallet } from './src/wallet.js';
import * as ledger from '@midnight-ntwrk/ledger-v8';
async function run() {
  setNetworkId('undeployed');
  const config = {
    indexer: 'http://localhost:8088/api/v3/graphql',
    indexerWS: 'ws://localhost:8088/api/v3/graphql/ws',
    node: 'http://localhost:9944',
    proofServer: 'http://localhost:6300',
    networkId: 'undeployed'
  };
  const masterSeed = new Uint8Array(32);
  masterSeed[31] = 1;
  const masterWallet = await initWalletWithSeed(masterSeed, config);
  await waitForSync(masterWallet.wallet);
  console.log("Master wallet synced");

  const dumpSeedHex = '5c4ade190eb78ef0d3050eef47171bfd1d85cf52a8a36c83232cb001cc23db12';
  const dumpSeed = new Uint8Array(Buffer.from(dumpSeedHex, 'hex'));
  const dumpWallet = await initWalletWithSeed(dumpSeed, config);
  
  const recipe = await masterWallet.wallet.transferTransaction([{
    type: 'unshielded',
    outputs: [{
      type: ledger.nativeToken().raw,
      receiverAddress: await dumpWallet.wallet.unshielded.getAddress(),
      amount: 50000n * 10n**6n,
    }]
  }], {
    shieldedSecretKeys: masterWallet.shieldedSecretKeys,
    dustSecretKey: masterWallet.dustSecretKey
  }, { ttl: new Date(Date.now() + 30 * 60 * 1000) });
  
  const signed = await masterWallet.wallet.signRecipe(recipe, (p) => masterWallet.unshieldedKeystore.signData(p));
  const finalized = await masterWallet.wallet.finalizeRecipe(signed);
  await masterWallet.wallet.submitTransaction(finalized);
  console.log('Transferred NIGHT to DUMP_SEED');
  
  await waitForSync(dumpWallet.wallet);
  await waitForFunds(dumpWallet.wallet);
  await registerNightForDust(dumpWallet);
  await closeWallet(dumpWallet);
  await closeWallet(masterWallet);
  console.log('Done!');
  process.exit(0);
}
run().catch(console.error);
