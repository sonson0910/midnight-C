#!/usr/bin/env node

import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

function decodeUserAddressToHex(address) {
  if (typeof address !== 'string' || address.length === 0) {
    throw new Error('beneficiary must be a non-empty address string');
  }

  if (/^(0x)?[0-9a-fA-F]{64}$/.test(address)) {
    return address.replace(/^0x/i, '').toLowerCase();
  }

  const parts = address.split('1');
  if (parts.length !== 2 || !parts[0].startsWith('mn_addr')) {
    throw new Error('beneficiary must be mn_addr... or 32-byte hex');
  }

  const charset = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l';
  const data = [];
  const payload = parts[1].toLowerCase();

  for (const ch of payload) {
    const idx = charset.indexOf(ch);
    if (idx < 0) {
      throw new Error(`beneficiary contains invalid bech32 character: ${ch}`);
    }
    data.push(idx);
  }

  if (data.length < 7) {
    throw new Error('beneficiary bech32 payload is too short');
  }

  const noChecksum = data.slice(0, -6);
  let acc = 0;
  let bits = 0;
  const out = [];
  for (const value of noChecksum) {
    acc = (acc << 5) | value;
    bits += 5;
    while (bits >= 8) {
      bits -= 8;
      out.push((acc >> bits) & 0xff);
    }
  }

  if (out.length !== 32) {
    throw new Error(`beneficiary decoded length ${out.length} != 32 bytes`);
  }

  return Buffer.from(out).toString('hex');
}

const thisFile = fileURLToPath(import.meta.url);
const repoRoot = path.resolve(path.dirname(thisFile), '..');

const DEFAULTS = {
  network: 'undeployed',
  contract: 'Vesting',
  contractExport: 'Vesting',
  privateStateId: 'vestingPrivateState',
  contractModule: path.join(repoRoot, 'contracts', 'vesting-contract', 'src', 'contract-module.mjs'),
  zkConfigPath: path.join(repoRoot, 'contracts', 'vesting-contract', 'src', 'managed', 'Vesting'),
};

function printHelp() {
  const lines = [
    'Deploy Compact Vesting Contract (normal official flow)',
    '',
    'Usage:',
    '  node tools/deploy-vesting-compact.mjs [options]',
    '',
    'Required (if no --constructor-args-json provided):',
    '  --beneficiary <mn_addr...>',
    '  --start-ts <unix-seconds>',
    '  --cliff-ts <unix-seconds>',
    '  --duration-secs <seconds>',
    '  --total-amount <decimal>',
    '',
    'Core options:',
    '  --network <id>                   preprod|preview|mainnet|undeployed (default: undeployed)',
    '  --seed <hex>                     32-byte wallet seed hex',
    '  --constructor-args-json <json>   Override constructor args directly',
    '  --contract-module <path>         Override contract module (default: contracts/vesting-contract/src/contract-module.mjs)',
    '  --contract-export <name>         Override export name (default: Vesting)',
    '  --zk-config-path <path>          Override managed assets path',
    '  --private-state-id <id>          Override private state id (default: vestingPrivateState)',
    '',
    'Pass-through deploy options:',
    '  --proof-server <url> --node-url <url> --indexer-url <url> --indexer-ws-url <url>',
    '  --sync-timeout <sec> --dust-wait <sec> --deploy-timeout <sec> --dust-utxo-limit <n>',
    '  --fee-bps <n> --initial-nonce <hex>',
    '',
    'Example:',
    '  node tools/deploy-vesting-compact.mjs --seed <hex> --beneficiary mn_addr1... --start-ts 1713500000 --cliff-ts 1716100000 --duration-secs 31536000 --total-amount 1000000000000',
  ];

  process.stdout.write(`${lines.join('\n')}\n`);
}

function isUintString(value) {
  return /^[0-9]+$/.test(value);
}

function typedBigint(value) {
  return { __type: 'bigint', value };
}

function typedBytesHex(value) {
  return { __type: 'bytes', hex: `0x${value}` };
}

function typedUserAddress(hexValue) {
  return { bytes: typedBytesHex(hexValue) };
}

function parseArgs(argv) {
  const opts = {
    help: false,
    network: DEFAULTS.network,
    seed: '',
    beneficiary: '',
    startTs: '',
    cliffTs: '',
    durationSecs: '',
    totalAmount: '',
    constructorArgsJson: '',
    contractModule: DEFAULTS.contractModule,
    contractExport: DEFAULTS.contractExport,
    zkConfigPath: DEFAULTS.zkConfigPath,
    privateStateId: DEFAULTS.privateStateId,
    passthrough: [],
  };

  const withValue = new Set([
    '--network',
    '--seed',
    '--beneficiary',
    '--start-ts',
    '--cliff-ts',
    '--duration-secs',
    '--total-amount',
    '--constructor-args-json',
    '--contract-module',
    '--contract-export',
    '--zk-config-path',
    '--private-state-id',
    '--proof-server',
    '--node-url',
    '--indexer-url',
    '--indexer-ws-url',
    '--sync-timeout',
    '--dust-wait',
    '--deploy-timeout',
    '--dust-utxo-limit',
    '--fee-bps',
    '--initial-nonce',
  ]);

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];

    if (arg === '--help' || arg === '-h') {
      opts.help = true;
      continue;
    }

    if (!arg.startsWith('--')) {
      throw new Error(`Unknown positional argument: ${arg}`);
    }

    if (!withValue.has(arg)) {
      throw new Error(`Unknown option: ${arg}`);
    }

    if (i + 1 >= argv.length) {
      throw new Error(`${arg} requires a value`);
    }

    const value = argv[++i];

    if (arg === '--network') {
      opts.network = value;
      continue;
    }
    if (arg === '--seed') {
      opts.seed = value;
      continue;
    }
    if (arg === '--beneficiary') {
      opts.beneficiary = value;
      continue;
    }
    if (arg === '--start-ts') {
      opts.startTs = value;
      continue;
    }
    if (arg === '--cliff-ts') {
      opts.cliffTs = value;
      continue;
    }
    if (arg === '--duration-secs') {
      opts.durationSecs = value;
      continue;
    }
    if (arg === '--total-amount') {
      opts.totalAmount = value;
      continue;
    }
    if (arg === '--constructor-args-json') {
      opts.constructorArgsJson = value;
      continue;
    }
    if (arg === '--contract-module') {
      opts.contractModule = value;
      continue;
    }
    if (arg === '--contract-export') {
      opts.contractExport = value;
      continue;
    }
    if (arg === '--zk-config-path') {
      opts.zkConfigPath = value;
      continue;
    }
    if (arg === '--private-state-id') {
      opts.privateStateId = value;
      continue;
    }

    opts.passthrough.push(arg, value);
  }

  if (opts.help) {
    return opts;
  }

  if (!opts.constructorArgsJson) {
    if (!opts.beneficiary || !opts.startTs || !opts.cliffTs || !opts.durationSecs || !opts.totalAmount) {
      throw new Error(
        'Missing constructor args. Provide --constructor-args-json or all vesting args: --beneficiary --start-ts --cliff-ts --duration-secs --total-amount',
      );
    }

    if (!isUintString(opts.startTs) || !isUintString(opts.cliffTs) || !isUintString(opts.durationSecs) || !isUintString(opts.totalAmount)) {
      throw new Error('start-ts, cliff-ts, duration-secs, total-amount must be unsigned decimal strings');
    }

    const beneficiaryHex = decodeUserAddressToHex(opts.beneficiary);

    const typedArgs = [
      typedUserAddress(beneficiaryHex),
      typedBigint(opts.startTs),
      typedBigint(opts.cliffTs),
      typedBigint(opts.durationSecs),
      typedBigint(opts.totalAmount),
    ];

    opts.constructorArgsJson = JSON.stringify(typedArgs);
  }

  return opts;
}

function main() {
  let opts;

  try {
    opts = parseArgs(process.argv.slice(2));
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`);
    process.stderr.write('Use --help to see valid options.\n');
    process.exit(1);
  }

  if (opts.help) {
    printHelp();
    process.exit(0);
  }

  const officialScript = path.join(repoRoot, 'tools', 'official-deploy-contract.mjs');

  const args = [
    officialScript,
    '--contract', DEFAULTS.contract,
    '--network', opts.network,
    '--contract-module', opts.contractModule,
    '--contract-export', opts.contractExport,
    '--private-state-id', opts.privateStateId,
    '--zk-config-path', opts.zkConfigPath,
    '--constructor-args-json', opts.constructorArgsJson,
  ];

  if (opts.seed) {
    args.push('--seed', opts.seed);
  }

  args.push(...opts.passthrough);

  const result = spawnSync('node', args, {
    cwd: repoRoot,
    stdio: 'inherit',
    env: process.env,
  });

  if (result.error) {
    process.stderr.write(`${result.error.message}\n`);
    process.exit(1);
  }

  process.exit(result.status ?? 1);
}

main();
