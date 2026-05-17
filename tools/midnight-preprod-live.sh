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
  MIDNIGHT_LIVE_SYNC_AUTO_RESUME=1   # auto-restart sync-ledger-state on transient fetch failure
  MIDNIGHT_LIVE_SYNC_MAX_RESTARTS=20
  MIDNIGHT_LIVE_SOURCE_MODE=local-cache|cold-sync|bounded|auto
  MIDNIGHT_LIVE_BALANCE_SOURCE=local-cache|funding-tx|funding-block|indexer-scan

Commands:
  generate-wallet     Generate a new wallet and local env file.
  print-faucet        Print the NIGHT address to paste into the faucet.
  proof-health        Check the configured proof server /health endpoint.
  balance             Query NIGHT/DUST wallet state, defaulting to local ledger cache.
  sync-status         Print sync process, cache sizes, and recent sync log.
  watch-checkpoint    Watch sync checkpoint, cache activity, rate, and ETA.
  sync-ledger-state   Build/update local ledger and wallet cache for tx building.
  enable-local-mode   Persist cache-only local transaction build mode in live.env.
  local-mode-status   Check whether local cache mode is ready for build/submit.
  export-cache [file] Export fetch/wallet cache for reuse on another machine.
  import-cache <file> Import fetch/wallet cache created by export-cache.
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
  local override_file
  override_file="$(mktemp "${TMPDIR:-/tmp}/midnight-env-overrides.XXXXXX")"
  local name
  while IFS= read -r name; do
    if [[ "$name" == MIDNIGHT_* ]]; then
      printf 'export %s=%q\n' "$name" "${!name}" >> "$override_file"
    fi
  done < <(compgen -e)

  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a

  # shellcheck disable=SC1090
  source "$override_file"
  rm -f "$override_file"
}

write_env_from_wallet() {
  python3 - "$WALLET_JSON" "$ENV_FILE" "$BUILD_DIR" "$NETWORK" "$NODE_URL" "$SOURCE_URL" "$INDEXER_URL" "$ROOT_DIR" <<'PY'
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
workspace = pathlib.Path(sys.argv[8])
data = json.loads(wallet_path.read_text())

def q(value):
    return shlex.quote(str(value))

lib_name = {
    "Darwin": "libmidnight_ledger_ffi.dylib",
    "Windows": "midnight_ledger_ffi.dll",
}.get(platform.system(), "libmidnight_ledger_ffi.so")
ledger = build_dir / "midnight-ledger-ffi" / "release" / lib_name
fetch_cache = workspace / "midnight_cache" / "live_submit_fetch_cache.db"
ledger_state_db = workspace / "midnight_cache" / "live_submit_ledger_state_db"
artifact_dir = workspace / "midnight-artifacts"
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
    "export MIDNIGHT_LIVE_SOURCE_MODE=local-cache",
    f"export MIDNIGHT_LIVE_SOURCE_SEED={q(seed_hex)}",
    f"export MIDNIGHT_LIVE_NIGHT_ADDRESS={q(night)}",
    f"export MIDNIGHT_LIVE_DESTINATION_ADDRESS={q(night)}",
    f"export MIDNIGHT_LIVE_DUST_ADDRESS={q(dust)}",
    f"export MIDNIGHT_LIVE_SHIELDED_ADDRESS={q(shielded)}",
    "export MIDNIGHT_LIVE_AMOUNT=1000000",
    f"export MIDNIGHT_LIVE_WALLET_ID={q(network + '-local')}",
    f"export MIDNIGHT_LIVE_FETCH_CACHE={q('redb:' + str(fetch_cache))}",
    f"export MIDNIGHT_LIVE_LEDGER_STATE_DB={q(ledger_state_db)}",
    "export MIDNIGHT_LIVE_DUST_WARP=1",
    "export MIDNIGHT_LIVE_FETCH_CONCURRENCY=4",
    "export MIDNIGHT_LIVE_FETCH_RETRY_COUNT=5",
    "export MIDNIGHT_LIVE_FETCH_RETRY_DELAY_MS=5000",
    f"export MIDNIGHT_LIVE_ARTIFACT_DIR={q(artifact_dir)}",
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

set_env_exports() {
  local env_file="$1"
  shift
  require_file "$env_file"
  python3 - "$env_file" "$@" <<'PY'
import pathlib
import shlex
import sys

env_path = pathlib.Path(sys.argv[1])
updates = {}
for item in sys.argv[2:]:
    key, value = item.split("=", 1)
    updates[key] = value

lines = env_path.read_text().splitlines()
seen = set()
out = []
for line in lines:
    stripped = line.strip()
    replaced = False
    if stripped.startswith("export ") and "=" in stripped:
        key = stripped[len("export "):].split("=", 1)[0].strip()
        if key in updates:
            out.append(f"export {key}={shlex.quote(updates[key])}")
            seen.add(key)
            replaced = True
    if not replaced:
        out.append(line)

missing = [(key, value) for key, value in updates.items() if key not in seen]
if missing:
    out.append("")
    out.append("# Midnight local-cache production mode")
for key, value in missing:
    out.append(f"export {key}={shlex.quote(value)}")

env_path.write_text("\n".join(out).rstrip() + "\n")
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
  (
    cd "$ROOT_DIR"
    if [[ "$action" == "sync-ledger-state" ]]; then
      run_sync_ledger_state
    else
      "$BUILD_DIR/bin/preprod_live_flow" "$action"
    fi
  )
}

is_retryable_sync_error() {
  local error="$1"
  local lower
  lower="$(printf '%s' "$error" | tr '[:upper:]' '[:lower:]')"
  case "$lower" in
    *"fetch task error"*|*"worker thread panicked"*|*"request timeout"*|*"rpc error"*|*"background task closed"*|*"connection closed"*|*"restart required"*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

sync_process_running() {
  pgrep -f "$BUILD_DIR/bin/preprod_live_flow.*sync-ledger-state" >/dev/null 2>&1 ||
    pgrep -f "$ROOT_DIR/tools/midnight-preprod-live.sh sync-ledger-state" >/dev/null 2>&1
}

run_sync_ledger_state() {
  local log_file="$ROOT_DIR/midnight_cache/sync-ledger-state.log"
  mkdir -p "$ROOT_DIR/midnight_cache"
  : > "$log_file"

  local auto_resume="${MIDNIGHT_LIVE_SYNC_AUTO_RESUME:-1}"
  local max_restarts="${MIDNIGHT_LIVE_SYNC_MAX_RESTARTS:-20}"
  local restart_delay="${MIDNIGHT_LIVE_SYNC_RESTART_DELAY_SECONDS:-10}"
  if ! [[ "$max_restarts" =~ ^[0-9]+$ ]] || (( max_restarts < 1 )); then
    echo "MIDNIGHT_LIVE_SYNC_MAX_RESTARTS must be a positive integer" >&2
    exit 2
  fi
  if ! [[ "$restart_delay" =~ ^[0-9]+$ ]]; then
    echo "MIDNIGHT_LIVE_SYNC_RESTART_DELAY_SECONDS must be an integer" >&2
    exit 2
  fi

  local attempt=1
  while (( attempt <= max_restarts )); do
    {
      echo "midnight-live-helper: sync attempt $attempt/$max_restarts started at $(date '+%Y-%m-%d %H:%M:%S %z')"
      echo "midnight-live-helper: auto_resume=$auto_resume"
    } | tee -a "$log_file"

    local flow_status
    set +e
    "$BUILD_DIR/bin/preprod_live_flow" sync-ledger-state 2>&1 | tee -a "$log_file"
    flow_status=${PIPESTATUS[0]}
    set -e

    if (( flow_status == 0 )); then
      echo "midnight-live-helper: sync completed successfully on attempt $attempt" | tee -a "$log_file"
      return 0
    fi

    local error_line error_text
    error_line="$(grep '^error=' "$log_file" | tail -n 1 || true)"
    error_text="${error_line#error=}"
    if [[ "$auto_resume" != "1" ]] || ! is_retryable_sync_error "$error_text"; then
      echo "midnight-live-helper: sync stopped after non-retryable error: ${error_text:-unknown}" | tee -a "$log_file"
      return "$flow_status"
    fi

    if (( attempt >= max_restarts )); then
      echo "midnight-live-helper: sync exhausted $max_restarts attempt(s); last error: ${error_text:-unknown}" | tee -a "$log_file"
      return "$flow_status"
    fi

    echo "midnight-live-helper: retryable error '${error_text:-unknown}', resuming after ${restart_delay}s" | tee -a "$log_file"
    sleep "$restart_delay"
    attempt=$((attempt + 1))
  done
}

proof_health() {
  load_env
  local url="${MIDNIGHT_LIVE_PROOF_SERVER_URL:-${MIDNIGHT_PROOF_SERVER_URL:-http://127.0.0.1:6300}}"
  local health_url="${url%/}/health"
  echo "Checking proof server: $health_url"
  if ! /usr/bin/curl -fsS --connect-timeout 2 --max-time 5 "$health_url"; then
    echo "Proof server is not reachable at $health_url" >&2
    echo "Set MIDNIGHT_LIVE_PROOF_SERVER_URL or start the proof server before live build/submit." >&2
    return 1
  fi
}

file_age_seconds() {
  local path="$1"
  if [[ ! -e "$path" ]]; then
    return 1
  fi
  local modified
  modified="$(stat -f "%m" "$path" 2>/dev/null || stat -c "%Y" "$path" 2>/dev/null || true)"
  if [[ -z "$modified" ]]; then
    return 1
  fi
  echo "$(($(date +%s) - modified))"
}

file_size_bytes() {
  local path="$1"
  if [[ ! -e "$path" ]]; then
    return 1
  fi
  stat -f "%z" "$path" 2>/dev/null || stat -c "%s" "$path" 2>/dev/null
}

path_has_data() {
  local path="$1"
  if [[ ! -e "$path" ]]; then
    return 1
  fi
  if [[ -f "$path" ]]; then
    local size
    size="$(file_size_bytes "$path" 2>/dev/null || echo 0)"
    (( size > 0 ))
    return
  fi
  if [[ -d "$path" ]]; then
    [[ -n "$(find "$path" -type f -size +0c -print -quit 2>/dev/null)" ]]
    return
  fi
  return 1
}

human_bytes() {
  awk -v bytes="${1:-0}" '
    BEGIN {
      value = bytes + 0
      split("B KiB MiB GiB TiB", units, " ")
      unit = 1
      while (value >= 1024 && unit < 5) {
        value = value / 1024
        unit++
      }
      if (unit == 1) {
        printf("%d%s", value, units[unit])
      } else {
        printf("%.1f%s", value, units[unit])
      }
    }'
}

human_duration() {
  awk -v seconds="${1:-0}" '
    BEGIN {
      s = int(seconds + 0)
      if (s < 0) {
        print "n/a"
        exit
      }
      d = int(s / 86400)
      s = s % 86400
      h = int(s / 3600)
      s = s % 3600
      m = int(s / 60)
      s = s % 60
      if (d > 0) {
        printf("%dd%02dh", d, h)
      } else if (h > 0) {
        printf("%dh%02dm", h, m)
      } else if (m > 0) {
        printf("%dm%02ds", m, s)
      } else {
        printf("%ds", s)
      }
    }'
}

checkpoint_snapshot() {
  local log_file="$1"
  if [[ ! -f "$log_file" ]]; then
    echo "0|0|0|0|0.000|missing-log|"
    return
  fi

  local source_line checkpoint_line
  source_line="$(grep 'midnight-toolkit-fetcher: source range' "$log_file" | tail -n 1 || true)"
  checkpoint_line="$(grep 'fetch checkpoint: verified through block' "$log_file" | tail -n 1 || true)"

  awk -v source="$source_line" -v checkpoint="$checkpoint_line" '
    BEGIN {
      min = 0
      finalized = 0
      current = 0
      state = "waiting"

      if (source != "") {
        min_text = source
        finalized_text = source
        sub(/^.*min_height=/, "", min_text)
        sub(/,.*/, "", min_text)
        sub(/^.*finalized_height=/, "", finalized_text)
        sub(/,.*/, "", finalized_text)
        min = min_text + 0
        finalized = finalized_text + 0
        state = "source-loaded"
      }

      if (checkpoint != "") {
        current_text = checkpoint
        sub(/^.*block /, "", current_text)
        current = current_text + 0
        state = "checkpointing"
      }

      total = finalized - min
      done = current - min
      if (done < 0) {
        done = 0
      }
      remaining = finalized - current
      if (remaining < 0) {
        remaining = 0
      }
      pct = total > 0 ? (done / total) * 100.0 : 0
      printf("%d|%d|%d|%d|%.3f|%s|%s\n", min, finalized, current, remaining, pct, state, source)
    }'
}

checkpoint_age_from_log() {
  local log_file="$1"
  if [[ ! -f "$log_file" ]]; then
    echo "-1"
    return
  fi
  awk '
    /midnight-ledger-ffi: sync_ledger_state: still loading source transactions after [0-9]+s/ {
      heartbeat_text = $0
      sub(/^.*after /, "", heartbeat_text)
      sub(/s;.*/, "", heartbeat_text)
      heartbeat = heartbeat_text + 0
    }
    /fetch checkpoint: verified through block/ {
      checkpoint_heartbeat = heartbeat
      seen_checkpoint = 1
    }
    END {
      if (seen_checkpoint && heartbeat >= checkpoint_heartbeat) {
        print heartbeat - checkpoint_heartbeat
      } else {
        print "-1"
      }
    }' "$log_file"
}

sync_terminal_state() {
  local log_file="$1"
  if [[ ! -f "$log_file" ]]; then
    echo "unknown|"
    return
  fi

  local success_line error_line
  success_line="$(grep '^success=' "$log_file" | tail -n 1 || true)"
  error_line="$(grep '^error=' "$log_file" | tail -n 1 || true)"
  if [[ "$success_line" == "success=1" ]]; then
    echo "success|"
  elif [[ "$success_line" == "success=0" ]]; then
    echo "failed|${error_line#error=}"
  else
    echo "running|"
  fi
}

sync_status() {
  load_env
  local fetch_cache="${MIDNIGHT_LIVE_FETCH_CACHE#redb:}"
  local ledger_state="${MIDNIGHT_LIVE_LEDGER_STATE_DB:-$ROOT_DIR/midnight_cache/live_submit_ledger_state_db}"
  local log_file="$ROOT_DIR/midnight_cache/sync-ledger-state.log"

  echo "Process:"
  ps aux | grep -E 'preprod_live_flow|sync-ledger-state' | grep -v grep || true
  echo
  echo "Cache:"
  du -sh "$fetch_cache" "$ledger_state" 2>/dev/null || true
  if [[ -f "$fetch_cache" ]]; then
    stat -f "  fetch size=%z bytes modified=%Sm" "$fetch_cache" 2>/dev/null || true
    local fetch_age
    fetch_age="$(file_age_seconds "$fetch_cache" || true)"
    if [[ -n "$fetch_age" ]]; then
      if (( fetch_age < 120 )); then
        echo "  fetch activity=active (${fetch_age}s since last write)"
      else
        echo "  fetch activity=stale (${fetch_age}s since last write)"
      fi
    fi
  fi
  if [[ -e "$ledger_state" ]]; then
    stat -f "  ledger modified=%Sm" "$ledger_state" 2>/dev/null || true
    local ledger_age
    ledger_age="$(file_age_seconds "$ledger_state" || true)"
    if [[ -n "$ledger_age" ]]; then
      echo "  ledger activity=${ledger_age}s since last write"
    fi
  fi
  echo
  echo "Sync progress:"
  if [[ -f "$log_file" ]]; then
    local snapshot min finalized checkpoint remaining pct state source_line
    snapshot="$(checkpoint_snapshot "$log_file")"
    IFS='|' read -r min finalized checkpoint remaining pct state source_line <<< "$snapshot"
    if [[ -n "$source_line" ]]; then
      echo "  $source_line"
    fi
    if (( checkpoint > 0 )); then
      echo "  checkpoint=$checkpoint remaining=$remaining progress=${pct}%"
      local checkpoint_age
      checkpoint_age="$(checkpoint_age_from_log "$log_file")"
      if (( checkpoint_age >= 0 )); then
        echo "  checkpoint unchanged for approximately $(human_duration "$checkpoint_age") of sync heartbeat time"
      fi
    else
      echo "  No checkpoint logged yet."
    fi
  else
    echo "  No log file at $log_file."
  fi

  if [[ -f "$log_file" ]]; then
    local terminal terminal_state terminal_error
    terminal="$(sync_terminal_state "$log_file")"
    IFS='|' read -r terminal_state terminal_error <<< "$terminal"
    if [[ "$terminal_state" == "failed" ]]; then
      echo "  last result=failed error=${terminal_error:-unknown}"
    elif [[ "$terminal_state" == "success" ]]; then
      echo "  last result=success"
    fi
  fi
  echo
  echo "Open cache files:"
  local pids
  pids="$(pgrep -f "$BUILD_DIR/bin/preprod_live_flow" || true)"
  if [[ -n "$pids" ]]; then
    while read -r pid; do
      [[ -z "$pid" ]] && continue
      lsof -p "$pid" 2>/dev/null | grep -E 'midnight_cache|live_submit|ledger|fetch' || true
    done <<< "$pids"
  fi
  echo
  echo "Recent log:"
  if [[ -f "$log_file" ]]; then
    tail -n 40 "$log_file"
  else
    echo "No log file at $log_file. Run sync with tee if you want persistent logs."
  fi
}

local_mode_status() {
  load_env
  local fetch_cache="${MIDNIGHT_LIVE_FETCH_CACHE#redb:}"
  local ledger_state="${MIDNIGHT_LIVE_LEDGER_STATE_DB:-$ROOT_DIR/midnight_cache/live_submit_ledger_state_db}"
  local log_file="$ROOT_DIR/midnight_cache/sync-ledger-state.log"
  local mode="${MIDNIGHT_LIVE_SOURCE_MODE:-auto}"
  local fetch_only="${MIDNIGHT_LIVE_FETCH_ONLY_CACHED:-0}"
  local allow_cold="${MIDNIGHT_LIVE_ALLOW_COLD_SYNC:-0}"

  echo "Local mode:"
  echo "  source_mode=$mode"
  echo "  fetch_only_cached=$fetch_only"
  echo "  allow_cold_sync=$allow_cold"
  echo "  fetch_cache=$fetch_cache"
  echo "  ledger_state_db=$ledger_state"

  echo
  echo "Cache readiness:"
  if [[ -f "$fetch_cache" ]]; then
    local fetch_size
    fetch_size="$(file_size_bytes "$fetch_cache" 2>/dev/null || echo 0)"
    echo "  fetch_cache=present size=$(human_bytes "$fetch_size")"
  else
    echo "  fetch_cache=missing"
  fi
  local ledger_ready=0
  if path_has_data "$ledger_state"; then
    ledger_ready=1
  fi
  if [[ -e "$ledger_state" ]]; then
    if du -sh "$ledger_state" >/dev/null 2>&1; then
      local ledger_height
      ledger_height="$(find "$ledger_state" -path '*/ledger/*.zstd' -type f -name '*.zstd' -print 2>/dev/null \
        | sed -E 's#^.*/([0-9]{12})\.zstd$#\1#' \
        | sort -n \
        | tail -n 1 \
        | sed -E 's/^0+//; s/^$/0/' || true)"
      if [[ -n "$ledger_height" ]]; then
        echo "  ledger_state=present $(du -sh "$ledger_state" 2>/dev/null | awk '{print $1}') latest_height=$ledger_height"
      else
        echo "  ledger_state=present $(du -sh "$ledger_state" 2>/dev/null | awk '{print $1}')"
      fi
    else
      echo "  ledger_state=present"
    fi
  else
    echo "  ledger_state=missing"
  fi

  echo
  echo "Sync checkpoint:"
  if [[ -f "$log_file" ]]; then
    local snapshot min finalized checkpoint remaining pct state source_line terminal terminal_state terminal_error
    snapshot="$(checkpoint_snapshot "$log_file")"
    IFS='|' read -r min finalized checkpoint remaining pct state source_line <<< "$snapshot"
    terminal="$(sync_terminal_state "$log_file")"
    IFS='|' read -r terminal_state terminal_error <<< "$terminal"
    echo "  checkpoint=$checkpoint finalized=$finalized remaining=$remaining progress=${pct}%"
    echo "  last_result=$terminal_state${terminal_error:+ error=$terminal_error}"
  else
    echo "  no sync log yet"
  fi

  echo
  if [[ "$mode" == "local-cache" && "$fetch_only" == "1" && "$allow_cold" != "1" && "$ledger_ready" == "1" ]]; then
    echo "ready=1"
  else
    echo "ready=0"
    echo "hint=Run sync-ledger-state until success, then run enable-local-mode."
  fi
}

enable_local_mode() {
  set_env_exports "$ENV_FILE" \
    "MIDNIGHT_LIVE_SOURCE_MODE=local-cache" \
    "MIDNIGHT_LIVE_FETCH_ONLY_CACHED=1" \
    "MIDNIGHT_LIVE_ALLOW_COLD_SYNC=0" \
    "MIDNIGHT_LIVE_REQUIRE_LEDGER_STATE_CACHE=1"
  echo "Enabled local-cache mode in $ENV_FILE"
  echo
  local_mode_status
}

watch_checkpoint() {
  local interval="${1:-10}"
  local once=0
  if [[ "$interval" == "once" || "$interval" == "--once" ]]; then
    once=1
    interval=0
  fi
  if ! [[ "$interval" =~ ^[0-9]+$ ]]; then
    echo "Usage: tools/midnight-preprod-live.sh watch-checkpoint [seconds|once]" >&2
    exit 2
  fi
  if (( interval == 0 && once == 0 )); then
    interval=10
  fi

  load_env
  local fetch_cache="${MIDNIGHT_LIVE_FETCH_CACHE#redb:}"
  local log_file="$ROOT_DIR/midnight_cache/sync-ledger-state.log"
  local prev_checkpoint=""
  local prev_time=""
  local last_checkpoint_change_time=""

  echo "Watching Midnight ledger sync checkpoint."
  echo "Log:   $log_file"
  echo "Cache: $fetch_cache"
  echo "Stop with Ctrl-C."
  echo

  while true; do
    local now epoch snapshot min finalized checkpoint remaining pct state source_line
    epoch="$(date +%s)"
    now="$(date '+%Y-%m-%d %H:%M:%S %z')"
    snapshot="$(checkpoint_snapshot "$log_file")"
    IFS='|' read -r min finalized checkpoint remaining pct state source_line <<< "$snapshot"

    local fetch_size fetch_size_human fetch_age cache_state
    fetch_size="$(file_size_bytes "$fetch_cache" 2>/dev/null || echo 0)"
    fetch_size_human="$(human_bytes "$fetch_size")"
    fetch_age="$(file_age_seconds "$fetch_cache" 2>/dev/null || echo -1)"
    cache_state="missing"
    if (( fetch_age >= 0 && fetch_age < 120 )); then
      cache_state="active"
    elif (( fetch_age >= 120 )); then
      cache_state="stale"
    fi

    local running_state
    if sync_process_running; then
      running_state="running"
    else
      running_state="not-running"
    fi

    local terminal terminal_state terminal_error
    terminal="$(sync_terminal_state "$log_file")"
    IFS='|' read -r terminal_state terminal_error <<< "$terminal"
    if [[ "$running_state" != "running" && "$terminal_state" == "failed" ]]; then
      running_state="failed"
    elif [[ "$running_state" != "running" && "$terminal_state" == "success" ]]; then
      running_state="completed"
    fi

    local delta dt delta_window rate eta movement
    delta="n/a"
    dt="n/a"
    delta_window="n/a"
    rate="n/a"
    eta="n/a"
    movement="baseline"
    if (( checkpoint == 0 )); then
      movement="waiting"
    fi
    if [[ -z "$last_checkpoint_change_time" ]]; then
      last_checkpoint_change_time="$epoch"
    fi
    if [[ -n "$prev_checkpoint" && -n "$prev_time" && "$checkpoint" =~ ^[0-9]+$ ]]; then
      dt=$((epoch - prev_time))
      delta=$((checkpoint - prev_checkpoint))
      delta_window="${delta}/${dt}s"
      if (( delta > 0 && dt > 0 )); then
        rate="$(awk -v blocks="$delta" -v seconds="$dt" 'BEGIN { printf("%.2f", blocks / seconds) }')"
        eta="$(awk -v remaining="$remaining" -v rate="$rate" 'BEGIN { if (rate > 0) printf("%d", remaining / rate); else print "-1" }')"
        eta="$(human_duration "$eta")"
        movement="advancing"
        last_checkpoint_change_time="$epoch"
      elif (( checkpoint > 0 )); then
        movement="no-new-checkpoint"
      fi
    fi

    local checkpoint_age heartbeat_age same_for_seconds same_for
    heartbeat_age="$(checkpoint_age_from_log "$log_file")"
    same_for_seconds=$((epoch - last_checkpoint_change_time))
    if (( heartbeat_age > same_for_seconds )); then
      checkpoint_age="$heartbeat_age"
    else
      checkpoint_age="$same_for_seconds"
    fi
    if (( checkpoint == 0 || checkpoint_age < 0 )); then
      same_for="n/a"
    else
      same_for="$(human_duration "$checkpoint_age")"
    fi

    printf '%s | %s | %s | checkpoint=%s/%s | same_for=%s | remaining=%s | progress=%s%% | delta=%s | rate=%s blk/s | eta=%s | cache=%s age=%ss %s\n' \
      "$now" "$running_state" "$movement" "$checkpoint" "$finalized" "$same_for" "$remaining" "$pct" "$delta_window" "$rate" "$eta" "$fetch_size_human" "$fetch_age" "$cache_state"

    if [[ "$state" == "missing-log" ]]; then
      echo "  No log file yet. Start: $ROOT_DIR/tools/midnight-preprod-live.sh sync-ledger-state"
    elif [[ "$state" == "waiting" ]]; then
      echo "  Waiting for source range. The Rust fetcher has not emitted min/finalized height yet."
    elif [[ -n "$source_line" ]]; then
      echo "  $source_line"
    fi

    local last_issue
    last_issue="$(grep -E 'Request timeout|retrying source load attempt|worker thread panicked|success=0|error=' "$log_file" 2>/dev/null | tail -n 1 || true)"
    if [[ -n "$last_issue" ]]; then
      echo "  last issue: $last_issue"
    fi
    if [[ "$terminal_state" == "failed" ]]; then
      echo "  status: sync exited with error=${terminal_error:-unknown}; restart sync to resume from checkpoint=$checkpoint."
    elif [[ "$terminal_state" == "success" ]]; then
      echo "  status: sync completed."
    fi
    if [[ "$movement" == "no-new-checkpoint" && "$cache_state" == "stale" ]]; then
      echo "  status: checkpoint and cache writes are both stale; likely waiting on RPC/fetch retry."
    elif [[ "$movement" == "no-new-checkpoint" && "$cache_state" == "active" && "$checkpoint_age" =~ ^[0-9]+$ && "$checkpoint_age" -ge 300 ]]; then
      echo "  status: cache is still active, but no contiguous verified block range has completed for $(human_duration "$checkpoint_age")."
      echo "  hint: if this keeps growing past 10-15 minutes, stop and restart sync; it will resume from checkpoint=$checkpoint."
    fi
    echo

    prev_checkpoint="$checkpoint"
    prev_time="$epoch"
    if (( once == 1 )); then
      break
    fi
    sleep "$interval"
  done
}

export_cache() {
  load_env
  local output="${1:-$ROOT_DIR/midnight_cache/midnight-$NETWORK-cache-$(date +%Y%m%d%H%M%S).tar.gz}"
  mkdir -p "$(dirname "$output")"
  (
    cd "$ROOT_DIR"
    tar -czf "$output" \
      midnight_cache/live_submit_fetch_cache.db \
      midnight_cache/live_submit_ledger_state_db \
      midnight_cache/sync-ledger-state.log 2>/dev/null || \
    tar -czf "$output" \
      midnight_cache/live_submit_fetch_cache.db \
      midnight_cache/live_submit_ledger_state_db 2>/dev/null || \
    tar -czf "$output" \
      midnight_cache/live_submit_fetch_cache.db
  )
  echo "$output"
}

import_cache() {
  local archive="${1:-}"
  if [[ -z "$archive" ]]; then
    echo "Usage: tools/midnight-preprod-live.sh import-cache <cache.tar.gz>" >&2
    exit 2
  fi
  require_file "$archive"
  mkdir -p "$ROOT_DIR/midnight_cache"
  tar -xzf "$archive" -C "$ROOT_DIR"
  sync_status
}

case "${1:-}" in
  generate-wallet)
    generate_wallet
    ;;
  print-faucet)
    print_faucet
    ;;
  proof-health)
    proof_health
    ;;
  balance)
    run_flow balance
    ;;
  sync-status)
    sync_status
    ;;
  watch-checkpoint|watch-sync)
    watch_checkpoint "${2:-10}"
    ;;
  sync-ledger-state)
    run_flow sync-ledger-state
    ;;
  enable-local-mode)
    enable_local_mode
    ;;
  local-mode-status)
    local_mode_status
    ;;
  export-cache)
    export_cache "${2:-}"
    ;;
  import-cache)
    import_cache "${2:-}"
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
