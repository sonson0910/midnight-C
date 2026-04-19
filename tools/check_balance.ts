import { WalletBuilder } from '@midnight-ntwrk/wallet-sdk-facade';
import { setNetworkId, NetworkId } from '@midnight-ntwrk/midnight-js-network-id';
import { pino } from 'pino';
import { Rx } from '@midnight-ntwrk/wallet-sdk-facade';

async function main() {
  const logger = pino({ level: 'info' });
  const seedHex = '5c4ade190eb78ef0d3050eef47171bfd1d85cf52a8a36c83232cb001cc23db12';
  
  setNetworkId(NetworkId.Undeployed);
  
  logger.info('Building wallet for check...');
  const wallet = await WalletBuilder.build(
    'http://127.0.0.1:8088/api/v3/graphql',
    'ws://127.0.0.1:8088/api/v3/graphql/ws',
    'http://127.0.0.1:6300',
    'ws://127.0.0.1:9944',
    seedHex,
    NetworkId.Undeployed,
    'info'
  );
  
  logger.info('Wallet built. Waiting for sync...');
  await Rx.firstValueFrom(
    wallet.state().pipe(
      Rx.filter((state) => state.isSynced)
    )
  );

  const state = await wallet.state().pipe(Rx.firstValueFrom());
  const balances = state.balances;
  logger.info(`NIGHT (unshielded): ${balances.unshielded}`);
  logger.info(`NIGHT (shielded): ${balances.shielded}`);
  logger.info(`DUST: ${balances.dust}`);

  process.exit(0);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
