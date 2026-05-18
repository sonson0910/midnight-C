#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OWNER="${GITHUB_OWNER:-Venera-labs}"
VISIBILITY="${GITHUB_VISIBILITY:-private}"
REMOTE_NAME="${GITHUB_REMOTE_NAME:-venera-publish}"

if ! command -v gh >/dev/null 2>&1; then
  echo "gh is required. Install GitHub CLI first." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "gh is not authenticated. Run: gh auth login" >&2
  exit 1
fi

case "$VISIBILITY" in
  public|private|internal) ;;
  *)
    echo "GITHUB_VISIBILITY must be public, private, or internal." >&2
    exit 1
    ;;
esac

publish_repo() {
  local dir="$1"
  local repo="$2"
  local description="$3"
  local path="${ROOT_DIR}/${dir}"

  if [[ ! -d "${path}/.git" ]]; then
    echo "Missing git repository: ${path}" >&2
    exit 1
  fi

  if [[ -n "$(git -C "$path" status --porcelain)" ]]; then
    echo "Repository has uncommitted changes: ${dir}" >&2
    echo "Commit or stash changes before publishing." >&2
    git -C "$path" status --short
    exit 1
  fi

  local full_name="${OWNER}/${repo}"
  local branch
  branch="$(git -C "$path" branch --show-current)"
  if [[ -z "$branch" ]]; then
    branch="main"
  fi

  if ! gh repo view "$full_name" >/dev/null 2>&1; then
    gh repo create "$full_name" "--${VISIBILITY}" --description "$description"
  fi

  if git -C "$path" remote get-url "$REMOTE_NAME" >/dev/null 2>&1; then
    git -C "$path" remote set-url "$REMOTE_NAME" "https://github.com/${full_name}.git"
  else
    git -C "$path" remote add "$REMOTE_NAME" "https://github.com/${full_name}.git"
  fi

  git -C "$path" push -u "$REMOTE_NAME" "HEAD:${branch}"
}

publish_repo "midnight-research/midnight-node" \
  "midnight-research-node" \
  "Patched Midnight node/toolkit source used by the Midnight C++ SDK ledger FFI."

publish_repo "midnight-research/midnight-ledger" \
  "midnight-research-ledger" \
  "Midnight ledger reference source used for C++ SDK compatibility checks."

publish_repo "midnight-research/midnight-indexer" \
  "midnight-research-indexer" \
  "Midnight indexer reference source used for C++ SDK GraphQL/schema checks."

publish_repo "midnight-research/midnight-zk" \
  "midnight-research-zk" \
  "Midnight ZK reference source used for C++ SDK proving compatibility checks."

echo "Published Midnight research repositories under ${OWNER}."
