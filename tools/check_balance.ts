import { WalletBuilder } from '@midnight-ntwrk/wallet-sdk-facade';
import { setNetworkId, NetworkId } from '@midnight-ntwrk/midnight-js-network-id';
import { pino } from 'pino';
import { Rx } from '@midnight-ntwrk/wallet-sdk-facade';

type CliOptions = {
  network: string;
  seedHex: string;
  indexerUrl: string;
  indexerWsUrl: string;
  proofServer: string;
  nodeUrl: string;
  syncTimeoutSeconds: number;
  logLevel: string;
  help: boolean;
};

function printHelp(): void {
  const lines = [
    'Midnight Wallet Balance Check',
    '',
    'Usage:',
    '  ts-node tools/check_balance.ts [options]',
    '',
    'Options:',
    '  --network <id>        preprod|preview|mainnet|undeployed (default: undeployed)',
    '  --seed <hex>          32-byte wallet seed hex (required if MIDNIGHT_WALLET_SEED unset)',
    '  --indexer-url <url>   Override indexer GraphQL HTTP URL',
    '  --indexer-ws-url <url> Override indexer GraphQL WS URL',
    '  --proof-server <url>  Override proof server URL',
    '  --node-url <url>      Override node relay WS URL',
    '  --sync-timeout <sec>  Sync timeout in seconds (default: 120)',
    '  --log-level <level>   Logger level (default: info)',
    '  --help                Show this help',
  ];

  process.stdout.write(`${lines.join('\n')}\n`);
}

function resolveNetworkId(network: string): NetworkId {
  const normalized = network.toLowerCase();
  const enumKeyByName: Record<string, string> = {
    preprod: 'Preprod',
    preview: 'Preview',
    mainnet: 'Mainnet',
    undeployed: 'Undeployed',
  };

  const key = enumKeyByName[normalized];
  const networkEnum = NetworkId as unknown as Record<string, NetworkId>;
  const value = key ? networkEnum[key] : undefined;

  if (!value) {
    throw new Error(`Unsupported network: ${network}`);
  }

  return value;
}

function getDefaultEndpoints(network: string): {
  indexerUrl: string;
  indexerWsUrl: string;
  proofServer: string;
  nodeUrl: string;
} {
  const map: Record<string, { indexerUrl: string; indexerWsUrl: string; proofServer: string; nodeUrl: string }> = {
    preprod: {
      indexerUrl: 'https://indexer.preprod.midnight.network/api/v3/graphql',
      indexerWsUrl: 'wss://indexer.preprod.midnight.network/api/v3/graphql/ws',
      proofServer: 'http://127.0.0.1:6300',
      nodeUrl: 'wss://rpc.preprod.midnight.network',
    },
    preview: {
      indexerUrl: 'https://indexer.preview.midnight.network/api/v3/graphql',
      indexerWsUrl: 'wss://indexer.preview.midnight.network/api/v3/graphql/ws',
      proofServer: 'http://127.0.0.1:6300',
      nodeUrl: 'wss://rpc.preview.midnight.network',
    },
    undeployed: {
      indexerUrl: 'http://127.0.0.1:8088/api/v3/graphql',
      indexerWsUrl: 'ws://127.0.0.1:8088/api/v3/graphql/ws',
      proofServer: 'http://127.0.0.1:6300',
      nodeUrl: 'ws://127.0.0.1:9944',
    },
    mainnet: {
      indexerUrl: '',
      indexerWsUrl: '',
      proofServer: 'http://127.0.0.1:6300',
      nodeUrl: '',
    },
  };

  return map[network] ?? map.undeployed;
}

function parseArgs(argv: string[]): CliOptions {
  const opts: CliOptions = {
    network: 'undeployed',
    seedHex: process.env.MIDNIGHT_WALLET_SEED || '',
    indexerUrl: '',
    indexerWsUrl: '',
    proofServer: '',
    nodeUrl: '',
    syncTimeoutSeconds: 120,
    logLevel: 'info',
    help: false,
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];

    if (arg === '--help' || arg === '-h') {
      opts.help = true;
      continue;
    }

    if (arg === '--network') {
      if (i + 1 >= argv.length) {
        throw new Error('--network requires a value');
      }
      opts.network = argv[++i];
      continue;
    }

    if (arg === '--seed') {
      if (i + 1 >= argv.length) {
        throw new Error('--seed requires a value');
      }
      opts.seedHex = argv[++i];
      continue;
    }

    if (arg === '--indexer-url') {
      if (i + 1 >= argv.length) {
        throw new Error('--indexer-url requires a value');
      }
      opts.indexerUrl = argv[++i];
      continue;
    }

    if (arg === '--indexer-ws-url') {
      if (i + 1 >= argv.length) {
        throw new Error('--indexer-ws-url requires a value');
      }
      opts.indexerWsUrl = argv[++i];
      continue;
    }

    if (arg === '--proof-server') {
      if (i + 1 >= argv.length) {
        throw new Error('--proof-server requires a value');
      }
      opts.proofServer = argv[++i];
      continue;
    }

    if (arg === '--node-url') {
      if (i + 1 >= argv.length) {
        throw new Error('--node-url requires a value');
      }
      opts.nodeUrl = argv[++i];
      continue;
    }

    if (arg === '--sync-timeout') {
      if (i + 1 >= argv.length) {
        throw new Error('--sync-timeout requires a value');
      }
      opts.syncTimeoutSeconds = Number(argv[++i]);
      continue;
    }

    if (arg === '--log-level') {
      if (i + 1 >= argv.length) {
        throw new Error('--log-level requires a value');
      }
      opts.logLevel = argv[++i];
      continue;
    }

    throw new Error(`Unknown argument: ${arg}`);
  }

  opts.network = opts.network.toLowerCase();

  if (opts.help) {
    return opts;
  }

  if (!Number.isInteger(opts.syncTimeoutSeconds) || opts.syncTimeoutSeconds <= 0) {
    throw new Error('sync-timeout must be a positive integer');
  }

  if (!opts.seedHex) {
    throw new Error('Missing wallet seed. Use --seed or set MIDNIGHT_WALLET_SEED');
  }

  const defaults = getDefaultEndpoints(opts.network);
  opts.indexerUrl = opts.indexerUrl || defaults.indexerUrl;
  opts.indexerWsUrl = opts.indexerWsUrl || defaults.indexerWsUrl;
  opts.proofServer = opts.proofServer || defaults.proofServer;
  opts.nodeUrl = opts.nodeUrl || defaults.nodeUrl;

  if (!opts.indexerUrl || !opts.indexerWsUrl || !opts.proofServer || !opts.nodeUrl) {
    throw new Error(
      `Missing endpoint configuration for network ${opts.network}. Provide --indexer-url --indexer-ws-url --proof-server --node-url`,
    );
  }

  return opts;
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    printHelp();
    return;
  }

  const logger = pino({ level: opts.logLevel });
  const networkId = resolveNetworkId(opts.network);
  setNetworkId(networkId);

  logger.info(
    {
      network: opts.network,
      indexerUrl: opts.indexerUrl,
      indexerWsUrl: opts.indexerWsUrl,
      proofServer: opts.proofServer,
      nodeUrl: opts.nodeUrl,
      syncTimeoutSeconds: opts.syncTimeoutSeconds,
    },
    'Building wallet for balance check',
  );

  let wallet: any;

  try {
    wallet = await WalletBuilder.build(
      opts.indexerUrl,
      opts.indexerWsUrl,
      opts.proofServer,
      opts.nodeUrl,
      opts.seedHex,
      networkId,
      opts.logLevel,
    );

    logger.info('Wallet built. Waiting for sync...');

    await Promise.race([
      Rx.firstValueFrom(
        wallet.state().pipe(
          Rx.filter((state: any) => Boolean(state?.isSynced)),
        ),
      ),
      new Promise((_, reject) => {
        setTimeout(() => reject(new Error(`Timed out waiting for sync (${opts.syncTimeoutSeconds}s)`)), opts.syncTimeoutSeconds * 1000);
      }),
    ]);

    const state = await Rx.firstValueFrom(wallet.state());
    const balances = state?.balances ?? {};

    logger.info(`NIGHT (unshielded): ${String(balances.unshielded ?? '0')}`);
    logger.info(`NIGHT (shielded): ${String(balances.shielded ?? '0')}`);
    logger.info(`DUST: ${String(balances.dust ?? '0')}`);

    if (state?.syncDiagnostics) {
      logger.info({ syncDiagnostics: state.syncDiagnostics }, 'Wallet sync diagnostics');
    }
  } finally {
    if (wallet && typeof wallet.stop === 'function') {
      await wallet.stop();
    }
  }

  process.exit(0);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
