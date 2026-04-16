#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { Buffer } from 'node:buffer';
import { fileURLToPath, pathToFileURL } from 'node:url';

const DEFAULT_NETWORK = 'preprod';
const ALLOWED_NETWORKS = new Set(['preprod', 'preview', 'mainnet', 'undeployed']);

function printHelp() {
  const lines = [
    'Midnight Official NIGHT Transfer',
    '',
    'Usage:',
    '  node tools/official-transfer-night.mjs --seed <hex> --to <mn_addr...> --amount <units> [options]',
    '',
    'Options:',
    '  --network <id>        Network ID: preprod|preview|mainnet|undeployed (default: preprod)',
    '  --seed <hex>          Required 32-byte source seed hex (64 hex chars, optional 0x prefix)',
    '  --to <address>        Required destination unshielded address',
    '  --amount <units>      Required amount in smallest units (positive integer)',
    '  --proof-server <url>  Override proof server URL (default by network)',
    '  --node-url <url>      Override node RPC URL (default by network)',
    '  --indexer-url <url>   Override indexer GraphQL URL (default by network)',
    '  --indexer-ws-url <url> Override indexer WS URL (default by network)',
    '  --sync-timeout <sec>  Sync timeout seconds (default: 120)',
    '  --help                Show this help',
  ];
  process.stdout.write(lines.join('\n') + '\n');
}

function parseArgs(argv) {
  const opts = {
    network: DEFAULT_NETWORK,
    seedHex: '',
    toAddress: '',
    amount: '',
    proofServer: '',
    nodeUrl: '',
    indexerUrl: '',
    indexerWsUrl: '',
    syncTimeoutSeconds: 120,
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

    if (arg === '--to') {
      if (i + 1 >= argv.length) {
        throw new Error('--to requires a value');
      }
      opts.toAddress = argv[++i];
      continue;
    }

    if (arg === '--amount') {
      if (i + 1 >= argv.length) {
        throw new Error('--amount requires a value');
      }
      opts.amount = argv[++i];
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

    if (arg === '--sync-timeout') {
      if (i + 1 >= argv.length) {
        throw new Error('--sync-timeout requires a value');
      }
      opts.syncTimeoutSeconds = Number(argv[++i]);
      continue;
    }

    throw new Error(`Unknown argument: ${arg}`);
  }

  return opts;
}

function normalizeSeedHex(seedHex) {
  const raw = seedHex.startsWith('0x') || seedHex.startsWith('0X')
    ? seedHex.slice(2)
    : seedHex;

  if (!/^[0-9a-fA-F]{64}$/.test(raw)) {
    throw new Error('Seed must be exactly 32 bytes (64 hex chars)');
  }

  return raw.toLowerCase();
}

function parseAmount(amount) {
  if (!/^[0-9]+$/.test(amount)) {
    throw new Error('Amount must be a positive integer string');
  }
  const parsed = BigInt(amount);
  if (parsed <= 0n) {
    throw new Error('Amount must be > 0');
  }
  return parsed;
}

function validateIndex(name, value) {
  if (!Number.isInteger(value) || value < 0 || value > 0x7fffffff) {
    throw new Error(`${name} must be an integer in range [0, 2147483647]`);
  }
}

async function loadOfficialModules(nodeModulesRoot) {
  const resolve = (...parts) => pathToFileURL(path.join(nodeModulesRoot, ...parts)).href;

  const networkMod = await import(resolve('@midnight-ntwrk', 'midnight-js-network-id', 'dist', 'index.mjs'));
  const hdMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-hd', 'dist', 'index.js'));
  const unshieldedMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-unshielded-wallet', 'dist', 'index.js'));
  const facadeMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-facade', 'dist', 'index.js'));
  const shieldedMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-shielded', 'dist', 'index.js'));
  const dustMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-dust-wallet', 'dist', 'index.js'));
  const ledgerMod = await import(resolve('@midnight-ntwrk', 'ledger-v7', 'midnight_ledger_wasm_fs.js'));
  const undiciMod = await import(resolve('undici', 'index.js'));
  const wsMod = await import(resolve('ws', 'wrapper.mjs'));

  return {
    setNetworkId: networkMod.setNetworkId,
    getNetworkId: networkMod.getNetworkId,
    HDWallet: hdMod.HDWallet,
    Roles: hdMod.Roles,
    createKeystore: unshieldedMod.createKeystore,
    InMemoryTransactionHistoryStorage: unshieldedMod.InMemoryTransactionHistoryStorage,
    PublicKey: unshieldedMod.PublicKey,
    UnshieldedWallet: unshieldedMod.UnshieldedWallet,
    WalletFacade: facadeMod.WalletFacade,
    ShieldedWallet: shieldedMod.ShieldedWallet,
    DustWallet: dustMod.DustWallet,
    ledger: ledgerMod,
    Agent: undiciMod.Agent,
    setGlobalDispatcher: undiciMod.setGlobalDispatcher,
    WebSocket: wsMod.WebSocket,
  };
}

function getDefaultEndpoints(network) {
  const map = {
    preprod: {
      indexerUrl: 'https://indexer.preprod.midnight.network/api/v3/graphql',
      indexerWsUrl: 'wss://indexer.preprod.midnight.network/api/v3/graphql/ws',
      nodeUrl: 'https://rpc.preprod.midnight.network',
      proofServer: 'http://127.0.0.1:6300',
    },
    preview: {
      indexerUrl: 'https://indexer.preview.midnight.network/api/v3/graphql',
      indexerWsUrl: 'wss://indexer.preview.midnight.network/api/v3/graphql/ws',
      nodeUrl: 'https://rpc.preview.midnight.network',
      proofServer: 'http://127.0.0.1:6300',
    },
    undeployed: {
      indexerUrl: 'http://127.0.0.1:8088/api/v3/graphql',
      indexerWsUrl: 'ws://127.0.0.1:8088/api/v3/graphql/ws',
      nodeUrl: 'http://127.0.0.1:9944',
      proofServer: 'http://127.0.0.1:6300',
    },
    mainnet: {
      indexerUrl: '',
      indexerWsUrl: '',
      nodeUrl: '',
      proofServer: 'http://127.0.0.1:6300',
    },
  };

  return map[network] ?? map.preprod;
}

function buildWalletConfigs(networkId, endpoints, modules) {
  return {
    shieldedConfig: {
      networkId,
      indexerClientConnection: {
        indexerHttpUrl: endpoints.indexerUrl,
        indexerWsUrl: endpoints.indexerWsUrl,
      },
      provingServerUrl: new URL(endpoints.proofServer),
      relayURL: new URL(endpoints.nodeUrl.replace(/^http/, 'ws')),
    },
    unshieldedConfig: {
      networkId,
      indexerClientConnection: {
        indexerHttpUrl: endpoints.indexerUrl,
        indexerWsUrl: endpoints.indexerWsUrl,
      },
      txHistoryStorage: new modules.InMemoryTransactionHistoryStorage(),
    },
    dustConfig: {
      networkId,
      costParameters: {
        additionalFeeOverhead: 300000000000000n,
        feeBlocksMargin: 5,
      },
      indexerClientConnection: {
        indexerHttpUrl: endpoints.indexerUrl,
        indexerWsUrl: endpoints.indexerWsUrl,
      },
      provingServerUrl: new URL(endpoints.proofServer),
      relayURL: new URL(endpoints.nodeUrl.replace(/^http/, 'ws')),
    },
  };
}

function timeout(ms, message) {
  return new Promise((_, reject) => {
    setTimeout(() => reject(new Error(message)), ms);
  });
}

function firstObservedState(observable, timeoutMs) {
  return new Promise((resolve, reject) => {
    let settled = false;
    const timer = setTimeout(() => {
      if (settled) {
        return;
      }
      settled = true;
      subscription.unsubscribe();
      reject(new Error(`Timed out waiting for first wallet state (${Math.floor(timeoutMs / 1000)}s)`));
    }, timeoutMs);

    const subscription = observable.subscribe({
      next: (value) => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timer);
        subscription.unsubscribe();
        resolve(value);
      },
      error: (error) => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timer);
        subscription.unsubscribe();
        reject(error);
      },
    });
  });
}

function emitResultAndExit(payload, exitCode) {
  process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`);
  process.exit(exitCode);
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    printHelp();
    return;
  }

  if (!ALLOWED_NETWORKS.has(opts.network)) {
    throw new Error(`Unsupported network: ${opts.network}`);
  }

  if (!opts.seedHex) {
    throw new Error('--seed is required');
  }

  if (!opts.toAddress) {
    throw new Error('--to is required');
  }

  if (!opts.amount) {
    throw new Error('--amount is required');
  }

  validateIndex('sync-timeout', opts.syncTimeoutSeconds);

  const normalizedSeed = normalizeSeedHex(opts.seedHex);
  const amount = parseAmount(opts.amount);

  const thisFile = fileURLToPath(import.meta.url);
  const thisDir = path.dirname(thisFile);
  const repoRoot = path.resolve(thisDir, '..');
  const nodeModulesRoot = path.join(repoRoot, 'midnight-research', 'node_modules');

  if (!fs.existsSync(nodeModulesRoot)) {
    throw new Error('Official SDK dependencies not found. Run: cd midnight-research && npm install');
  }

  const modules = await loadOfficialModules(nodeModulesRoot);

  if (typeof modules.WebSocket === 'function') {
    globalThis.WebSocket = modules.WebSocket;
  }

  if (typeof modules.setGlobalDispatcher === 'function' && typeof modules.Agent === 'function') {
    modules.setGlobalDispatcher(new modules.Agent({
      headersTimeout: 3_600_000,
      bodyTimeout: 3_600_000,
      connectTimeout: 60_000,
    }));
  }

  modules.setNetworkId(opts.network);
  const networkId = modules.getNetworkId();

  const hd = modules.HDWallet.fromSeed(Buffer.from(normalizedSeed, 'hex'));
  if (hd.type !== 'seedOk') {
    throw new Error('Failed to initialize HD wallet from seed');
  }

  const derived = hd.hdWallet
    .selectAccount(0)
    .selectRoles([modules.Roles.NightExternal, modules.Roles.Zswap, modules.Roles.Dust])
    .deriveKeysAt(0);

  if (derived.type !== 'keysDerived') {
    hd.hdWallet.clear();
    throw new Error('Failed to derive wallet role keys');
  }

  const nightExternalSeed = derived.keys[modules.Roles.NightExternal];
  const zswapSeed = derived.keys[modules.Roles.Zswap];
  const dustSeed = derived.keys[modules.Roles.Dust];

  const defaultEndpoints = getDefaultEndpoints(opts.network);
  const endpoints = {
    indexerUrl: opts.indexerUrl || defaultEndpoints.indexerUrl,
    indexerWsUrl: opts.indexerWsUrl || defaultEndpoints.indexerWsUrl,
    nodeUrl: opts.nodeUrl || defaultEndpoints.nodeUrl,
    proofServer: opts.proofServer || defaultEndpoints.proofServer,
  };

  if (!endpoints.indexerUrl || !endpoints.indexerWsUrl || !endpoints.nodeUrl || !endpoints.proofServer) {
    throw new Error(`Missing endpoints for network ${opts.network}`);
  }

  const { shieldedConfig, unshieldedConfig, dustConfig } = buildWalletConfigs(networkId, endpoints, modules);

  const shieldedSecretKeys = modules.ledger.ZswapSecretKeys.fromSeed(zswapSeed);
  const dustSecretKey = modules.ledger.DustSecretKey.fromSeed(dustSeed);
  const unshieldedKeystore = modules.createKeystore(nightExternalSeed, networkId);

  const sourceAddress = String(unshieldedKeystore.getBech32Address());

  const shieldedWallet = modules.ShieldedWallet(shieldedConfig).startWithSecretKeys(shieldedSecretKeys);
  const unshieldedWallet = modules.UnshieldedWallet(unshieldedConfig).startWithPublicKey(
    modules.PublicKey.fromKeyStore(unshieldedKeystore),
  );
  const dustWallet = modules.DustWallet(dustConfig).startWithSecretKey(
    dustSecretKey,
    modules.ledger.LedgerParameters.initialParameters().dust,
  );

  const wallet = new modules.WalletFacade(shieldedWallet, unshieldedWallet, dustWallet);

  try {
    await wallet.start(shieldedSecretKeys, dustSecretKey);

    const syncTimeoutMs = Math.max(10, opts.syncTimeoutSeconds) * 1000;
    let synced = true;
    let syncError = '';
    let state;

    try {
      state = await Promise.race([
        wallet.waitForSyncedState(),
        timeout(syncTimeoutMs, `Timed out waiting for wallet sync (${opts.syncTimeoutSeconds}s)`),
      ]);
    } catch (error) {
      synced = false;
      syncError = error instanceof Error ? error.message : String(error);
      state = await firstObservedState(wallet.state(), 15_000);
    }

    const tokenType = modules.ledger.unshieldedToken().raw;
    const unshieldedBalance = state.unshielded.balances[tokenType] ?? 0n;
    const dustBalance = state.dust.walletBalance(new Date());

    if (unshieldedBalance < amount) {
      emitResultAndExit(
        {
          success: false,
          source: 'official-midnight-sdk',
          network: networkId,
          sourceAddress,
          destinationAddress: opts.toAddress,
          amount: amount.toString(),
          synced,
          syncError,
          unshieldedBalance: unshieldedBalance.toString(),
          dustBalance: dustBalance.toString(),
          error: 'Insufficient unshielded NIGHT balance',
        },
        2,
      );
      return;
    }

    const recipe = await wallet.transferTransaction(
      [
        {
          type: 'unshielded',
          outputs: [
            {
              type: tokenType,
              receiverAddress: opts.toAddress,
              amount,
            },
          ],
        },
      ],
      {
        shieldedSecretKeys,
        dustSecretKey,
      },
      {
        ttl: new Date(Date.now() + 60 * 60 * 1000),
        payFees: true,
      },
    );

    const finalized = await wallet.finalizeRecipe(recipe);
    const txId = await wallet.submitTransaction(finalized);

    emitResultAndExit(
      {
        success: true,
        source: 'official-midnight-sdk',
        network: networkId,
        sourceAddress,
        destinationAddress: opts.toAddress,
        amount: amount.toString(),
        synced,
        syncError,
        txId: String(txId),
        unshieldedBalanceBefore: unshieldedBalance.toString(),
        dustBalanceBefore: dustBalance.toString(),
      },
      0,
    );
  } finally {
    try {
      await wallet.stop();
    } catch {
      // no-op
    }
    try {
      shieldedSecretKeys.clear();
    } catch {
      // no-op
    }
    try {
      dustSecretKey.clear();
    } catch {
      // no-op
    }
    try {
      hd.hdWallet.clear();
    } catch {
      // no-op
    }
  }
}

main().catch((error) => {
  emitResultAndExit(
    {
      success: false,
      source: 'official-midnight-sdk',
      error: error instanceof Error ? error.message : String(error),
    },
    1,
  );
});
