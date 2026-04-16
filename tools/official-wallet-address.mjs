#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { Buffer } from 'node:buffer';
import { fileURLToPath, pathToFileURL } from 'node:url';

const DEFAULT_NETWORK = 'preprod';
const ALLOWED_NETWORKS = new Set(['preprod', 'preview', 'mainnet', 'undeployed']);

function printHelp() {
  const lines = [
    'Midnight Official Address Bridge',
    '',
    'Usage:',
    '  node tools/official-wallet-address.mjs [options]',
    '',
    'Options:',
    '  --network <id>   Network ID: preprod|preview|mainnet|undeployed (default: preprod)',
    '  --seed <hex>     Optional 32-byte seed hex (64 hex chars, optional 0x prefix)',
    '  --account <n>    HD account index (default: 0)',
    '  --index <n>      Address index (default: 0)',
    '  --register-dust  Submit dust registration tx for available NIGHT UTXOs',
    '  --dust-utxo-limit <n>  Max unregistered NIGHT UTXOs to register in one tx (default: 1)',
    '  --proof-server <url>  Override proof server URL (default by network)',
    '  --node-url <url>      Override node RPC URL (default by network)',
    '  --indexer-url <url>   Override indexer GraphQL URL (default by network)',
    '  --indexer-ws-url <url> Override indexer WS URL (default by network)',
    '  --sync-timeout <sec>  Wait timeout for wallet sync when registering dust (default: 120)',
    '  --help           Show this help',
  ];
  process.stdout.write(lines.join('\n') + '\n');
}

function parseArgs(argv) {
  const opts = {
    network: DEFAULT_NETWORK,
    seedHex: '',
    account: 0,
    index: 0,
    registerDust: false,
    dustUtxoLimit: 1,
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

    if (arg === '--account') {
      if (i + 1 >= argv.length) {
        throw new Error('--account requires a value');
      }
      opts.account = Number(argv[++i]);
      continue;
    }

    if (arg === '--index') {
      if (i + 1 >= argv.length) {
        throw new Error('--index requires a value');
      }
      opts.index = Number(argv[++i]);
      continue;
    }

    if (arg === '--register-dust') {
      opts.registerDust = true;
      continue;
    }

    if (arg === '--dust-utxo-limit') {
      if (i + 1 >= argv.length) {
        throw new Error('--dust-utxo-limit requires a value');
      }
      opts.dustUtxoLimit = Number(argv[++i]);
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
  const addressMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-address-format', 'dist', 'index.js'));
  const ledgerMod = await import(resolve('@midnight-ntwrk', 'ledger-v7', 'midnight_ledger_wasm_fs.js'));
  const undiciMod = await import(resolve('undici', 'index.js'));
  const wsMod = await import(resolve('ws', 'wrapper.mjs'));

  return {
    setNetworkId: networkMod.setNetworkId,
    getNetworkId: networkMod.getNetworkId,
    HDWallet: hdMod.HDWallet,
    Roles: hdMod.Roles,
    generateRandomSeed: hdMod.generateRandomSeed,
    createKeystore: unshieldedMod.createKeystore,
    InMemoryTransactionHistoryStorage: unshieldedMod.InMemoryTransactionHistoryStorage,
    PublicKey: unshieldedMod.PublicKey,
    UnshieldedWallet: unshieldedMod.UnshieldedWallet,
    WalletFacade: facadeMod.WalletFacade,
    ShieldedWallet: shieldedMod.ShieldedWallet,
    DustWallet: dustMod.DustWallet,
    MidnightBech32m: addressMod.MidnightBech32m,
    ShieldedAddress: addressMod.ShieldedAddress,
    ShieldedCoinPublicKey: addressMod.ShieldedCoinPublicKey,
    ShieldedEncryptionPublicKey: addressMod.ShieldedEncryptionPublicKey,
    DustAddress: addressMod.DustAddress,
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

function emitResultAndExit(payload, exitCode) {
  process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`);
  process.exit(exitCode);
}

function timeout(ms, message) {
  return new Promise((_, reject) => {
    setTimeout(() => reject(new Error(message)), ms);
  });
}

function withTimeout(promise, ms, message) {
  return Promise.race([promise, timeout(ms, message)]);
}

function waitForState(observable, predicate, timeoutMs, timeoutMessage) {
  return new Promise((resolve, reject) => {
    let settled = false;
    const timer = setTimeout(() => {
      if (settled) {
        return;
      }
      settled = true;
      subscription.unsubscribe();
      reject(new Error(timeoutMessage));
    }, timeoutMs);

    const subscription = observable.subscribe({
      next: (value) => {
        if (settled) {
          return;
        }

        let matched = false;
        try {
          matched = Boolean(predicate(value));
        } catch {
          matched = false;
        }

        if (!matched) {
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

function firstObservedState(observable, timeoutMs) {
  return waitForState(
    observable,
    () => true,
    timeoutMs,
    `Timed out waiting for first wallet state (${Math.floor(timeoutMs / 1000)}s)`,
  );
}

function normalizeToDate(value) {
  if (!value) {
    return null;
  }

  if (value instanceof Date) {
    return Number.isNaN(value.getTime()) ? null : value;
  }

  if (typeof value === 'number') {
    const d = new Date(value);
    return Number.isNaN(d.getTime()) ? null : d;
  }

  if (typeof value === 'bigint') {
    const asNumber = Number(value);
    if (!Number.isFinite(asNumber)) {
      return null;
    }
    const d = new Date(asNumber);
    return Number.isNaN(d.getTime()) ? null : d;
  }

  if (typeof value === 'string') {
    const d = new Date(value);
    return Number.isNaN(d.getTime()) ? null : d;
  }

  return null;
}

function computeDustAvailabilityEstimate(availableCoins, dustGracePeriodSeconds) {
  if (!Number.isFinite(dustGracePeriodSeconds) || dustGracePeriodSeconds <= 0) {
    return {
      estimatedDustEligibleAt: '',
      estimatedDustWaitSeconds: 0,
    };
  }

  const registeredCoinTimes = availableCoins
    .filter((coin) => coin?.meta?.registeredForDustGeneration === true)
    .map((coin) => normalizeToDate(coin?.meta?.ctime))
    .filter((d) => d instanceof Date);

  if (registeredCoinTimes.length === 0) {
    return {
      estimatedDustEligibleAt: '',
      estimatedDustWaitSeconds: 0,
    };
  }

  const firstEligibleMs = Math.min(
    ...registeredCoinTimes.map((d) => d.getTime() + (dustGracePeriodSeconds * 1000)),
  );

  const nowMs = Date.now();
  const remainingSeconds = Math.max(0, Math.ceil((firstEligibleMs - nowMs) / 1000));

  return {
    estimatedDustEligibleAt: new Date(firstEligibleMs).toISOString(),
    estimatedDustWaitSeconds: remainingSeconds,
  };
}

async function registerDustIfRequested(opts, modules, seeds, dustAddress) {
  const result = {
    requested: opts.registerDust,
    attempted: false,
    success: false,
    txId: '',
    message: '',
    strategy: '',
    dustGracePeriodSeconds: 0,
    estimatedDustEligibleAt: '',
    estimatedDustWaitSeconds: 0,
    availableUtxoCount: 0,
    unregisteredUtxoCount: 0,
    registrationBatchSize: 0,
  };

  if (!opts.registerDust) {
    result.success = true;
    result.message = 'Dust registration not requested';
    return result;
  }

  if (opts.network === 'mainnet') {
    result.message = 'Dust registration is disabled for mainnet in this tool';
    return result;
  }

  const defaultEndpoints = getDefaultEndpoints(opts.network);
  const endpoints = {
    indexerUrl: opts.indexerUrl || defaultEndpoints.indexerUrl,
    indexerWsUrl: opts.indexerWsUrl || defaultEndpoints.indexerWsUrl,
    nodeUrl: opts.nodeUrl || defaultEndpoints.nodeUrl,
    proofServer: opts.proofServer || defaultEndpoints.proofServer,
  };

  if (!endpoints.indexerUrl || !endpoints.indexerWsUrl || !endpoints.nodeUrl || !endpoints.proofServer) {
    result.message = 'Missing network endpoints for dust registration';
    return result;
  }

  const networkId = modules.getNetworkId();
  const { shieldedConfig, unshieldedConfig, dustConfig } = buildWalletConfigs(networkId, endpoints, modules);

  const shieldedSecretKeys = modules.ledger.ZswapSecretKeys.fromSeed(seeds.zswapSeed);
  const dustSecretKey = modules.ledger.DustSecretKey.fromSeed(seeds.dustSeed);
  const unshieldedKeystore = modules.createKeystore(seeds.nightExternalSeed, networkId);

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
    let state;
    let syncError = '';

    try {
      state = await waitForState(
        wallet.state(),
        (s) => Boolean(s?.isSynced),
        syncTimeoutMs,
        `Timed out waiting for wallet sync (${opts.syncTimeoutSeconds}s)`,
      );
    } catch (error) {
      syncError = error instanceof Error ? error.message : String(error);
      state = await firstObservedState(wallet.state(), 15_000);
    }

    const availableCoins =
      state?.unshielded?.capabilities?.coinsAndBalances?.getAvailableCoins?.(state?.unshielded?.state) ??
      state?.unshielded?.availableCoins ??
      [];

    const dustGracePeriodSeconds = Number(
      modules.ledger.LedgerParameters.initialParameters().dust.dustGracePeriodSeconds ?? 0n,
    );
    result.dustGracePeriodSeconds = Number.isFinite(dustGracePeriodSeconds) ? dustGracePeriodSeconds : 0;

    const estimate = computeDustAvailabilityEstimate(availableCoins, result.dustGracePeriodSeconds);
    result.estimatedDustEligibleAt = estimate.estimatedDustEligibleAt;
    result.estimatedDustWaitSeconds = estimate.estimatedDustWaitSeconds;

    const nightUtxos = availableCoins.filter((coin) => coin?.meta?.registeredForDustGeneration === false);
    result.availableUtxoCount = availableCoins.length;
    result.unregisteredUtxoCount = nightUtxos.length;

    if (nightUtxos.length === 0) {
      const hasFunds = availableCoins.length > 0;
      const dustBalance = state?.dust?.walletBalance?.(new Date()) ?? 0n;
      if (dustBalance > 0n) {
        result.success = true;
        result.message = `Dust already available (${dustBalance.toString()}), no registration needed`;
      } else if (hasFunds) {
        result.message = 'All NIGHT UTXOs are already registered; waiting for DUST generation';
      } else {
        result.message = 'No NIGHT UTXO available for registration. Fund unshielded address first.';
      }

      if (syncError) {
        result.message = `${result.message} Sync note: ${syncError}`;
      }

      return result;
    }

    result.attempted = true;
    const opTimeoutMs = Math.max(120, opts.syncTimeoutSeconds * 5) * 1000;
    const registrationBatchSize = Math.min(Math.max(1, opts.dustUtxoLimit), nightUtxos.length);
    const utxosToRegister = nightUtxos.slice(0, registrationBatchSize);
    result.registrationBatchSize = registrationBatchSize;

    let recipe;
    const signingKey = unshieldedKeystore.getPublicKey();
    const signRegistration = (payload) => unshieldedKeystore.signData(payload);

    let registerError;

    try {
      recipe = await withTimeout(
        wallet.registerNightUtxosForDustGeneration(
          utxosToRegister,
          signingKey,
          signRegistration,
        ),
        opTimeoutMs,
        `Timed out building dust registration recipe (${Math.floor(opTimeoutMs / 1000)}s)`,
      );
      result.strategy = 'registerNightUtxosForDustGeneration';
    } catch (error) {
      registerError = error;
      const message = error instanceof Error ? error.message : String(error);

      if (/argument|parameter|arity|expects/i.test(message)) {
        try {
          recipe = await withTimeout(
            wallet.registerNightUtxosForDustGeneration(
              utxosToRegister,
              signingKey,
              signRegistration,
              dustAddress,
            ),
            opTimeoutMs,
            `Timed out building dust registration recipe (${Math.floor(opTimeoutMs / 1000)}s)`,
          );
          result.strategy = 'registerNightUtxosForDustGeneration+receiver';
          registerError = undefined;
        } catch (errorWithReceiver) {
          registerError = errorWithReceiver;
        }
      }
    }

    if (!recipe && typeof wallet.createDustActionTransaction === 'function') {
      const fallbackTransaction = await withTimeout(
        wallet.createDustActionTransaction(
          { type: 'registration', dustReceiverAddress: dustAddress },
          utxosToRegister,
          signingKey,
          signRegistration,
        ),
        opTimeoutMs,
        `Timed out building fallback dust registration transaction (${Math.floor(opTimeoutMs / 1000)}s)`,
      );

      recipe = {
        type: 'UNPROVEN_TRANSACTION',
        transaction: fallbackTransaction,
      };
      result.strategy = 'createDustActionTransaction';
    }

    if (!recipe) {
      throw registerError ?? new Error('Unable to build dust registration recipe');
    }

    const finalizePromise =
      recipe && typeof recipe === 'object' && recipe.transaction && typeof wallet.finalizeTransaction === 'function'
        ? wallet.finalizeTransaction(recipe.transaction)
        : wallet.finalizeRecipe(recipe);

    const finalized = await withTimeout(finalizePromise, opTimeoutMs, `Timed out finalizing dust registration transaction (${Math.floor(opTimeoutMs / 1000)}s)`);

    const txId = await withTimeout(
      wallet.submitTransaction(finalized),
      opTimeoutMs,
      `Timed out submitting dust registration transaction (${Math.floor(opTimeoutMs / 1000)}s)`,
    );

    let dustBalanceAfter = state?.dust?.walletBalance?.(new Date()) ?? 0n;
    try {
      const dustState = await waitForState(
        wallet.state(),
        (s) => (s?.dust?.walletBalance?.(new Date()) ?? 0n) > 0n,
        Math.max(60, opts.syncTimeoutSeconds) * 1000,
        `Timed out waiting for DUST balance update (${Math.max(60, opts.syncTimeoutSeconds)}s)`,
      );
      dustBalanceAfter = dustState?.dust?.walletBalance?.(new Date()) ?? dustBalanceAfter;
    } catch {
      // DUST may appear with delay after registration submission.
    }

    result.success = true;
    result.txId = String(txId);
    result.message = `Submitted dust registration for ${utxosToRegister.length}/${nightUtxos.length} unregistered NIGHT UTXO(s). Current DUST balance: ${dustBalanceAfter.toString()}`;
    return result;
  } catch (error) {
    result.message = error instanceof Error ? error.message : String(error);
    return result;
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
  }
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

  validateIndex('account', opts.account);
  validateIndex('index', opts.index);
  if (!Number.isInteger(opts.dustUtxoLimit) || opts.dustUtxoLimit < 1 || opts.dustUtxoLimit > 1000) {
    throw new Error('dust-utxo-limit must be an integer in range [1, 1000]');
  }
  validateIndex('sync-timeout', opts.syncTimeoutSeconds);

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

  const seedHex = opts.seedHex
    ? normalizeSeedHex(opts.seedHex)
    : Buffer.from(modules.generateRandomSeed()).toString('hex');

  const hd = modules.HDWallet.fromSeed(Buffer.from(seedHex, 'hex'));
  if (hd.type !== 'seedOk') {
    throw new Error('Failed to initialize HD wallet from seed');
  }

  const derived = hd.hdWallet
    .selectAccount(opts.account)
    .selectRoles([modules.Roles.NightExternal, modules.Roles.Zswap, modules.Roles.Dust])
    .deriveKeysAt(opts.index);

  if (derived.type !== 'keysDerived') {
    hd.hdWallet.clear();
    throw new Error('Failed to derive wallet role keys');
  }

  const nightExternalSeed = derived.keys[modules.Roles.NightExternal];
  const zswapSeed = derived.keys[modules.Roles.Zswap];
  const dustSeed = derived.keys[modules.Roles.Dust];

  const keystore = modules.createKeystore(nightExternalSeed, networkId);
  const unshieldedAddress = String(keystore.getBech32Address());

  const zswapSecretKeys = modules.ledger.ZswapSecretKeys.fromSeed(zswapSeed);
  const dustSecretKey = modules.ledger.DustSecretKey.fromSeed(dustSeed);

  const shieldedCoinPub = modules.ShieldedCoinPublicKey.fromHexString(zswapSecretKeys.coinPublicKey);
  const shieldedEncPub = modules.ShieldedEncryptionPublicKey.fromHexString(zswapSecretKeys.encryptionPublicKey);
  const shieldedAddress = modules.MidnightBech32m.encode(
    networkId,
    new modules.ShieldedAddress(shieldedCoinPub, shieldedEncPub),
  ).toString();

  const dustAddress = modules.DustAddress.encodePublicKey(networkId, dustSecretKey.publicKey);

  const dustRegistration = await registerDustIfRequested(
    opts,
    modules,
    {
      nightExternalSeed,
      zswapSeed,
      dustSeed,
    },
    dustAddress,
  );

  const derivationPath = {
    unshielded: `m/${opts.account}/${Number(modules.Roles.NightExternal)}/${opts.index}`,
    shielded: `m/${opts.account}/${Number(modules.Roles.Zswap)}/${opts.index}`,
    dust: `m/${opts.account}/${Number(modules.Roles.Dust)}/${opts.index}`,
  };

  try {
    zswapSecretKeys.clear();
  } catch {
    // no-op
  }
  try {
    dustSecretKey.clear();
  } catch {
    // no-op
  }

  hd.hdWallet.clear();

  emitResultAndExit(
    {
      success: true,
      source: 'official-midnight-sdk',
      network: networkId,
      seedHex,
      derivationPath,
      unshieldedAddress,
      shieldedAddress,
      dustAddress,
      dustRegistrationRequested: opts.registerDust,
      dustRegistrationAttempted: dustRegistration.attempted,
      dustRegistrationSuccess: dustRegistration.success,
      dustRegistrationMessage: dustRegistration.message,
      dustRegistrationStrategy: dustRegistration.strategy,
      dustRegistrationTxId: dustRegistration.txId,
      dustGracePeriodSeconds: dustRegistration.dustGracePeriodSeconds,
      estimatedDustEligibleAt: dustRegistration.estimatedDustEligibleAt,
      estimatedDustWaitSeconds: dustRegistration.estimatedDustWaitSeconds,
      dustRegistrationAvailableUtxoCount: dustRegistration.availableUtxoCount,
      dustRegistrationUnregisteredUtxoCount: dustRegistration.unregisteredUtxoCount,
      dustRegistrationBatchSize: dustRegistration.registrationBatchSize,
    },
    0,
  );
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
