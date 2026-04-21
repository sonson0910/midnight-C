# Vesting Compact Contract

This package contains a simple Vesting contract written in Compact and is designed to be deployed through the existing official deploy flow.

## 1. Compile managed artifacts

Run from repository root:

```bash
cd contracts/vesting-contract
npm install
npm run compact
```

After compile, required deploy assets are:

- `contracts/vesting-contract/src/contract-module.mjs`
- `contracts/vesting-contract/src/managed/Vesting`

## 2. Deploy (normal flow)

Run from repository root:

```bash
node tools/deploy-vesting-compact.mjs \
  --seed <64-hex-seed> \
  --beneficiary <mn_addr...> \
  --start-ts 1713500000 \
  --cliff-ts 1716100000 \
  --duration-secs 31536000 \
  --total-amount 1000000000000
```

### Optional endpoint overrides

```bash
node tools/deploy-vesting-compact.mjs \
  --seed <64-hex-seed> \
  --beneficiary <mn_addr...> \
  --start-ts 1713500000 \
  --cliff-ts 1716100000 \
  --duration-secs 31536000 \
  --total-amount 1000000000000 \
  --network undeployed \
  --proof-server http://127.0.0.1:6300 \
  --node-url http://127.0.0.1:9944 \
  --indexer-url http://127.0.0.1:8088/api/v3/graphql \
  --indexer-ws-url ws://127.0.0.1:8088/api/v3/graphql/ws
```

## 3. Deploy with explicit constructor JSON

```bash
node tools/deploy-vesting-compact.mjs \
  --seed <64-hex-seed> \
  --constructor-args-json '["mn_addr...",{"__type":"bigint","value":"1713500000"},{"__type":"bigint","value":"1716100000"},{"__type":"bigint","value":"31536000"},{"__type":"bigint","value":"1000000000000"}]'
```
