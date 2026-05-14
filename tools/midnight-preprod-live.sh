#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NETWORK="${MIDNIGHT_NETWORK:-preview}"
BUILD_DIR="${MIDNIGHT_BUILD_DIR:-$ROOT_DIR/build_codex_verify_full}"
SECRETS_DIR="${MIDNIGHT_SECRETS_DIR:-$ROOT_DIR/.secrets/midnight-$NETWORK}"
WALLET_JSON="${MIDNIGHT_WALLET_JSON:-$SECRETS_DIR/wallet_$NETWORK.json}"
ENV_FILE="${MIDNIGHT_ENV_FILE:-$SECRETS_DIR/live.env}"
NODE_URL="${MIDNIGHT_LIVE_NODE_URL:-${MIDNIGHT_NODE_URL:-https://rpc.$NETWORK.midnight.network}}"
SOURCE_URL="${MIDNIGHT_LIVE_SOURCE_URL:-${MIDNIGHT_SOURCE_URL:-wss://rpc.$NETWORK.midnight.network}}"
INDEXER_URL="${MIDNIGHT_LIVE_INDEXER_URL:-${MIDNIGHT_INDEXER_URL:-https://indexer.$NETWORK.midnight.network/api/v4/graphql}}"
FAUCET_URL="${MIDNIGHT_FAUCET_URL:-https://faucet.$NETWORK.midnight.network/}"

usage() {
  cat <<'EOF'
Usage: tools/midnight-preprod-live.sh <command>

Network defaults to preview. Override with:
  MIDNIGHT_NETWORK=preprod|preview
  MIDNIGHT_NODE_URL=https://...
  MIDNIGHT_SOURCE_URL=wss://...
  MIDNIGHT_INDEXER_URL=https://.../api/v4/graphql
  MIDNIGHT_FAUCET_URL=https://...
  MIDNIGHT_LIVE_FUNDING_TX_HASH=0x...
  MIDNIGHT_LIVE_FUNDING_BLOCK=721281
  MIDNIGHT_LIVE_ALLOW_COLD_SYNC=1   # opt into long first-time ledger sync inside build commands

Commands:
  generate-wallet     Generate a new wallet and local env file.
  print-faucet        Print the NIGHT address to paste into the faucet.
  balance             Query NIGHT/DUST wallet state through the indexer.
  sync-ledger-state   Build/update local ledger and wallet cache for tx building.
  register-dust       Build/prove/submit a DUST registration transaction.
  transfer-night      Build/prove/submit a small NIGHT transfer.
  both                Register DUST, then submit the NIGHT transfer.

The script reads secrets from .secrets/midnight-$NETWORK/live.env by default.
That directory is ignored by git.
EOF
}

require_file() {
  if [[ ! -f "$1" ]]; then
    echo "Missing file: $1" >&2
    exit 1
  fi
}

require_executable() {
  if [[ ! -x "$1" ]]; then
    echo "Missing executable: $1" >&2
    echo "Run: cmake --build \"$BUILD_DIR\"" >&2
    exit 1
  fi
}

load_env() {
  require_file "$ENV_FILE"
  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a
}

write_env_from_wallet() {
  python3 - "$WALLET_JSON" "$ENV_FILE" "$BUILD_DIR" "$NETWORK" "$NODE_URL" "$SOURCE_URL" "$INDEXER_URL" <<'PY'
import json
import pathlib
import platform
import shlex
import sys

wallet_path = pathlib.Path(sys.argv[1])
env_path = pathlib.Path(sys.argv[2])
build_dir = pathlib.Path(sys.argv[3])
network = sys.argv[4]
node_url = sys.argv[5]
source_url = sys.argv[6]
indexer_url = sys.argv[7]
data = json.loads(wallet_path.read_text())

def q(value):
    return shlex.quote(str(value))

lib_name = {
    "Darwin": "libmidnight_ledger_ffi.dylib",
    "Windows": "midnight_ledger_ffi.dll",
}.get(platform.system(), "libmidnight_ledger_ffi.so")
ledger = build_dir / "midnight-ledger-ffi" / "release" / lib_name
seed_hex = data["seed_hex"]
night = data["night_address"]
dust = data["dust_address"]
shielded = data["shielded_address"]

lines = [
    f"# Local Midnight {network} live-test secrets. Do not commit.",
    f"export MIDNIGHT_NETWORK={q(network)}",
    f"export MIDNIGHT_LEDGER_FFI_LIBRARY={q(ledger)}",
    f"export MIDNIGHT_LIVE_NODE_URL={q(node_url)}",
    f"export MIDNIGHT_LIVE_SOURCE_URL={q(source_url)}",
    f"export MIDNIGHT_LIVE_INDEXER_URL={q(indexer_url)}",
    "export MIDNIGHT_LIVE_PROOF_SERVER_URL=http://127.0.0.1:6300",
    f"export MIDNIGHT_LIVE_SOURCE_SEED={q(seed_hex)}",
    f"export MIDNIGHT_LIVE_NIGHT_ADDRESS={q(night)}",
    f"export MIDNIGHT_LIVE_DESTINATION_ADDRESS={q(night)}",
    f"export MIDNIGHT_LIVE_DUST_ADDRESS={q(dust)}",
    f"export MIDNIGHT_LIVE_SHIELDED_ADDRESS={q(shielded)}",
    "export MIDNIGHT_LIVE_AMOUNT=1000000",
    f"export MIDNIGHT_LIVE_WALLET_ID={q(network + '-local')}",
    "export MIDNIGHT_LIVE_ARTIFACT_DIR=midnight-artifacts",
    "export MIDNIGHT_LIVE_CONFIRMATION_TIMEOUT_MS=300000",
    "export MIDNIGHT_LIVE_SYNC_TIMEOUT_MS=0",
    "# Set to 1 for build/submit commands after sync-ledger-state has completed.",
    "# export MIDNIGHT_LIVE_FETCH_ONLY_CACHED=1",
    "# Build/submit commands default to cache-only to avoid hidden hour-long cold sync.",
    "# export MIDNIGHT_LIVE_ALLOW_COLD_SYNC=1",
    "# Optional fast-path after faucet funding. Fill these once faucet returns.",
    "# export MIDNIGHT_LIVE_FUNDING_TX_HASH=0x...",
    "# export MIDNIGHT_LIVE_FUNDING_BLOCK=721281",
]
env_path.write_text("\n".join(lines) + "\n")
PY
}

generate_wallet() {
  require_executable "$BUILD_DIR/bin/wallet-generator-new"
  mkdir -p "$SECRETS_DIR"
  (
    cd "$SECRETS_DIR"
    MIDNIGHT_NETWORK="$NETWORK" \
    MIDNIGHT_NODE_URL="$NODE_URL" \
    MIDNIGHT_INDEXER_BASE_URL="${INDEXER_URL%/api/v4/graphql}" \
    MIDNIGHT_FAUCET_URL="$FAUCET_URL" \
      "$BUILD_DIR/bin/wallet-generator-new" > wallet_generator_output.txt
  )
  write_env_from_wallet
  echo
  echo "Wallet JSON: $WALLET_JSON"
  echo "Env file:    $ENV_FILE"
  echo "Generator log: $SECRETS_DIR/wallet_generator_output.txt"
  echo "Paste this official NIGHT address into faucet:"
  print_faucet
}

print_faucet() {
  if [[ -f "$ENV_FILE" ]]; then
    load_env
    echo "$MIDNIGHT_LIVE_NIGHT_ADDRESS"
    echo "Faucet: $FAUCET_URL"
    return
  fi
  require_file "$WALLET_JSON"
  python3 - "$WALLET_JSON" "$FAUCET_URL" <<'PY'
import json
import sys
data = json.load(open(sys.argv[1]))
print(data["night_address"])
print(f"Faucet: {sys.argv[2]}")
PY
}

run_flow() {
  local action="$1"
  require_executable "$BUILD_DIR/bin/preprod_live_flow"
  load_env
  if [[ ! -f "${MIDNIGHT_LEDGER_FFI_LIBRARY:-}" ]]; then
    echo "Missing MIDNIGHT_LEDGER_FFI_LIBRARY: ${MIDNIGHT_LEDGER_FFI_LIBRARY:-unset}" >&2
    echo "Run: cmake --build \"$BUILD_DIR\"" >&2
    exit 1
  fi
  "$BUILD_DIR/bin/preprod_live_flow" "$action"
}

case "${1:-}" in
  generate-wallet)
    generate_wallet
    ;;
  print-faucet)
    print_faucet
    ;;
  balance)
    run_flow balance
    ;;
  sync-ledger-state)
    run_flow sync-ledger-state
    ;;
  register-dust)
    run_flow register-dust
    ;;
  transfer-night)
    run_flow transfer-night
    ;;
  both)
    run_flow both
    ;;
  ""|-h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
