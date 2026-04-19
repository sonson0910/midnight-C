#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { Buffer } from 'node:buffer';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';

const DEFAULT_NETWORK = 'preview';
const ALLOWED_NETWORKS = new Set(['preprod', 'preview', 'mainnet', 'undeployed']);

function printHelp() {
  const lines = [
    'Midnight Official NIGHT Transfer',
    '',
    'Usage:',
    '  node tools/official-transfer-night.mjs --seed <hex> --to <mn_addr...> --amount <units> [options]',
    '',
    'Options:',
    '  --network <id>        Network ID: preprod|preview|mainnet|undeployed (default: preview)',
    '  --seed <hex>          Required 32-byte source seed hex (64 hex chars, optional 0x prefix)',
    '  --to <address>        Required destination unshielded address',
    '  --amount <units>      Required amount in smallest units (positive integer)',
    '  --proof-server <url>  Override proof server URL (default by network)',
    '  --node-url <url>      Override node RPC URL (default by network)',
    '  --indexer-url <url>   Override indexer GraphQL URL (default by network)',
    '  --indexer-ws-url <url> Override indexer WS URL (default by network)',
    '  --state-cache <path>  Wallet state cache file path (default: auto under .cache)',
    '  --no-state-cache    Disable state cache restore/save for this run',
    '  --sync-timeout <sec>  Sync timeout seconds (default: 120)',
    '  --dust-wait <sec>     Wait seconds for DUST balance to appear (default: sync-timeout)',
    '  --settle-wait <sec>   Wait seconds for unshielded NIGHT UTXOs to settle (default: 90)',
    '  --submit-retries <n>  Retry failed transfer submission n times (default: 1)',
    '  --dust-utxo-limit <n> Max unregistered NIGHT UTXOs to auto-register per tx (default: 1)',
    '  --dust-deregister-retries <n> Retry auto-deregistration when submit fails (default: 3)',
    '  --allow-self-transfer Allow source==destination transfer (default: blocked)',
    '  --no-auto-register-dust Disable automatic DUST registration before transfer',
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
    stateCache: '',
    useStateCache: true,
    syncTimeoutSeconds: 120,
    dustWaitSeconds: null,
    settleWaitSeconds: 90,
    submitRetries: 1,
    dustUtxoLimit: 1,
    dustDeregisterRetries: 3,
    allowSelfTransfer: false,
    autoRegisterDust: true,
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

    if (arg === '--state-cache') {
      if (i + 1 >= argv.length) {
        throw new Error('--state-cache requires a value');
      }
      opts.stateCache = argv[++i];
      continue;
    }

    if (arg === '--no-state-cache') {
      opts.useStateCache = false;
      continue;
    }

    if (arg === '--sync-timeout') {
      if (i + 1 >= argv.length) {
        throw new Error('--sync-timeout requires a value');
      }
      opts.syncTimeoutSeconds = Number(argv[++i]);
      continue;
    }

    if (arg === '--dust-wait') {
      if (i + 1 >= argv.length) {
        throw new Error('--dust-wait requires a value');
      }
      opts.dustWaitSeconds = Number(argv[++i]);
      continue;
    }

    if (arg === '--settle-wait') {
      if (i + 1 >= argv.length) {
        throw new Error('--settle-wait requires a value');
      }
      opts.settleWaitSeconds = Number(argv[++i]);
      continue;
    }

    if (arg === '--submit-retries') {
      if (i + 1 >= argv.length) {
        throw new Error('--submit-retries requires a value');
      }
      opts.submitRetries = Number(argv[++i]);
      continue;
    }

    if (arg === '--dust-utxo-limit') {
      if (i + 1 >= argv.length) {
        throw new Error('--dust-utxo-limit requires a value');
      }
      opts.dustUtxoLimit = Number(argv[++i]);
      continue;
    }

    if (arg === '--dust-deregister-retries') {
      if (i + 1 >= argv.length) {
        throw new Error('--dust-deregister-retries requires a value');
      }
      opts.dustDeregisterRetries = Number(argv[++i]);
      continue;
    }

    if (arg === '--no-auto-register-dust') {
      opts.autoRegisterDust = false;
      continue;
    }

    if (arg === '--allow-self-transfer') {
      opts.allowSelfTransfer = true;
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
  const addressFormatMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-address-format', 'dist', 'index.js'));
  const facadeMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-facade', 'dist', 'index.js'));
  const shieldedMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-shielded', 'dist', 'index.js'));
  const dustMod = await import(resolve('@midnight-ntwrk', 'wallet-sdk-dust-wallet', 'dist', 'index.js'));
  const ledgerMod = await import(resolve('@midnight-ntwrk', 'ledger-v8', 'midnight_ledger_wasm_fs.js'));
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
    MidnightBech32m: addressFormatMod.MidnightBech32m,
    UnshieldedAddress: addressFormatMod.UnshieldedAddress,
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

  return map[network] ?? map.preview;
}

function getDustFeeOverhead(networkId) {
  const normalized = String(networkId ?? '').toLowerCase();
  if (normalized.includes('undeployed')) {
    return 500000000000000000n;
  }
  return 1000n;
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
        additionalFeeOverhead: getDustFeeOverhead(networkId),
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

function appendError(existing, message) {
  if (!message) {
    return existing;
  }
  return existing ? `${existing}; ${message}` : message;
}

function getDefaultStateCachePath(repoRoot, network, normalizedSeedHex) {
  const digest = createHash('sha256')
    .update(`${network}:${normalizedSeedHex}`)
    .digest('hex')
    .slice(0, 24);
  return path.join(repoRoot, '.cache', 'official-wallet-state', `${network}-${digest}.json`);
}

function encodeSerializedState(serialized) {
  if (typeof serialized === 'string') {
    return { kind: 'string', data: serialized };
  }

  if (serialized instanceof Uint8Array || Buffer.isBuffer(serialized)) {
    return { kind: 'base64', data: Buffer.from(serialized).toString('base64') };
  }

  throw new Error(`Unsupported serialized state type: ${typeof serialized}`);
}

function decodeSerializedState(encoded) {
  if (!encoded || typeof encoded !== 'object') {
    return null;
  }

  if (encoded.kind === 'string' && typeof encoded.data === 'string') {
    return encoded.data;
  }

  if (encoded.kind === 'base64' && typeof encoded.data === 'string') {
    return Buffer.from(encoded.data, 'base64');
  }

  return null;
}

function loadStateCache(filePath) {
  if (!filePath || !fs.existsSync(filePath)) {
    return null;
  }

  const raw = fs.readFileSync(filePath, 'utf8');
  const parsed = JSON.parse(raw);
  if (!parsed || typeof parsed !== 'object') {
    return null;
  }

  return parsed;
}

function saveStateCache(filePath, payload) {
  const dirPath = path.dirname(filePath);
  fs.mkdirSync(dirPath, { recursive: true });
  fs.writeFileSync(filePath, `${JSON.stringify(payload, null, 2)}\n`, 'utf8');
}

function timeout(ms, message) {
  return new Promise((_, reject) => {
    setTimeout(() => reject(new Error(message)), ms);
  });
}

function withTimeout(promise, ms, message) {
  return Promise.race([promise, timeout(ms, message)]);
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

function asBigIntOrNull(value) {
  if (typeof value === 'bigint') {
    return value;
  }

  if (typeof value === 'number' && Number.isFinite(value) && Number.isInteger(value)) {
    return BigInt(value);
  }

  if (typeof value === 'string' && /^-?[0-9]+$/.test(value)) {
    try {
      return BigInt(value);
    } catch {
      return null;
    }
  }

  return null;
}

function stringifyNumeric(value) {
  const bi = asBigIntOrNull(value);
  if (bi !== null) {
    return bi.toString();
  }

  if (typeof value === 'number' && Number.isFinite(value)) {
    return String(value);
  }

  if (typeof value === 'string') {
    return value;
  }

  return '';
}

function normalizeProgress(progress) {
  const appliedIndex = asBigIntOrNull(progress?.appliedIndex);
  const highestRelevantWalletIndex = asBigIntOrNull(progress?.highestRelevantWalletIndex);
  const highestIndex = asBigIntOrNull(progress?.highestIndex);
  const highestRelevantIndex = asBigIntOrNull(progress?.highestRelevantIndex);

  let isStrictlyComplete = null;
  try {
    if (typeof progress?.isStrictlyComplete === 'function') {
      isStrictlyComplete = Boolean(progress.isStrictlyComplete());
    }
  } catch {
    isStrictlyComplete = null;
  }

  let isCompleteWithin50 = null;
  try {
    if (typeof progress?.isCompleteWithin === 'function') {
      isCompleteWithin50 = Boolean(progress.isCompleteWithin(50n));
    }
  } catch {
    isCompleteWithin50 = null;
  }

  let applyLag = '';
  if (appliedIndex !== null && highestRelevantWalletIndex !== null) {
    applyLag = (highestRelevantWalletIndex >= appliedIndex
      ? highestRelevantWalletIndex - appliedIndex
      : appliedIndex - highestRelevantWalletIndex).toString();
  }

  return {
    appliedIndex: stringifyNumeric(appliedIndex),
    highestRelevantWalletIndex: stringifyNumeric(highestRelevantWalletIndex),
    highestIndex: stringifyNumeric(highestIndex),
    highestRelevantIndex: stringifyNumeric(highestRelevantIndex),
    applyLag,
    isConnected: typeof progress?.isConnected === 'boolean' ? progress.isConnected : null,
    isStrictlyComplete,
    isCompleteWithin50,
  };
}

function buildSyncDiagnostics(state) {
  return {
    facadeIsSynced: Boolean(state?.isSynced),
    shielded: normalizeProgress(state?.shielded?.state?.progress),
    unshielded: normalizeProgress(state?.unshielded?.progress),
    dust: normalizeProgress(state?.dust?.state?.progress),
  };
}

function getAvailableUnshieldedCoins(state) {
  if (Array.isArray(state?.unshielded?.availableCoins)) {
    return state.unshielded.availableCoins;
  }

  if (Array.isArray(state?.unshielded?.state?.availableUtxos)) {
    return state.unshielded.state.availableUtxos;
  }

  try {
    const getAvailableCoins = state?.unshielded?.capabilities?.coinsAndBalances?.getAvailableCoins;
    if (typeof getAvailableCoins === 'function') {
      const available = getAvailableCoins(state?.unshielded?.state ?? state?.unshielded ?? state);
      if (Array.isArray(available)) {
        return available;
      }
    }
  } catch {
    // Ignore capability-shape mismatches across SDK versions and use empty fallback.
  }

  return [];
}

function getPendingUnshieldedCoins(state) {
  if (Array.isArray(state?.unshielded?.pendingCoins)) {
    return state.unshielded.pendingCoins;
  }

  if (Array.isArray(state?.unshielded?.state?.pendingUtxos)) {
    return state.unshielded.state.pendingUtxos;
  }

  try {
    const getPendingCoins = state?.unshielded?.capabilities?.coinsAndBalances?.getPendingCoins;
    if (typeof getPendingCoins === 'function') {
      const pending = getPendingCoins(state?.unshielded?.state ?? state?.unshielded ?? state);
      if (Array.isArray(pending)) {
        return pending;
      }
    }
  } catch {
    // Ignore capability-shape mismatches across SDK versions and use empty fallback.
  }

  return [];
}

function getCoinMeta(coin) {
  if (coin && typeof coin === 'object' && coin.meta && typeof coin.meta === 'object') {
    return coin.meta;
  }
  return {};
}

function getCoinValue(coin) {
  const direct = asBigIntOrNull(coin?.value);
  if (direct !== null) {
    return direct;
  }

  const nested = asBigIntOrNull(coin?.utxo?.value);
  if (nested !== null) {
    return nested;
  }

  return 0n;
}

function getDustBalance(dustState, at = new Date()) {
  if (!dustState || typeof dustState !== 'object') {
    return 0n;
  }

  try {
    if (typeof dustState.walletBalance === 'function') {
      return asBigIntOrNull(dustState.walletBalance(at)) ?? 0n;
    }

    if (typeof dustState.balance === 'function') {
      return asBigIntOrNull(dustState.balance(at)) ?? 0n;
    }
  } catch {
    return 0n;
  }

  return 0n;
}

function collectWalletMetrics(state, modules, tokenType) {
  const unshieldedBalance = state?.unshielded?.balances?.[tokenType] ?? 0n;

  let dustBalance = 0n;
  let dustBalanceError = '';
  try {
    dustBalance = getDustBalance(state?.dust);
  } catch (error) {
    dustBalanceError = error instanceof Error ? error.message : String(error);
  }

  const dustAvailableCoins = Array.isArray(state?.dust?.availableCoins)
    ? state.dust.availableCoins.length
    : 0;
  const dustPendingCoins = Array.isArray(state?.dust?.pendingCoins)
    ? state.dust.pendingCoins.length
    : 0;
  const dustPendingBalance = Array.isArray(state?.dust?.pendingCoins)
    ? state.dust.pendingCoins.reduce((sum, coin) => {
      const value = typeof coin?.initialValue === 'bigint' ? coin.initialValue : 0n;
      return sum + value;
    }, 0n)
    : 0n;

  const availableCoins = getAvailableUnshieldedCoins(state);
  const pendingCoins = getPendingUnshieldedCoins(state);
  const availableUtxoCount = availableCoins.length;
  const registeredUtxoCount = availableCoins.filter(
    (coin) => getCoinMeta(coin).registeredForDustGeneration === true,
  ).length;
  const unregisteredUtxoCount = availableCoins.filter(
    (coin) => getCoinMeta(coin).registeredForDustGeneration !== true,
  ).length;
  const spendableUnregisteredUtxoCount = unregisteredUtxoCount;
  const pendingUtxoCount = pendingCoins.length;
  const pendingUnshieldedBalance = pendingCoins.reduce((sum, coin) => sum + getCoinValue(coin), 0n);

  const dustGracePeriodSeconds = Number(
    modules.ledger.LedgerParameters.initialParameters().dust.dustGracePeriodSeconds ?? 0n,
  );

  return {
    unshieldedBalance,
    dustBalance,
    dustBalanceError,
    dustAvailableCoins,
    dustPendingCoins,
    dustPendingBalance,
    availableCoins,
    pendingCoins,
    availableUtxoCount,
    registeredUtxoCount,
    unregisteredUtxoCount,
    spendableUnregisteredUtxoCount,
    pendingUtxoCount,
    pendingUnshieldedBalance,
    dustGracePeriodSeconds,
    dustEstimate: computeDustAvailabilityEstimate(availableCoins, dustGracePeriodSeconds),
    syncDiagnostics: buildSyncDiagnostics(state),
  };
}

async function registerDustForTransfer(params) {
  const {
    wallet,
    unshieldedKeystore,
    availableCoins,
    syncTimeoutSeconds,
    dustUtxoLimit,
    dustAddress,
  } = params;

  const result = {
    enabled: true,
    attempted: false,
    success: false,
    strategy: '',
    txId: '',
    message: '',
    error: '',
    registrationBatchSize: 0,
    availableUtxoCount: availableCoins.length,
    unregisteredUtxoCount: 0,
  };

  const nightUtxos = availableCoins.filter(
    (coin) => getCoinMeta(coin).registeredForDustGeneration !== true,
  );
  result.unregisteredUtxoCount = nightUtxos.length;

  if (nightUtxos.length === 0) {
    result.message = 'No unregistered NIGHT UTXOs found for auto-registration';
    return result;
  }

  result.attempted = true;

  const opTimeoutMs = Math.max(120, syncTimeoutSeconds * 5) * 1000;
  const registrationBatchSize = Math.min(Math.max(1, dustUtxoLimit), nightUtxos.length);
  const utxosToRegister = nightUtxos.slice(0, registrationBatchSize);
  result.registrationBatchSize = registrationBatchSize;

  const signingKey = unshieldedKeystore.getPublicKey();
  const signRegistration = (payload) => unshieldedKeystore.signData(payload);

  let recipe;
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

    if (dustAddress && /argument|parameter|arity|expects/i.test(message)) {
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

  if (!recipe && typeof wallet.createDustActionTransaction === 'function' && dustAddress) {
    try {
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
    } catch (error) {
      registerError = error;
    }
  }

  if (!recipe) {
    const error = registerError ?? new Error('Unable to build dust registration recipe');
    result.error = error instanceof Error ? error.message : String(error);
    result.message = result.error;
    return result;
  }

  try {
    const finalizePromise =
      recipe && typeof recipe === 'object' && recipe.transaction && typeof wallet.finalizeTransaction === 'function'
        ? wallet.finalizeTransaction(recipe.transaction)
        : wallet.finalizeRecipe(recipe);

    const finalized = await withTimeout(
      finalizePromise,
      opTimeoutMs,
      `Timed out finalizing dust registration transaction (${Math.floor(opTimeoutMs / 1000)}s)`,
    );

    const txId = await withTimeout(
      wallet.submitTransaction(finalized),
      opTimeoutMs,
      `Timed out submitting dust registration transaction (${Math.floor(opTimeoutMs / 1000)}s)`,
    );

    result.success = true;
    result.txId = String(txId);
    result.message = `Submitted auto dust registration for ${registrationBatchSize}/${nightUtxos.length} unregistered NIGHT UTXO(s)`;
    return result;
  } catch (error) {
    result.error = error instanceof Error ? error.message : String(error);
    result.message = result.error;
    return result;
  }
}

async function deregisterDustForTransfer(params) {
  const {
    wallet,
    unshieldedKeystore,
    availableCoins,
    syncTimeoutSeconds,
    dustUtxoLimit,
    dustDeregisterRetries,
  } = params;

  const result = {
    enabled: true,
    attempted: false,
    success: false,
    strategy: '',
    txId: '',
    message: '',
    error: '',
    deregistrationBatchSize: 0,
    attempts: 0,
    availableUtxoCount: availableCoins.length,
    registeredUtxoCount: 0,
    unregisteredUtxoCount: 0,
  };

  const registeredUtxos = availableCoins.filter(
    (coin) => getCoinMeta(coin).registeredForDustGeneration === true,
  );
  result.registeredUtxoCount = registeredUtxos.length;
  result.unregisteredUtxoCount = availableCoins.length - registeredUtxos.length;

  if (registeredUtxos.length === 0) {
    result.message = 'No registered NIGHT UTXOs found for auto-deregistration';
    return result;
  }

  if (typeof wallet.finalizeRecipe !== 'function' && typeof wallet.finalizeTransaction !== 'function') {
    result.error = 'Wallet SDK does not expose finalizeRecipe/finalizeTransaction';
    result.message = result.error;
    return result;
  }

  result.attempted = true;

  const opTimeoutMs = Math.max(120, syncTimeoutSeconds * 5) * 1000;
  const deregistrationBatchSize = Math.min(Math.max(1, dustUtxoLimit), registeredUtxos.length);
  const utxosToDeregister = registeredUtxos.slice(0, deregistrationBatchSize);
  result.deregistrationBatchSize = deregistrationBatchSize;

  const signingKey = unshieldedKeystore.getPublicKey();
  const signRegistration = (payload) => unshieldedKeystore.signData(payload);

  const maxAttempts = Math.max(1, dustDeregisterRetries + 1);
  let lastErrorMessage = '';
  let attempts = 0;

  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    attempts = attempt + 1;
    result.attempts = attempts;

    try {
      let recipe;

      if (typeof wallet.deregisterFromDustGeneration === 'function') {
        recipe = await withTimeout(
          wallet.deregisterFromDustGeneration(
            utxosToDeregister,
            signingKey,
            signRegistration,
          ),
          opTimeoutMs,
          `Timed out building dust deregistration recipe (${Math.floor(opTimeoutMs / 1000)}s)`,
        );
        result.strategy = 'deregisterFromDustGeneration';
      } else if (typeof wallet.createDustActionTransaction === 'function') {
        const dustTx = await withTimeout(
          wallet.createDustActionTransaction(
            { type: 'deregistration' },
            utxosToDeregister,
            signingKey,
            signRegistration,
          ),
          opTimeoutMs,
          `Timed out building dust deregistration transaction (${Math.floor(opTimeoutMs / 1000)}s)`,
        );

        recipe = {
          type: 'UNPROVEN_TRANSACTION',
          transaction: dustTx,
        };
        result.strategy = 'createDustActionTransaction(deregistration)';
      } else {
        throw new Error('Wallet SDK does not expose dust deregistration methods');
      }

      const finalizePromise =
        recipe && typeof recipe === 'object' && recipe.transaction && typeof wallet.finalizeTransaction === 'function'
          ? wallet.finalizeTransaction(recipe.transaction)
          : wallet.finalizeRecipe(recipe);

      const finalized = await withTimeout(
        finalizePromise,
        opTimeoutMs,
        `Timed out finalizing dust deregistration transaction (${Math.floor(opTimeoutMs / 1000)}s)`,
      );

      const txId = await withTimeout(
        wallet.submitTransaction(finalized),
        opTimeoutMs,
        `Timed out submitting dust deregistration transaction (${Math.floor(opTimeoutMs / 1000)}s)`,
      );

      result.success = true;
      result.txId = String(txId);
      result.message = `Submitted auto dust deregistration for ${deregistrationBatchSize}/${registeredUtxos.length} registered NIGHT UTXO(s) on attempt ${attempts}/${maxAttempts}`;
      return result;
    } catch (error) {
      lastErrorMessage = error instanceof Error ? error.message : String(error);

      const canRetry = attempt < (maxAttempts - 1)
        && shouldRetryDustDeregistration(lastErrorMessage);

      if (canRetry) {
        const backoffMs = Math.min(15_000, 3_000 * (attempt + 1));
        await delay(backoffMs);
        continue;
      }

      break;
    }
  }

  result.error = lastErrorMessage;
  result.message = result.error
    ? `Auto-deregistration failed after ${attempts}/${maxAttempts} attempt(s): ${result.error}`
    : `Auto-deregistration failed after ${attempts}/${maxAttempts} attempt(s)`;
  return result;
}

function emitResultAndExit(payload, exitCode) {
  process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`);
  process.exit(exitCode);
}

function delay(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

function hasCustomErrorCode(errorMessage, code) {
  if (!errorMessage) {
    return false;
  }

  return String(errorMessage).toLowerCase().includes(`custom error: ${String(code)}`);
}

function shouldRetryDustDeregistration(errorMessage) {
  if (!errorMessage) {
    return false;
  }

  if (hasCustomErrorCode(errorMessage, 182)) {
    return false;
  }

  const msg = String(errorMessage).toLowerCase();
  return /timeout|temporarily|pool|stale|disconnected|connection|429|too many requests/.test(msg);
}

function shouldRetryTransferAttempt(stage, errorMessage) {
  if (!errorMessage) {
    return false;
  }

  const msg = String(errorMessage).toLowerCase();

  if (stage === 'submitTransaction') {
    if (hasCustomErrorCode(errorMessage, 182)) {
      return false;
    }

    return /invalid transaction|transaction submission failed|custom error|temporarily|timeout|stale|pool/.test(msg);
  }

  if (stage === 'transferTransaction') {
    return /no dust tokens|dust|insufficient|sync|pending/.test(msg);
  }

  return false;
}

function isLikelyDustDesignationLock(metrics) {
  if (!metrics || typeof metrics !== 'object') {
    return false;
  }

  if (metrics.availableUtxoCount <= 0) {
    return false;
  }

  if (metrics.registeredUtxoCount !== metrics.availableUtxoCount) {
    return false;
  }

  if (metrics.unregisteredUtxoCount !== 0) {
    return false;
  }

  return Number(metrics?.dustEstimate?.estimatedDustWaitSeconds ?? 0) > 0;
}

function isLikelyPendingUtxoLock(metrics) {
  if (!metrics || typeof metrics !== 'object') {
    return false;
  }

  if ((metrics.pendingUtxoCount ?? 0) <= 0) {
    return false;
  }

  return true;
}

function buildErrorHint(stage, errorMessage, sourceAddress, destinationAddress) {
  const msg = String(errorMessage ?? '');
  if (!msg) {
    return '';
  }

  const lower = msg.toLowerCase();

  if (sourceAddress && destinationAddress && sourceAddress === destinationAddress) {
    if (lower.includes('custom error: 192') || lower.includes('invalid transaction')) {
      return 'Source and destination are identical. Preview/preprod nodes may reject self-transfer. Use a different destination or --allow-self-transfer only for diagnostics.';
    }
  }

  if (lower.includes('no dust tokens found')) {
    return 'Wallet does not currently expose spendable DUST. Wait for sync and DUST availability, or re-run explicit dust registration before transfer.';
  }

  if (lower.includes('custom error: 182')) {
    return 'Node rejected this transaction (Custom error: 182). On preview/preprod this often means selected NIGHT UTXOs are non-spendable while designated for DUST generation. If auto-deregistration also fails with 182, this wallet cannot transfer until at least one unregistered NIGHT UTXO is available.';
  }

  if (lower.includes('custom error: 192')) {
    return 'Node rejected this transaction shape. Verify destination address and avoid self-transfer on preview.';
  }

  if (lower.includes('custom error: 138')) {
    return 'Node rejected dust deregistration transaction. Retry with a fully synced wallet and SDK recipe flow.';
  }

  if (lower.includes('insufficient unshielded night balance')) {
    return 'Funds may be temporarily pending/locked after a failed transaction. Wait for UTXO settlement and retry.';
  }

  if (lower.includes('insufficient funds')) {
    return 'NIGHT UTXOs may be fully designated for DUST generation and temporarily unavailable for transfer; auto-deregistration and retry may be required.';
  }

  if (stage === 'submitTransaction' && lower.includes('invalid transaction')) {
    return 'Submission rejected by node. A short resync and retry often resolves transient mempool/state races.';
  }

  return '';
}

async function executeTransferOnce(wallet, tokenType, destinationAddress, amount, shieldedSecretKeys, dustSecretKey, unshieldedKeystore) {
  let recipe;
  try {
    recipe = await wallet.transferTransaction(
      [
        {
          type: 'unshielded',
          outputs: [
            {
              type: tokenType,
              receiverAddress: destinationAddress,
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
  } catch (error) {
    return {
      success: false,
      stage: 'transferTransaction',
      error: error instanceof Error ? error.message : String(error),
    };
  }

  let signedRecipe = recipe;
  try {
    if (typeof wallet.signRecipe === 'function' && unshieldedKeystore && typeof unshieldedKeystore.signData === 'function') {
      signedRecipe = await wallet.signRecipe(recipe, (payload) => unshieldedKeystore.signData(payload));
    }
  } catch (error) {
    return {
      success: false,
      stage: 'signRecipe',
      error: error instanceof Error ? error.message : String(error),
    };
  }

  let finalized;
  try {
    finalized = await wallet.finalizeRecipe(signedRecipe);
  } catch (error) {
    return {
      success: false,
      stage: 'finalizeRecipe',
      error: error instanceof Error ? error.message : String(error),
    };
  }

  try {
    const txId = await wallet.submitTransaction(finalized);
    return {
      success: true,
      txId: String(txId),
    };
  } catch (error) {
    return {
      success: false,
      stage: 'submitTransaction',
      error: error instanceof Error ? error.message : String(error),
    };
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
  if (opts.dustWaitSeconds !== null) {
    validateIndex('dust-wait', opts.dustWaitSeconds);
  }
  validateIndex('settle-wait', opts.settleWaitSeconds);
  if (!Number.isInteger(opts.submitRetries) || opts.submitRetries < 0 || opts.submitRetries > 5) {
    throw new Error('submit-retries must be an integer in range [0, 5]');
  }
  if (!Number.isInteger(opts.dustUtxoLimit) || opts.dustUtxoLimit < 1 || opts.dustUtxoLimit > 1000) {
    throw new Error('dust-utxo-limit must be an integer in range [1, 1000]');
  }
  if (!Number.isInteger(opts.dustDeregisterRetries) || opts.dustDeregisterRetries < 0 || opts.dustDeregisterRetries > 10) {
    throw new Error('dust-deregister-retries must be an integer in range [0, 10]');
  }

  const normalizedSeed = normalizeSeedHex(opts.seedHex);
  const amount = parseAmount(opts.amount);

  const thisFile = fileURLToPath(import.meta.url);
  const thisDir = path.dirname(thisFile);
  const repoRoot = path.resolve(thisDir, '..');
  const stateCachePath = opts.useStateCache
    ? (opts.stateCache || getDefaultStateCachePath(repoRoot, opts.network, normalizedSeed))
    : '';
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

  let stateCacheLoadError = '';
  let stateCacheLoaded = false;
  let stateCacheRestored = {
    shielded: false,
    unshielded: false,
    dust: false,
  };

  let cachedState = null;
  if (opts.useStateCache) {
    try {
      cachedState = loadStateCache(stateCachePath);
      stateCacheLoaded = Boolean(cachedState);
    } catch (error) {
      stateCacheLoadError = error instanceof Error ? error.message : String(error);
    }
  }

  const shieldedWalletFactory = modules.ShieldedWallet(shieldedConfig);
  const unshieldedWalletFactory = modules.UnshieldedWallet(unshieldedConfig);
  const dustWalletFactory = modules.DustWallet(dustConfig);

  let shieldedWallet;
  let unshieldedWallet;
  let dustWallet;

  const restoredShielded = decodeSerializedState(cachedState?.shieldedState);
  if (opts.useStateCache && restoredShielded !== null) {
    try {
      shieldedWallet = shieldedWalletFactory.restore(restoredShielded);
      stateCacheRestored.shielded = true;
    } catch (error) {
      stateCacheLoadError = appendError(
        stateCacheLoadError,
        `shielded restore failed: ${error instanceof Error ? error.message : String(error)}`,
      );
    }
  }
  if (!shieldedWallet) {
    shieldedWallet = shieldedWalletFactory.startWithSecretKeys(shieldedSecretKeys);
  }

  const restoredUnshielded = decodeSerializedState(cachedState?.unshieldedState);
  if (opts.useStateCache && restoredUnshielded !== null) {
    try {
      unshieldedWallet = unshieldedWalletFactory.restore(restoredUnshielded);
      stateCacheRestored.unshielded = true;
    } catch (error) {
      stateCacheLoadError = appendError(
        stateCacheLoadError,
        `unshielded restore failed: ${error instanceof Error ? error.message : String(error)}`,
      );
    }
  }
  if (!unshieldedWallet) {
    unshieldedWallet = unshieldedWalletFactory.startWithPublicKey(
      modules.PublicKey.fromKeyStore(unshieldedKeystore),
    );
  }

  const restoredDust = decodeSerializedState(cachedState?.dustState);
  if (opts.useStateCache && restoredDust !== null) {
    try {
      dustWallet = dustWalletFactory.restore(restoredDust);
      stateCacheRestored.dust = true;
    } catch (error) {
      stateCacheLoadError = appendError(
        stateCacheLoadError,
        `dust restore failed: ${error instanceof Error ? error.message : String(error)}`,
      );
    }
  }
  if (!dustWallet) {
    dustWallet = dustWalletFactory.startWithSecretKey(
      dustSecretKey,
      modules.ledger.LedgerParameters.initialParameters().dust,
    );
  }

  const wallet = typeof modules.WalletFacade.init === 'function'
    ? await modules.WalletFacade.init({
      configuration: {
        networkId,
        indexerClientConnection: {
          indexerHttpUrl: endpoints.indexerUrl,
          indexerWsUrl: endpoints.indexerWsUrl,
        },
        relayURL: new URL(endpoints.nodeUrl.replace(/^http/, 'ws')),
        provingServerUrl: new URL(endpoints.proofServer),
      },
      shielded: () => shieldedWallet,
      unshielded: () => unshieldedWallet,
      dust: () => dustWallet,
    })
    : new modules.WalletFacade(shieldedWallet, unshieldedWallet, dustWallet);

  let stateCacheSaveAttempted = false;
  let stateCacheSaved = false;
  let stateCacheSaveError = '';

  const persistWalletState = async () => {
    if (!opts.useStateCache) {
      return;
    }

    if (stateCacheSaveAttempted) {
      return;
    }
    stateCacheSaveAttempted = true;

    try {
      const [shieldedState, unshieldedState, dustState] = await Promise.all([
        shieldedWallet.serializeState(),
        unshieldedWallet.serializeState(),
        dustWallet.serializeState(),
      ]);

      saveStateCache(stateCachePath, {
        version: 1,
        network: opts.network,
        sourceAddress,
        updatedAt: new Date().toISOString(),
        shieldedState: encodeSerializedState(shieldedState),
        unshieldedState: encodeSerializedState(unshieldedState),
        dustState: encodeSerializedState(dustState),
      });

      stateCacheSaved = true;
    } catch (error) {
      stateCacheSaveError = error instanceof Error ? error.message : String(error);
    }
  };

  const emitTransferResult = async (payload, exitCode) => {
    await persistWalletState();
    emitResultAndExit(
      {
        ...payload,
        stateCachePath,
        stateCacheLoaded,
        stateCacheRestored,
        stateCacheLoadError,
        stateCacheSaved,
        stateCacheSaveError,
      },
      exitCode,
    );
  };

  try {
    await wallet.start(shieldedSecretKeys, dustSecretKey);

    const syncTimeoutMs = Math.max(10, opts.syncTimeoutSeconds) * 1000;
    const dustWaitSeconds = opts.dustWaitSeconds === null
      ? opts.syncTimeoutSeconds
      : opts.dustWaitSeconds;
    const settleWaitSeconds = opts.settleWaitSeconds;
    const dustWaitTimeoutMs = Math.max(10, dustWaitSeconds) * 1000;
    const settleWaitTimeoutMs = Math.max(10, settleWaitSeconds) * 1000;
    let synced = true;
    let syncError = '';
    let settleWaitError = '';
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
    let metrics = collectWalletMetrics(state, modules, tokenType);
    let dustWaitError = '';
    const dustAddress = typeof state?.dust?.dustAddress === 'string' ? state.dust.dustAddress : '';
    let dustAutoRegistration = {
      enabled: opts.autoRegisterDust && opts.network !== 'mainnet',
      attempted: false,
      success: false,
      strategy: '',
      txId: '',
      message: opts.autoRegisterDust
        ? (opts.network === 'mainnet'
          ? 'Automatic dust registration is skipped on mainnet'
          : 'Automatic dust registration is enabled')
        : 'Automatic dust registration disabled (--no-auto-register-dust)',
      error: '',
      registrationBatchSize: 0,
      availableUtxoCount: metrics.availableUtxoCount,
      unregisteredUtxoCount: metrics.unregisteredUtxoCount,
    };

    let dustAutoDeregistration = {
      enabled: opts.network !== 'mainnet',
      attempted: false,
      success: false,
      strategy: '',
      txId: '',
      message: opts.network === 'mainnet'
        ? 'Automatic dust deregistration is skipped on mainnet'
        : 'Automatic dust deregistration on insufficient-funds transfer is enabled',
      error: '',
      deregistrationBatchSize: 0,
      availableUtxoCount: metrics.availableUtxoCount,
      registeredUtxoCount: metrics.registeredUtxoCount,
      unregisteredUtxoCount: metrics.unregisteredUtxoCount,
    };

    if (!synced || !state?.isSynced) {
      await emitTransferResult(
        {
          success: false,
          source: 'official-midnight-sdk',
          network: networkId,
          sourceAddress,
          destinationAddress: opts.toAddress,
          amount: amount.toString(),
          synced,
          syncError,
          syncDiagnostics: metrics.syncDiagnostics,
          unshieldedBalance: metrics.unshieldedBalance.toString(),
          pendingUnshieldedBalance: metrics.pendingUnshieldedBalance.toString(),
          dustBalance: metrics.dustBalance.toString(),
          dustBalanceError: metrics.dustBalanceError,
          dustAvailableCoins: metrics.dustAvailableCoins,
          dustPendingCoins: metrics.dustPendingCoins,
          dustPendingBalance: metrics.dustPendingBalance.toString(),
          availableUtxoCount: metrics.availableUtxoCount,
          registeredUtxoCount: metrics.registeredUtxoCount,
          pendingUtxoCount: metrics.pendingUtxoCount,
          unregisteredUtxoCount: metrics.unregisteredUtxoCount,
          likelyDustDesignationLock: isLikelyDustDesignationLock(metrics),
          likelyPendingUtxoLock: isLikelyPendingUtxoLock(metrics),
          dustGracePeriodSeconds: metrics.dustGracePeriodSeconds,
          estimatedDustEligibleAt: metrics.dustEstimate.estimatedDustEligibleAt,
          estimatedDustWaitSeconds: metrics.dustEstimate.estimatedDustWaitSeconds,
          dustWaitTimeoutSeconds: dustWaitSeconds,
          dustWaitError,
          settleWaitTimeoutSeconds: settleWaitSeconds,
          settleWaitError,
          dustAutoRegistration,
          dustAutoDeregistration,
          error: 'Wallet is not fully synced yet',
          errorHint: 'Increase --sync-timeout and retry. Do not trust DUST/balance snapshots when synced=false.',
          stage: 'syncCheck',
        },
        8,
      );
      return;
    }

    if (metrics.syncDiagnostics?.dust?.isCompleteWithin50 === false) {
      await emitTransferResult(
        {
          success: false,
          source: 'official-midnight-sdk',
          network: networkId,
          sourceAddress,
          destinationAddress: opts.toAddress,
          amount: amount.toString(),
          synced,
          syncError,
          syncDiagnostics: metrics.syncDiagnostics,
          unshieldedBalance: metrics.unshieldedBalance.toString(),
          pendingUnshieldedBalance: metrics.pendingUnshieldedBalance.toString(),
          dustBalance: metrics.dustBalance.toString(),
          dustBalanceError: metrics.dustBalanceError,
          dustAvailableCoins: metrics.dustAvailableCoins,
          dustPendingCoins: metrics.dustPendingCoins,
          dustPendingBalance: metrics.dustPendingBalance.toString(),
          availableUtxoCount: metrics.availableUtxoCount,
          registeredUtxoCount: metrics.registeredUtxoCount,
          pendingUtxoCount: metrics.pendingUtxoCount,
          unregisteredUtxoCount: metrics.unregisteredUtxoCount,
          likelyDustDesignationLock: isLikelyDustDesignationLock(metrics),
          likelyPendingUtxoLock: isLikelyPendingUtxoLock(metrics),
          dustGracePeriodSeconds: metrics.dustGracePeriodSeconds,
          estimatedDustEligibleAt: metrics.dustEstimate.estimatedDustEligibleAt,
          estimatedDustWaitSeconds: metrics.dustEstimate.estimatedDustWaitSeconds,
          dustWaitTimeoutSeconds: dustWaitSeconds,
          dustWaitError,
          settleWaitTimeoutSeconds: settleWaitSeconds,
          settleWaitError,
          dustAutoRegistration,
          dustAutoDeregistration,
          error: 'Dust wallet sync is lagging behind indexer',
          errorHint: 'Wait for dust sync to catch up (syncDiagnostics.dust.isCompleteWithin50=true) before transfer attempts.',
          stage: 'dustSyncLagCheck',
        },
        9,
      );
      return;
    }

    if (
      dustAutoRegistration.enabled &&
      metrics.dustBalance <= 0n &&
      metrics.unregisteredUtxoCount > 0
    ) {
      dustAutoRegistration = await registerDustForTransfer({
        wallet,
        unshieldedKeystore,
        availableCoins: metrics.availableCoins,
        syncTimeoutSeconds: opts.syncTimeoutSeconds,
        dustUtxoLimit: opts.dustUtxoLimit,
        dustAddress,
      });

      if (dustAutoRegistration.success) {
        try {
          state = await waitForState(
            wallet.state(),
            (s) => Boolean(s?.isSynced),
            syncTimeoutMs,
            `Timed out waiting for wallet sync after auto dust registration (${opts.syncTimeoutSeconds}s)`,
          );
          synced = Boolean(state?.isSynced);
        } catch (error) {
          const registrationSyncError = error instanceof Error ? error.message : String(error);
          syncError = appendError(syncError, registrationSyncError);
        }

        metrics = collectWalletMetrics(state, modules, tokenType);
        dustAutoRegistration.availableUtxoCount = metrics.availableUtxoCount;
        dustAutoRegistration.unregisteredUtxoCount = metrics.unregisteredUtxoCount;
      }
    }

    const buildTransferPayload = () => ({
      source: 'official-midnight-sdk',
      network: networkId,
      sourceAddress,
      destinationAddress: opts.toAddress,
      amount: amount.toString(),
      synced,
      syncError,
      syncDiagnostics: metrics.syncDiagnostics,
      unshieldedBalance: metrics.unshieldedBalance.toString(),
      pendingUnshieldedBalance: metrics.pendingUnshieldedBalance.toString(),
      dustBalance: metrics.dustBalance.toString(),
      dustBalanceError: metrics.dustBalanceError,
      dustAvailableCoins: metrics.dustAvailableCoins,
      dustPendingCoins: metrics.dustPendingCoins,
      dustPendingBalance: metrics.dustPendingBalance.toString(),
      availableUtxoCount: metrics.availableUtxoCount,
      registeredUtxoCount: metrics.registeredUtxoCount,
      pendingUtxoCount: metrics.pendingUtxoCount,
      unregisteredUtxoCount: metrics.unregisteredUtxoCount,
      spendableUnregisteredUtxoCount: metrics.spendableUnregisteredUtxoCount,
      likelyDustDesignationLock: isLikelyDustDesignationLock(metrics),
      likelyPendingUtxoLock: isLikelyPendingUtxoLock(metrics),
      dustGracePeriodSeconds: metrics.dustGracePeriodSeconds,
      estimatedDustEligibleAt: metrics.dustEstimate.estimatedDustEligibleAt,
      estimatedDustWaitSeconds: metrics.dustEstimate.estimatedDustWaitSeconds,
      dustWaitTimeoutSeconds: dustWaitSeconds,
      dustWaitError,
      settleWaitTimeoutSeconds: settleWaitSeconds,
      settleWaitError,
      dustAutoRegistration,
      dustAutoDeregistration,
    });

    let destinationReceiverAddress = opts.toAddress;
    try {
      if (modules.MidnightBech32m && modules.UnshieldedAddress) {
        destinationReceiverAddress = modules.MidnightBech32m
          .parse(opts.toAddress)
          .decode(modules.UnshieldedAddress, networkId);
      }
    } catch (error) {
      const addressError = error instanceof Error ? error.message : String(error);
      await emitTransferResult(
        {
          success: false,
          ...buildTransferPayload(),
          error: `Invalid destination address: ${addressError}`,
          errorHint: 'Provide a valid bech32 unshielded address for the selected network.',
          stage: 'validateInput',
        },
        2,
      );
      return;
    }

    if (!opts.allowSelfTransfer && sourceAddress === opts.toAddress) {
      const error = 'Source and destination addresses are identical. Self-transfer is blocked by default because preview/preprod nodes may reject it (Custom error: 192). Use --allow-self-transfer to override.';
      await emitTransferResult(
        {
          success: false,
          ...buildTransferPayload(),
          error,
          errorHint: buildErrorHint('validateInput', error, sourceAddress, opts.toAddress),
          stage: 'validateInput',
        },
        2,
      );
      return;
    }

    if (metrics.dustBalance <= 0n && metrics.availableUtxoCount > 0) {
      try {
        const dustReadyState = await waitForState(
          wallet.state(),
          (s) => {
            try {
              return getDustBalance(s?.dust) > 0n;
            } catch {
              return false;
            }
          },
          dustWaitTimeoutMs,
          `Timed out waiting for DUST balance update (${dustWaitSeconds}s)`,
        );
        state = dustReadyState;
        synced = Boolean(dustReadyState?.isSynced);
        metrics = collectWalletMetrics(state, modules, tokenType);
        dustAutoRegistration.availableUtxoCount = metrics.availableUtxoCount;
        dustAutoRegistration.unregisteredUtxoCount = metrics.unregisteredUtxoCount;
        dustAutoDeregistration.availableUtxoCount = metrics.availableUtxoCount;
        dustAutoDeregistration.registeredUtxoCount = metrics.registeredUtxoCount;
        dustAutoDeregistration.unregisteredUtxoCount = metrics.unregisteredUtxoCount;
      } catch (error) {
        dustWaitError = error instanceof Error ? error.message : String(error);
      }
    }

    if (metrics.unshieldedBalance < amount || metrics.availableUtxoCount === 0) {
      try {
        state = await waitForState(
          wallet.state(),
          (s) => {
            if (!s?.isSynced) {
              return false;
            }
            const m = collectWalletMetrics(s, modules, tokenType);
            return m.unshieldedBalance >= amount && m.availableUtxoCount > 0;
          },
          settleWaitTimeoutMs,
          `Timed out waiting for unshielded UTXO settlement (${settleWaitSeconds}s)`,
        );

        synced = Boolean(state?.isSynced);
        metrics = collectWalletMetrics(state, modules, tokenType);
        dustAutoDeregistration.availableUtxoCount = metrics.availableUtxoCount;
        dustAutoDeregistration.registeredUtxoCount = metrics.registeredUtxoCount;
        dustAutoDeregistration.unregisteredUtxoCount = metrics.unregisteredUtxoCount;
      } catch (error) {
        settleWaitError = error instanceof Error ? error.message : String(error);
      }
    }

    if (metrics.pendingUtxoCount > 0) {
      try {
        state = await waitForState(
          wallet.state(),
          (s) => {
            if (!s?.isSynced) {
              return false;
            }
            const m = collectWalletMetrics(s, modules, tokenType);
            return m.pendingUtxoCount === 0;
          },
          settleWaitTimeoutMs,
          `Timed out waiting for pending unshielded UTXOs to clear (${settleWaitSeconds}s)`,
        );

        synced = Boolean(state?.isSynced);
        metrics = collectWalletMetrics(state, modules, tokenType);
      } catch (error) {
        const pendingWaitError = error instanceof Error ? error.message : String(error);
        settleWaitError = appendError(settleWaitError, pendingWaitError);
      }
    }

    if (metrics.unshieldedBalance < amount) {
      const insufficientError = 'Insufficient unshielded NIGHT balance';
      await emitTransferResult(
        {
          success: false,
          ...buildTransferPayload(),
          error: insufficientError,
          errorHint: buildErrorHint('balanceCheck', insufficientError, sourceAddress, opts.toAddress),
        },
        2,
      );
      return;
    }

    if (isLikelyDustDesignationLock(metrics)) {
      const lockError = 'All available NIGHT UTXOs are currently designated for DUST generation and appear locked by grace period';
      await emitTransferResult(
        {
          success: false,
          ...buildTransferPayload(),
          error: lockError,
          errorHint: `Wait until estimatedDustEligibleAt (${metrics.dustEstimate.estimatedDustEligibleAt}) or estimatedDustWaitSeconds reaches 0, then retry transfer.`,
          stage: 'preTransferLockCheck',
        },
        6,
      );
      return;
    }

    if (isLikelyPendingUtxoLock(metrics)) {
      const pendingError = 'One or more NIGHT UTXOs are pending from a previous failed/unfinished transaction';
      await emitTransferResult(
        {
          success: false,
          ...buildTransferPayload(),
          error: pendingError,
          errorHint: 'Wait until pendingUtxoCount becomes 0, then retry transfer. If it stays pending, fund a fresh unregistered NIGHT UTXO and transfer with --no-auto-register-dust.',
          stage: 'preTransferPendingCheck',
        },
        7,
      );
      return;
    }

    if (metrics.availableUtxoCount > 0 && metrics.unregisteredUtxoCount === 0) {
      if (dustAutoDeregistration.enabled && !dustAutoDeregistration.attempted) {
        dustAutoDeregistration = await deregisterDustForTransfer({
          wallet,
          unshieldedKeystore,
          availableCoins: metrics.availableCoins,
          syncTimeoutSeconds: opts.syncTimeoutSeconds,
          dustUtxoLimit: opts.dustUtxoLimit,
          dustDeregisterRetries: opts.dustDeregisterRetries,
        });

        if (dustAutoDeregistration.success) {
          try {
            state = await waitForState(
              wallet.state(),
              (s) => {
                if (!s?.isSynced) {
                  return false;
                }
                const m = collectWalletMetrics(s, modules, tokenType);
                return m.availableUtxoCount > 0 && m.unregisteredUtxoCount > 0 && m.pendingUtxoCount === 0;
              },
              settleWaitTimeoutMs,
              `Timed out waiting for spendable UTXO after auto dust deregistration (${settleWaitSeconds}s)`,
            );

            synced = Boolean(state?.isSynced);
            metrics = collectWalletMetrics(state, modules, tokenType);
            dustAutoDeregistration.availableUtxoCount = metrics.availableUtxoCount;
            dustAutoDeregistration.registeredUtxoCount = metrics.registeredUtxoCount;
            dustAutoDeregistration.unregisteredUtxoCount = metrics.unregisteredUtxoCount;
          } catch (error) {
            const deregWaitError = error instanceof Error ? error.message : String(error);
            settleWaitError = appendError(settleWaitError, deregWaitError);
          }
        } else if (dustAutoDeregistration.error) {
          settleWaitError = appendError(settleWaitError, `auto-deregistration failed: ${dustAutoDeregistration.error}`);
        }
      }

      if (metrics.availableUtxoCount > 0 && metrics.unregisteredUtxoCount === 0) {
        const noSpendableError = 'All available NIGHT UTXOs are designated for DUST generation; none are directly spendable for transfer';
        let noSpendableHint = dustAutoDeregistration.attempted
          ? 'Auto-deregistration was attempted but no spendable unregistered UTXO became available. Fund a fresh unregistered NIGHT UTXO, or retry after wallet/node resync.'
          : 'Create or fund a fresh unregistered NIGHT UTXO, then transfer with --no-auto-register-dust. Alternatively, deregister designated UTXOs if supported by your network/node policy.';

        const allowUndeployedFallback = opts.network === 'undeployed';

        const likelyDeterministicNodeRejection =
          dustAutoDeregistration.attempted
          && /transaction submission error/.test(String(dustAutoDeregistration.error ?? '').toLowerCase())
          && metrics.availableUtxoCount > 0
          && metrics.registeredUtxoCount === metrics.availableUtxoCount
          && metrics.unregisteredUtxoCount === 0
          && Number(metrics?.dustEstimate?.estimatedDustWaitSeconds ?? 0) === 0;

        if (dustAutoDeregistration.attempted && (hasCustomErrorCode(dustAutoDeregistration.error, 182) || likelyDeterministicNodeRejection)) {
          noSpendableHint = 'Auto-deregistration was rejected by node (Custom error: 182). This wallet currently has only DUST-designated NIGHT UTXOs and no spendable unregistered UTXO. Receive NIGHT to a fresh wallet/index with --no-auto-register-dust, or transfer from another wallet to create an unregistered UTXO before retrying.';
        }

        if (!allowUndeployedFallback) {
          await emitTransferResult(
            {
              success: false,
              ...buildTransferPayload(),
              error: noSpendableError,
              errorHint: noSpendableHint,
              stage: 'preTransferNoSpendableUtxoCheck',
            },
            10,
          );
          return;
        }

        settleWaitError = appendError(
          settleWaitError,
          `${noSpendableError}; continuing anyway because network=undeployed`,
        );
      }
    }

    const transferRetry = {
      maxRetries: opts.submitRetries,
      attempted: false,
      attemptCount: 0,
      waitError: '',
      lastError: '',
    };

    let txId = '';
    let lastTransferFailure = null;

    for (let attempt = 0; attempt <= opts.submitRetries; attempt += 1) {
      transferRetry.attemptCount = attempt + 1;

      if (attempt > 0) {
        transferRetry.attempted = true;
        try {
          state = await waitForState(
            wallet.state(),
            (s) => {
              if (!s?.isSynced) {
                return false;
              }
              const m = collectWalletMetrics(s, modules, tokenType);
              return m.unshieldedBalance >= amount && m.availableUtxoCount > 0 && m.dustBalance > 0n && m.pendingUtxoCount === 0;
            },
            settleWaitTimeoutMs,
            `Timed out waiting for retry preconditions (${settleWaitSeconds}s)`,
          );
          synced = Boolean(state?.isSynced);
          metrics = collectWalletMetrics(state, modules, tokenType);
          dustAutoDeregistration.availableUtxoCount = metrics.availableUtxoCount;
          dustAutoDeregistration.registeredUtxoCount = metrics.registeredUtxoCount;
          dustAutoDeregistration.unregisteredUtxoCount = metrics.unregisteredUtxoCount;
        } catch (error) {
          transferRetry.waitError = error instanceof Error ? error.message : String(error);
        }
      }

      const transferAttempt = await executeTransferOnce(
        wallet,
        tokenType,
        destinationReceiverAddress,
        amount,
        shieldedSecretKeys,
        dustSecretKey,
        unshieldedKeystore,
      );

      if (transferAttempt.success) {
        txId = transferAttempt.txId;
        lastTransferFailure = null;
        break;
      }

      lastTransferFailure = transferAttempt;
      transferRetry.lastError = transferAttempt.error;

      const transferErrorText = String(transferAttempt.error ?? '').toLowerCase();
      const needsDeregistration =
        dustAutoDeregistration.enabled &&
        !dustAutoDeregistration.attempted &&
        transferAttempt.stage === 'transferTransaction' &&
        transferErrorText.includes('insufficient funds') &&
        metrics.registeredUtxoCount > 0;

      if (needsDeregistration) {
        dustAutoDeregistration = await deregisterDustForTransfer({
          wallet,
          unshieldedKeystore,
          availableCoins: metrics.availableCoins,
          syncTimeoutSeconds: opts.syncTimeoutSeconds,
          dustUtxoLimit: opts.dustUtxoLimit,
          dustDeregisterRetries: opts.dustDeregisterRetries,
        });

        if (dustAutoDeregistration.success) {
          try {
            state = await waitForState(
              wallet.state(),
              (s) => {
                if (!s?.isSynced) {
                  return false;
                }
                const m = collectWalletMetrics(s, modules, tokenType);
                return m.unshieldedBalance >= amount && m.availableUtxoCount > 0 && m.unregisteredUtxoCount > 0;
              },
              settleWaitTimeoutMs,
              `Timed out waiting for UTXO state after auto dust deregistration (${settleWaitSeconds}s)`,
            );

            synced = Boolean(state?.isSynced);
            metrics = collectWalletMetrics(state, modules, tokenType);
            dustAutoDeregistration.availableUtxoCount = metrics.availableUtxoCount;
            dustAutoDeregistration.registeredUtxoCount = metrics.registeredUtxoCount;
            dustAutoDeregistration.unregisteredUtxoCount = metrics.unregisteredUtxoCount;
          } catch (error) {
            const deregWaitError = error instanceof Error ? error.message : String(error);
            settleWaitError = appendError(settleWaitError, deregWaitError);
          }

          const transferAfterDereg = await executeTransferOnce(
            wallet,
            tokenType,
            destinationReceiverAddress,
            amount,
            shieldedSecretKeys,
            dustSecretKey,
            unshieldedKeystore,
          );

          if (transferAfterDereg.success) {
            txId = transferAfterDereg.txId;
            lastTransferFailure = null;
            break;
          }

          lastTransferFailure = transferAfterDereg;
          transferRetry.lastError = transferAfterDereg.error;

          if (!shouldRetryTransferAttempt(transferAfterDereg.stage, transferAfterDereg.error)) {
            break;
          }
        }
      }

      if (!shouldRetryTransferAttempt(transferAttempt.stage, transferAttempt.error)) {
        break;
      }

      if (attempt >= opts.submitRetries) {
        break;
      }
    }

    if (!txId) {
      const error = lastTransferFailure?.error ?? 'Transaction submission failed';
      const stage = lastTransferFailure?.stage ?? 'submitTransaction';
      let errorHint = buildErrorHint(stage, error, sourceAddress, opts.toAddress);

      if (
        !errorHint &&
        stage === 'submitTransaction' &&
        metrics.availableUtxoCount > 0 &&
        metrics.registeredUtxoCount === metrics.availableUtxoCount &&
        metrics.unregisteredUtxoCount === 0
      ) {
        errorHint = 'All available NIGHT UTXOs are already designated for DUST generation. Transfer submission can be rejected until at least one unregistered spendable UTXO exists or deregistration succeeds.';
      }

      if (!errorHint && stage === 'submitTransaction') {
        errorHint = 'Transaction reached submit stage but was rejected by node. Recheck UTXO pending/designation state and retry after sync.';
      }

      await emitTransferResult(
        {
          success: false,
          ...buildTransferPayload(),
          transferRetry,
          error,
          errorHint,
          stage,
        },
        5,
      );
      return;
    }

    await emitTransferResult(
      {
        success: true,
        ...buildTransferPayload(),
        transferRetry,
        txId: String(txId),
        unshieldedBalanceBefore: metrics.unshieldedBalance.toString(),
        dustBalanceBefore: metrics.dustBalance.toString(),
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
      errorStack: error instanceof Error ? (error.stack || '') : '',
    },
    1,
  );
});
