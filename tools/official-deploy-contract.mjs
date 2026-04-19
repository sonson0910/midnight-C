#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { Buffer } from 'node:buffer';
import { randomBytes } from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';

const DEFAULT_NETWORK = 'undeployed';
const DEFAULT_CONTRACT = 'FaucetAMM';
const ALLOWED_NETWORKS = new Set(['preprod', 'preview', 'mainnet', 'undeployed']);
const BRIDGE_REQUIRE = createRequire(import.meta.url);

const BUILTIN_CONTRACT_PROFILES = {
  faucetamm: {
    contractName: 'FaucetAMM',
    moduleSpecifier: '@midnight-ntwrk/counter-contract',
    contractExport: 'FaucetAMM',
    privateStateId: 'faucetAMMPrivateState',
    privateStateStoreName: 'faucetamm-private-state',
    managedAssetPathSegments: ['@midnight-ntwrk', 'counter-contract', 'dist', 'managed', 'FaucetAMM'],
    buildDefaultConstructorArgs: ({ feeBps, constructorNonce }) => [BigInt(feeBps), constructorNonce],
  },
};

function normalizeContractKey(value) {
  return String(value ?? '').toLowerCase().replace(/[^a-z0-9]/g, '');
}

function toCamelCase(value) {
  const parts = String(value ?? '')
    .trim()
    .split(/[^a-zA-Z0-9]+/)
    .filter(Boolean);

  if (parts.length === 0) {
    return 'contract';
  }

  const [first, ...rest] = parts;
  return first.charAt(0).toLowerCase() + first.slice(1) + rest.map((part) =>
    part.charAt(0).toUpperCase() + part.slice(1).toLowerCase(),
  ).join('');
}

function toKebabCase(value) {
  const normalized = String(value ?? '')
    .replace(/([a-z0-9])([A-Z])/g, '$1-$2')
    .replace(/[^a-zA-Z0-9]+/g, '-')
    .replace(/-+/g, '-')
    .replace(/^-|-$/g, '')
    .toLowerCase();
  return normalized || 'contract';
}

function printHelp() {
  const lines = [
    'Midnight Official Compact Deploy Bridge',
    '',
    'Usage:',
    '  node tools/official-deploy-contract.mjs [options]',
    '',
    'Options:',
    '  --contract <name>      Contract identifier (default: FaucetAMM)',
    '  --contract-module <id> Contract JS module path/specifier for custom deploy',
    '  --contract-export <id> Export name from contract module (default: --contract)',
    '  --network <id>         Network ID: preprod|preview|mainnet|undeployed (default: undeployed)',
    '  --seed <hex>           Optional wallet seed (32-byte hex, optional 0x)',
    '  --fee-bps <int>        FaucetAMM fee in basis points (default: 10)',
    '  --initial-nonce <hex>  Optional 32-byte constructor nonce (hex, optional 0x)',
    '  --proof-server <url>   Override proof server URL',
    '  --node-url <url>       Override node RPC URL',
    '  --indexer-url <url>    Override indexer GraphQL URL',
    '  --indexer-ws-url <url> Override indexer GraphQL WS URL',
    '  --sync-timeout <sec>   Wallet sync timeout (default: 120)',
    '  --dust-wait <sec>      Dust availability timeout (default: sync-timeout)',
    '  --deploy-timeout <sec> Deploy operation timeout (default: 300)',
    '  --dust-utxo-limit <n>  Max NIGHT UTXOs to register for DUST in one tx (default: 1)',
    '  --private-state-store <name> Private state store name (default: contract-specific)',
    '  --private-state-id <id> Private state ID passed to deployContract',
    '  --constructor-args-json <json> Constructor args JSON array/object',
    '  --constructor-args-file <path> File containing constructor args JSON',
    '  --zk-config-path <path> Override contract ZK asset directory',
    '  --help                 Show this help',
    '',
    'Typed constructor args JSON:',
    '  - bigint: "bigint:12345" or {"__type":"bigint","value":"12345"}',
    '  - bytes:  "hex:0xdeadbeef" or {"__type":"bytes","hex":"0xdeadbeef"}',
  ];

  process.stdout.write(lines.join('\n') + '\n');
}

function parseArgs(argv) {
  const opts = {
    contract: DEFAULT_CONTRACT,
    contractModule: '',
    contractExport: '',
    network: DEFAULT_NETWORK,
    seedHex: '',
    feeBps: 10,
    initialNonceHex: '',
    proofServer: '',
    nodeUrl: '',
    indexerUrl: '',
    indexerWsUrl: '',
    syncTimeoutSeconds: 120,
    dustWaitSeconds: null,
    deployTimeoutSeconds: 300,
    dustUtxoLimit: 1,
    privateStateStoreName: '',
    privateStateId: '',
    constructorArgsJson: '',
    constructorArgsFile: '',
    zkConfigPath: '',
    help: false,
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];

    if (arg === '--help' || arg === '-h') {
      opts.help = true;
      continue;
    }

    if (arg === '--contract') {
      if (i + 1 >= argv.length) {
        throw new Error('--contract requires a value');
      }
      opts.contract = argv[++i];
      continue;
    }

    if (arg === '--network') {
      if (i + 1 >= argv.length) {
        throw new Error('--network requires a value');
      }
      opts.network = argv[++i];
      continue;
    }

    if (arg === '--contract-module') {
      if (i + 1 >= argv.length) {
        throw new Error('--contract-module requires a value');
      }
      opts.contractModule = argv[++i];
      continue;
    }

    if (arg === '--contract-export') {
      if (i + 1 >= argv.length) {
        throw new Error('--contract-export requires a value');
      }
      opts.contractExport = argv[++i];
      continue;
    }

    if (arg === '--seed') {
      if (i + 1 >= argv.length) {
        throw new Error('--seed requires a value');
      }
      opts.seedHex = argv[++i];
      continue;
    }

    if (arg === '--fee-bps') {
      if (i + 1 >= argv.length) {
        throw new Error('--fee-bps requires a value');
      }
      opts.feeBps = Number(argv[++i]);
      continue;
    }

    if (arg === '--initial-nonce') {
      if (i + 1 >= argv.length) {
        throw new Error('--initial-nonce requires a value');
      }
      opts.initialNonceHex = argv[++i];
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

    if (arg === '--dust-wait') {
      if (i + 1 >= argv.length) {
        throw new Error('--dust-wait requires a value');
      }
      opts.dustWaitSeconds = Number(argv[++i]);
      continue;
    }

    if (arg === '--deploy-timeout') {
      if (i + 1 >= argv.length) {
        throw new Error('--deploy-timeout requires a value');
      }
      opts.deployTimeoutSeconds = Number(argv[++i]);
      continue;
    }

    if (arg === '--dust-utxo-limit') {
      if (i + 1 >= argv.length) {
        throw new Error('--dust-utxo-limit requires a value');
      }
      opts.dustUtxoLimit = Number(argv[++i]);
      continue;
    }

    if (arg === '--private-state-store') {
      if (i + 1 >= argv.length) {
        throw new Error('--private-state-store requires a value');
      }
      opts.privateStateStoreName = argv[++i];
      continue;
    }

    if (arg === '--private-state-id') {
      if (i + 1 >= argv.length) {
        throw new Error('--private-state-id requires a value');
      }
      opts.privateStateId = argv[++i];
      continue;
    }

    if (arg === '--constructor-args-json') {
      if (i + 1 >= argv.length) {
        throw new Error('--constructor-args-json requires a value');
      }
      opts.constructorArgsJson = argv[++i];
      continue;
    }

    if (arg === '--constructor-args-file') {
      if (i + 1 >= argv.length) {
        throw new Error('--constructor-args-file requires a value');
      }
      opts.constructorArgsFile = argv[++i];
      continue;
    }

    if (arg === '--zk-config-path') {
      if (i + 1 >= argv.length) {
        throw new Error('--zk-config-path requires a value');
      }
      opts.zkConfigPath = argv[++i];
      continue;
    }

    throw new Error(`Unknown argument: ${arg}`);
  }

  return opts;
}

function reviveTypedArgValue(value) {
  if (Array.isArray(value)) {
    return value.map((item) => reviveTypedArgValue(item));
  }

  if (value && typeof value === 'object') {
    if (value.__type === 'bigint') {
      if (typeof value.value !== 'string' || !/^-?[0-9]+$/.test(value.value)) {
        throw new Error('Invalid bigint constructor arg. Expected {"__type":"bigint","value":"123"}');
      }
      return BigInt(value.value);
    }

    if (value.__type === 'bytes') {
      const source = String(value.hex ?? value.value ?? '');
      const normalized = source.startsWith('0x') ? source.slice(2) : source;
      if (normalized.length === 0 || normalized.length % 2 !== 0 || !/^[0-9a-fA-F]+$/.test(normalized)) {
        throw new Error('Invalid bytes constructor arg. Expected hex string in __type bytes object');
      }
      return Buffer.from(normalized, 'hex');
    }

    const converted = {};
    for (const [key, nested] of Object.entries(value)) {
      converted[key] = reviveTypedArgValue(nested);
    }
    return converted;
  }

  if (typeof value === 'string') {
    if (/^bigint:-?[0-9]+$/.test(value)) {
      return BigInt(value.slice(7));
    }

    if (/^hex:(0x)?[0-9a-fA-F]+$/.test(value)) {
      const hex = value.slice(4).replace(/^0x/i, '');
      if (hex.length % 2 !== 0) {
        throw new Error('Invalid hex constructor arg. Hex payload must have an even number of characters');
      }
      return Buffer.from(hex, 'hex');
    }
  }

  return value;
}

function loadConstructorArgs(opts) {
  if (opts.constructorArgsJson && opts.constructorArgsFile) {
    throw new Error('Use either --constructor-args-json or --constructor-args-file, not both');
  }

  if (!opts.constructorArgsJson && !opts.constructorArgsFile) {
    return null;
  }

  const raw = opts.constructorArgsFile
    ? fs.readFileSync(path.resolve(opts.constructorArgsFile), 'utf8')
    : opts.constructorArgsJson;

  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch (error) {
    throw new Error(`Invalid constructor args JSON: ${error instanceof Error ? error.message : String(error)}`);
  }

  if (!Array.isArray(parsed) && (parsed === null || typeof parsed !== 'object')) {
    throw new Error('Constructor args JSON must be an array or object');
  }

  return reviveTypedArgValue(parsed);
}

function resolveModuleEntry(specifier, repoRoot, nodeModulesRoot) {
  if (!specifier) {
    throw new Error('Contract module specifier is empty');
  }

  if (specifier.startsWith('file://')) {
    return fileURLToPath(specifier);
  }

  if (specifier.startsWith('/') || specifier.startsWith('./') || specifier.startsWith('../')) {
    return path.resolve(repoRoot, specifier);
  }

  try {
    return BRIDGE_REQUIRE.resolve(specifier, { paths: [repoRoot, nodeModulesRoot] });
  } catch (error) {
    throw new Error(`Failed to resolve module '${specifier}': ${error instanceof Error ? error.message : String(error)}`);
  }
}

async function importModuleBySpecifier(specifier, repoRoot, nodeModulesRoot) {
  const entryPath = resolveModuleEntry(specifier, repoRoot, nodeModulesRoot);
  return import(pathToFileURL(entryPath).href);
}

function resolveContractNamespace(moduleExports, exportName) {
  if (moduleExports && Object.prototype.hasOwnProperty.call(moduleExports, exportName)) {
    return moduleExports[exportName];
  }

  if (moduleExports?.default && typeof moduleExports.default === 'object' &&
      Object.prototype.hasOwnProperty.call(moduleExports.default, exportName)) {
    return moduleExports.default[exportName];
  }

  const available = Object.keys(moduleExports ?? {});
  throw new Error(`Export '${exportName}' not found in contract module. Available exports: ${available.join(', ') || '<none>'}`);
}

function resolveContractDefinition(contractNamespace, exportName) {
  if (contractNamespace && typeof contractNamespace === 'object') {
    if (Object.prototype.hasOwnProperty.call(contractNamespace, 'Contract')) {
      return contractNamespace.Contract;
    }
    if (Object.prototype.hasOwnProperty.call(contractNamespace, 'default')) {
      return contractNamespace.default;
    }
  }

  if (contractNamespace !== undefined && contractNamespace !== null) {
    return contractNamespace;
  }

  throw new Error(`Contract export '${exportName}' is empty`);
}

async function resolveContractProfile(opts, context) {
  const requestedContractName = String(opts.contract || DEFAULT_CONTRACT).trim() || DEFAULT_CONTRACT;
  const contractKey = normalizeContractKey(requestedContractName);
  const builtin = BUILTIN_CONTRACT_PROFILES[contractKey] ?? null;
  const contractName = builtin?.contractName ?? requestedContractName;

  if (!builtin && !opts.contractModule) {
    throw new Error(
      `Unsupported contract '${requestedContractName}'. Provide --contract-module and --zk-config-path for custom contracts`,
    );
  }

  const moduleSpecifier = opts.contractModule || builtin.moduleSpecifier;
  const contractExport = opts.contractExport || builtin?.contractExport || contractName;
  const importedModule = await importModuleBySpecifier(moduleSpecifier, context.repoRoot, context.nodeModulesRoot);
  const contractNamespace = resolveContractNamespace(importedModule, contractExport);
  const contractDefinition = resolveContractDefinition(contractNamespace, contractExport);

  const constructorArgsOverride = loadConstructorArgs(opts);
  const constructorArgs = constructorArgsOverride ?? (
    builtin
      ? builtin.buildDefaultConstructorArgs({
        feeBps: opts.feeBps,
        constructorNonce: context.constructorNonce,
      })
      : []
  );

  const defaultPrivateStateStore = builtin?.privateStateStoreName ?? `${toKebabCase(contractName)}-private-state`;
  const defaultPrivateStateId = builtin?.privateStateId ?? `${toCamelCase(contractName)}PrivateState`;
  const defaultZkConfigPath = builtin
    ? path.join(context.nodeModulesRoot, ...builtin.managedAssetPathSegments)
    : '';

  const zkConfigPath = opts.zkConfigPath || defaultZkConfigPath;
  if (!zkConfigPath) {
    throw new Error('Missing zk config path for contract. Use --zk-config-path');
  }

  return {
    contractName,
    contractKey,
    contractExport,
    moduleSpecifier,
    contractDefinition,
    constructorArgs,
    privateStateStoreName: opts.privateStateStoreName || defaultPrivateStateStore,
    privateStateId: opts.privateStateId || defaultPrivateStateId,
    zkConfigPath,
  };
}

function normalizeHex(value, expectedBytes, label) {
  const raw = value.startsWith('0x') || value.startsWith('0X')
    ? value.slice(2)
    : value;

  const expectedChars = expectedBytes * 2;
  const matcher = new RegExp(`^[0-9a-fA-F]{${expectedChars}}$`);
  if (!matcher.test(raw)) {
    throw new Error(`${label} must be exactly ${expectedBytes} bytes (${expectedChars} hex chars)`);
  }

  return raw.toLowerCase();
}

function validateInteger(name, value, min, max) {
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new Error(`${name} must be an integer in range [${min}, ${max}]`);
  }
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

  return map[network] ?? map.undeployed;
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
    let subscription;

    const timer = setTimeout(() => {
      if (settled) {
        return;
      }
      settled = true;
      if (subscription) {
        subscription.unsubscribe();
      }
      reject(new Error(timeoutMessage));
    }, timeoutMs);

    subscription = observable.subscribe({
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

function getDustBalanceSafe(state) {
  try {
    const value = state?.dust?.walletBalance?.(new Date());
    if (typeof value === 'bigint') {
      return value;
    }
    if (typeof value === 'number' && Number.isFinite(value) && Number.isInteger(value)) {
      return BigInt(value);
    }
    if (typeof value === 'string' && /^-?[0-9]+$/.test(value)) {
      return BigInt(value);
    }
  } catch {
    return 0n;
  }
  return 0n;
}

function getUnshieldedBalanceSafe(state, modules) {
  try {
    const token = modules.ledger.unshieldedToken().raw;
    const value = state?.unshielded?.balances?.[token] ?? 0n;
    if (typeof value === 'bigint') {
      return value;
    }
    if (typeof value === 'number' && Number.isFinite(value) && Number.isInteger(value)) {
      return BigInt(value);
    }
    if (typeof value === 'string' && /^-?[0-9]+$/.test(value)) {
      return BigInt(value);
    }
  } catch {
    return 0n;
  }
  return 0n;
}

function getSourceAddressSafe(unshieldedKeystore) {
  try {
    if (unshieldedKeystore && typeof unshieldedKeystore.getBech32Address === 'function') {
      return unshieldedKeystore.getBech32Address();
    }
  } catch {
    return '';
  }
  return '';
}

function normalizeContractAddress(value) {
  if (typeof value === 'string') {
    return value;
  }

  if (value && typeof value === 'object') {
    if (value.bytes instanceof Uint8Array) {
      return `0x${Buffer.from(value.bytes).toString('hex')}`;
    }
    if (Array.isArray(value.bytes)) {
      return `0x${Buffer.from(value.bytes).toString('hex')}`;
    }
    if (typeof value.toString === 'function') {
      const rendered = value.toString();
      if (typeof rendered === 'string' && rendered !== '[object Object]') {
        return rendered;
      }
    }
  }

  return '';
}

function normalizeTxId(value) {
  if (typeof value === 'string') {
    return value;
  }
  if (value && typeof value.toString === 'function') {
    const rendered = value.toString();
    if (typeof rendered === 'string' && rendered !== '[object Object]') {
      return rendered;
    }
  }
  return '';
}

function signTransactionIntents(tx, signFn, proofMarker, modules) {
  if (!tx || !tx.intents || tx.intents.size === 0) {
    return;
  }

  for (const segment of tx.intents.keys()) {
    const intent = tx.intents.get(segment);
    if (!intent) {
      continue;
    }

    const cloned = modules.ledger.Intent.deserialize(
      'signature',
      proofMarker,
      'pre-binding',
      intent.serialize(),
    );

    const signatureData = cloned.signatureData(segment);
    const signature = signFn(signatureData);

    if (cloned.fallibleUnshieldedOffer) {
      const signatures = cloned.fallibleUnshieldedOffer.inputs.map((_, i) =>
        cloned.fallibleUnshieldedOffer.signatures.at(i) ?? signature,
      );
      cloned.fallibleUnshieldedOffer = cloned.fallibleUnshieldedOffer.addSignatures(signatures);
    }

    if (cloned.guaranteedUnshieldedOffer) {
      const signatures = cloned.guaranteedUnshieldedOffer.inputs.map((_, i) =>
        cloned.guaranteedUnshieldedOffer.signatures.at(i) ?? signature,
      );
      cloned.guaranteedUnshieldedOffer = cloned.guaranteedUnshieldedOffer.addSignatures(signatures);
    }

    tx.intents.set(segment, cloned);
  }
}

async function createWalletAndMidnightProvider(ctx, modules) {
  const { wallet, shieldedSecretKeys, dustSecretKey, unshieldedKeystore } = ctx;
  const synced = await waitForState(
    wallet.state(),
    (state) => Boolean(state?.isSynced),
    30_000,
    'Timed out waiting for wallet synced state',
  );

  return {
    getCoinPublicKey() {
      return synced.shielded.coinPublicKey.toHexString();
    },

    getEncryptionPublicKey() {
      return synced.shielded.encryptionPublicKey.toHexString();
    },

    async balanceTx(tx, ttl) {
      const recipe = await wallet.balanceUnboundTransaction(
        tx,
        { shieldedSecretKeys, dustSecretKey },
        { ttl: ttl ?? new Date(Date.now() + (30 * 60 * 1000)) },
      );

      const signFn = (payload) => unshieldedKeystore.signData(payload);

      try {
        signTransactionIntents(recipe.baseTransaction, signFn, 'proof', modules);
        if (recipe.balancingTransaction) {
          signTransactionIntents(recipe.balancingTransaction, signFn, 'pre-proof', modules);
        }
        return wallet.finalizeRecipe(recipe);
      } catch {
        if (typeof wallet.signRecipe === 'function') {
          const signed = await wallet.signRecipe(recipe, signFn);
          return wallet.finalizeRecipe(signed);
        }
        throw new Error('Could not sign deploy transaction with available wallet APIs');
      }
    },

    submitTx(tx) {
      return wallet.submitTransaction(tx);
    },
  };
}

async function ensureDustAvailable(wallet, unshieldedKeystore, modules, dustWaitTimeoutMs, dustUtxoLimit) {
  let state = await waitForState(
    wallet.state(),
    (value) => Boolean(value?.isSynced),
    dustWaitTimeoutMs,
    `Timed out waiting for wallet sync before dust check (${Math.floor(dustWaitTimeoutMs / 1000)}s)`,
  );

  let dustBalance = getDustBalanceSafe(state);
  if (dustBalance > 0n) {
    return {
      attempted: false,
      success: true,
      message: 'Dust already available',
      dustBalance,
      registeredUtxos: 0,
    };
  }

  const availableCoins = Array.isArray(state?.unshielded?.availableCoins)
    ? state.unshielded.availableCoins
    : [];

  const unregistered = availableCoins.filter((coin) => coin?.meta?.registeredForDustGeneration !== true);
  const batch = unregistered.slice(0, Math.max(1, dustUtxoLimit));

  if (batch.length > 0) {
    const recipe = await wallet.registerNightUtxosForDustGeneration(
      batch,
      unshieldedKeystore.getPublicKey(),
      (payload) => unshieldedKeystore.signData(payload),
    );

    const finalized = await wallet.finalizeRecipe(recipe);
    await wallet.submitTransaction(finalized);
  }

  state = await waitForState(
    wallet.state(),
    (value) => Boolean(value?.isSynced) && getDustBalanceSafe(value) > 0n,
    dustWaitTimeoutMs,
    `Dust balance did not become positive within ${Math.floor(dustWaitTimeoutMs / 1000)}s`,
  );

  dustBalance = getDustBalanceSafe(state);
  return {
    attempted: batch.length > 0,
    success: dustBalance > 0n,
    message: dustBalance > 0n ? 'Dust is available' : 'Dust unavailable after registration attempt',
    dustBalance,
    registeredUtxos: batch.length,
  };
}

async function loadModules(nodeModulesRoot) {
  const resolve = (...parts) => pathToFileURL(path.join(nodeModulesRoot, ...parts)).href;

  const [
    networkMod,
    hdMod,
    unshieldedMod,
    facadeMod,
    shieldedMod,
    dustMod,
    ledgerMod,
    wsMod,
    undiciMod,
    compactJsMod,
    contractsMod,
    proofMod,
    publicDataMod,
    zkMod,
    levelMod,
  ] = await Promise.all([
    import(resolve('@midnight-ntwrk', 'midnight-js-network-id', 'dist', 'index.mjs')),
    import(resolve('@midnight-ntwrk', 'wallet-sdk-hd', 'dist', 'index.js')),
    import(resolve('@midnight-ntwrk', 'wallet-sdk-unshielded-wallet', 'dist', 'index.js')),
    import(resolve('@midnight-ntwrk', 'wallet-sdk-facade', 'dist', 'index.js')),
    import(resolve('@midnight-ntwrk', 'wallet-sdk-shielded', 'dist', 'index.js')),
    import(resolve('@midnight-ntwrk', 'wallet-sdk-dust-wallet', 'dist', 'index.js')),
    import(resolve('@midnight-ntwrk', 'ledger-v8', 'midnight_ledger_wasm_fs.js')),
    import(resolve('ws', 'wrapper.mjs')),
    import(resolve('undici', 'index.js')),
    import(resolve('@midnight-ntwrk', 'compact-js', 'dist', 'esm', 'index.js')),
    import(resolve('@midnight-ntwrk', 'midnight-js-contracts', 'dist', 'index.mjs')),
    import(resolve('@midnight-ntwrk', 'midnight-js-http-client-proof-provider', 'dist', 'index.mjs')),
    import(resolve('@midnight-ntwrk', 'midnight-js-indexer-public-data-provider', 'dist', 'index.mjs')),
    import(resolve('@midnight-ntwrk', 'midnight-js-node-zk-config-provider', 'dist', 'index.mjs')),
    import(resolve('@midnight-ntwrk', 'midnight-js-level-private-state-provider', 'dist', 'index.mjs')),
  ]);

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
    ledger: ledgerMod,
    WebSocket: wsMod.WebSocket,
    Agent: undiciMod.Agent,
    setGlobalDispatcher: undiciMod.setGlobalDispatcher,
    CompiledContract: compactJsMod.CompiledContract,
    deployContract: contractsMod.deployContract,
    httpClientProofProvider: proofMod.httpClientProofProvider,
    indexerPublicDataProvider: publicDataMod.indexerPublicDataProvider,
    NodeZkConfigProvider: zkMod.NodeZkConfigProvider,
    levelPrivateStateProvider: levelMod.levelPrivateStateProvider,
  };
}

function emitResultAndExit(payload, exitCode) {
  process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`);
  process.exit(exitCode);
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const requestedContractName = String(opts.contract || DEFAULT_CONTRACT).trim() || DEFAULT_CONTRACT;

  if (opts.help) {
    printHelp();
    return;
  }

  const normalizedNetwork = String(opts.network).toLowerCase();
  if (!ALLOWED_NETWORKS.has(normalizedNetwork)) {
    throw new Error(`Unsupported network: ${opts.network}`);
  }
  opts.network = normalizedNetwork;

  validateInteger('fee-bps', opts.feeBps, 0, 65535);
  validateInteger('sync-timeout', opts.syncTimeoutSeconds, 10, 7200);
  validateInteger('deploy-timeout', opts.deployTimeoutSeconds, 10, 7200);
  validateInteger('dust-utxo-limit', opts.dustUtxoLimit, 1, 1000);

  if (opts.dustWaitSeconds !== null) {
    validateInteger('dust-wait', opts.dustWaitSeconds, 10, 7200);
  }

  const thisFile = fileURLToPath(import.meta.url);
  const thisDir = path.dirname(thisFile);
  const repoRoot = path.resolve(thisDir, '..');
  const nodeModulesRoot = path.join(repoRoot, 'midnight-research', 'node_modules');

  if (!fs.existsSync(nodeModulesRoot)) {
    throw new Error('Official SDK dependencies not found. Run: cd midnight-research && npm install');
  }

  const modules = await loadModules(nodeModulesRoot);

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

  const defaultEndpoints = getDefaultEndpoints(opts.network);
  const endpoints = {
    indexerUrl: opts.indexerUrl || defaultEndpoints.indexerUrl,
    indexerWsUrl: opts.indexerWsUrl || defaultEndpoints.indexerWsUrl,
    nodeUrl: opts.nodeUrl || defaultEndpoints.nodeUrl,
    proofServer: opts.proofServer || defaultEndpoints.proofServer,
  };

  if (!endpoints.indexerUrl || !endpoints.indexerWsUrl || !endpoints.nodeUrl || !endpoints.proofServer) {
    throw new Error('Missing endpoint configuration. Provide --indexer-url/--indexer-ws-url/--node-url/--proof-server');
  }

  const seedHex = opts.seedHex
    ? normalizeHex(opts.seedHex, 32, 'seed')
    : Buffer.from(modules.generateRandomSeed()).toString('hex');

  const constructorNonce = opts.initialNonceHex
    ? Buffer.from(normalizeHex(opts.initialNonceHex, 32, 'initial nonce'), 'hex')
    : randomBytes(32);

  let resolvedContractProfile;
  try {
    resolvedContractProfile = await resolveContractProfile(opts, {
      repoRoot,
      nodeModulesRoot,
      constructorNonce,
    });
  } catch (error) {
    emitResultAndExit(
      {
        success: false,
        source: 'official-midnight-sdk',
        contract: requestedContractName,
        contractExport: opts.contractExport || opts.contract,
        contractModule: opts.contractModule || '',
        network: networkId,
        error: error instanceof Error ? error.message : String(error),
      },
      1,
    );
    return;
  }

  const hd = modules.HDWallet.fromSeed(Buffer.from(seedHex, 'hex'));
  if (hd.type !== 'seedOk') {
    throw new Error('Failed to initialize HD wallet from seed');
  }

  const derived = hd.hdWallet
    .selectAccount(0)
    .selectRoles([modules.Roles.NightExternal, modules.Roles.Zswap, modules.Roles.Dust])
    .deriveKeysAt(0);

  if (derived.type !== 'keysDerived') {
    hd.hdWallet.clear();
    throw new Error('Failed to derive wallet keys');
  }

  const keys = derived.keys;
  hd.hdWallet.clear();

  const shieldedSecretKeys = modules.ledger.ZswapSecretKeys.fromSeed(keys[modules.Roles.Zswap]);
  const dustSecretKey = modules.ledger.DustSecretKey.fromSeed(keys[modules.Roles.Dust]);
  const unshieldedKeystore = modules.createKeystore(keys[modules.Roles.NightExternal], networkId);

  const walletConfigs = buildWalletConfigs(networkId, endpoints, modules);
  const shieldedWallet = modules.ShieldedWallet(walletConfigs.shieldedConfig).startWithSecretKeys(shieldedSecretKeys);
  const unshieldedWallet = modules.UnshieldedWallet(walletConfigs.unshieldedConfig).startWithPublicKey(
    modules.PublicKey.fromKeyStore(unshieldedKeystore),
  );
  const dustWallet = modules.DustWallet(walletConfigs.dustConfig).startWithSecretKey(
    dustSecretKey,
    modules.ledger.LedgerParameters.initialParameters().dust,
  );

  const wallet = new modules.WalletFacade(shieldedWallet, unshieldedWallet, dustWallet);

  const syncTimeoutMs = opts.syncTimeoutSeconds * 1000;
  const dustWaitSeconds = opts.dustWaitSeconds === null ? opts.syncTimeoutSeconds : opts.dustWaitSeconds;
  const dustWaitTimeoutMs = dustWaitSeconds * 1000;
  const deployTimeoutMs = opts.deployTimeoutSeconds * 1000;

  let state = null;
  let dustStatus = {
    attempted: false,
    success: false,
    message: '',
    dustBalance: 0n,
    registeredUtxos: 0,
  };
  let contractProfile = resolvedContractProfile;
  let resultPayload = null;
  let resultCode = 1;

  try {
    await wallet.start(shieldedSecretKeys, dustSecretKey);

    state = await waitForState(
      wallet.state(),
      (value) => Boolean(value?.isSynced),
      syncTimeoutMs,
      `Timed out waiting for wallet sync (${opts.syncTimeoutSeconds}s)`,
    );

    dustStatus = await ensureDustAvailable(
      wallet,
      unshieldedKeystore,
      modules,
      dustWaitTimeoutMs,
      opts.dustUtxoLimit,
    );

    if (!dustStatus.success || dustStatus.dustBalance <= 0n) {
      throw new Error(dustStatus.message || 'Dust is unavailable for deployment fees');
    }

    const walletAndMidnightProvider = await createWalletAndMidnightProvider(
      {
        wallet,
        shieldedSecretKeys,
        dustSecretKey,
        unshieldedKeystore,
      },
      modules,
    );

    const zkConfigPath = path.resolve(contractProfile.zkConfigPath);

    if (!fs.existsSync(zkConfigPath)) {
      throw new Error(`ZK config path not found: ${zkConfigPath}`);
    }

    const compiledContract = modules.CompiledContract.make(
      contractProfile.contractName,
      contractProfile.contractDefinition,
    ).pipe(
      modules.CompiledContract.withVacantWitnesses,
      modules.CompiledContract.withCompiledFileAssets(zkConfigPath),
    );

    const zkConfigProvider = new modules.NodeZkConfigProvider(zkConfigPath);
    const providers = {
      privateStateProvider: modules.levelPrivateStateProvider({
        privateStateStoreName: contractProfile.privateStateStoreName,
        walletProvider: walletAndMidnightProvider,
      }),
      publicDataProvider: modules.indexerPublicDataProvider(endpoints.indexerUrl, endpoints.indexerWsUrl),
      zkConfigProvider,
      proofProvider: modules.httpClientProofProvider(endpoints.proofServer, zkConfigProvider, { timeout: 3_600_000 }),
      walletProvider: walletAndMidnightProvider,
      midnightProvider: walletAndMidnightProvider,
    };

    const deployResult = await withTimeout(
      modules.deployContract(providers, {
        compiledContract,
        privateStateId: contractProfile.privateStateId,
        initialPrivateState: {},
        args: contractProfile.constructorArgs,
      }),
      deployTimeoutMs,
      `Contract deployment timed out (${opts.deployTimeoutSeconds}s)`,
    );

    const publicTxData = deployResult?.deployTxData?.public ?? {};
    const contractAddress = normalizeContractAddress(publicTxData.contractAddress);
    const txId = normalizeTxId(
      publicTxData.txId ?? publicTxData.transactionHash ?? publicTxData.hash,
    );

    if (!contractAddress) {
      throw new Error('Deployment completed but contract address is missing in response');
    }

    const finalState = await waitForState(
      wallet.state(),
      (value) => Boolean(value?.isSynced),
      30_000,
      'Timed out refreshing wallet state after deploy',
    );

    resultPayload = {
      success: true,
      source: 'official-midnight-sdk',
      contract: contractProfile.contractName,
      contractExport: contractProfile.contractExport,
      contractModule: contractProfile.moduleSpecifier,
      network: networkId,
      seedHex,
      sourceAddress: getSourceAddressSafe(unshieldedKeystore),
      feeBps: String(opts.feeBps),
      constructorNonceHex: Buffer.from(constructorNonce).toString('hex'),
      contractAddress,
      txId,
      unshieldedBalance: getUnshieldedBalanceSafe(finalState, modules).toString(),
      dustBalance: getDustBalanceSafe(finalState).toString(),
      dustRegistration: {
        attempted: dustStatus.attempted,
        success: dustStatus.success,
        message: dustStatus.message,
        registeredUtxos: dustStatus.registeredUtxos,
      },
      endpoints,
    };
    resultCode = 0;
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    const fallbackState = state;

    resultPayload = {
      success: false,
      source: 'official-midnight-sdk',
      contract: contractProfile?.contractName ?? requestedContractName,
      contractExport: contractProfile?.contractExport || opts.contractExport || opts.contract,
      contractModule: contractProfile?.moduleSpecifier || opts.contractModule || '',
      network: networkId,
      seedHex,
      sourceAddress: getSourceAddressSafe(unshieldedKeystore),
      feeBps: String(opts.feeBps),
      unshieldedBalance: getUnshieldedBalanceSafe(fallbackState, modules).toString(),
      dustBalance: getDustBalanceSafe(fallbackState).toString(),
      dustRegistration: {
        attempted: dustStatus.attempted,
        success: dustStatus.success,
        message: dustStatus.message,
        registeredUtxos: dustStatus.registeredUtxos,
      },
      endpoints,
      error: message,
    };
    resultCode = 1;
  } finally {
    try {
      if (typeof wallet.stop === 'function') {
        await wallet.stop();
      }
    } catch {
      // Best effort shutdown.
    }
  }

  emitResultAndExit(
    resultPayload ?? {
      success: false,
      source: 'official-midnight-sdk',
      error: 'Deployment bridge finished without a result payload',
    },
    resultCode,
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
