#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native Midnight ledger backend ABI consumed by FfiLedgerBackend.
 * The bundled production implementation lives in
 * midnight-research/midnight-node/util/ledger-ffi and links against the
 * official Midnight ledger/toolkit crates. It is a native library boundary,
 * not a CLI/process bridge.
 */

/*
 * Return backend ABI, source, version, and capability information as JSON.
 * response_json must be released by midnight_ledger_free_string.
 */
int midnight_ledger_backend_info(char **response_json);

/*
 * Validate canonical Midnight values with the same parser stack used by the
 * native ledger/toolkit builder. request_json:
 *   {
 *     "kind": "wallet|contract-address|token-type|wallet-seed",
 *     "value": "...",
 *     "network": "preview"
 *   }
 */
int midnight_ledger_validate(const char *request_json, char **response_json);

/*
 * Inspect ledger-serialized Midnight transaction bytes. request_json:
 *   { "transaction_hex": "...", "ledger_version": 8 }
 *
 * response_json includes transaction_hash, tx_type, size_bytes, verified, and
 * contract_address when the transaction contains a deploy action.
 */
int midnight_ledger_inspect_transaction(const char *request_json, char **response_json);

/*
 * Build/prove a canonical Midnight transaction.
 *
 * request_json is a UTF-8 JSON object:
 *   {
 *     "operation": "transfer_night|register_dust|deregister_dust|deploy_simple_contract|call_simple_contract|custom_contract_transaction|sync_ledger_state|wallet_summary",
 *     "params": { ... }
 *   }
 *
 * response_json must be allocated by the backend and released by
 * midnight_ledger_free_string. The response shape is:
 *   {
 *     "success": true,
 *     "transaction_hex": "...",
 *     "transaction_hash": "...",
 *     "contract_address": "...",
 *     "output_file": "...",
 *     "intent_path": "...",
 *     "private_state_path": "...",
 *     "zswap_state_path": "...",
 *     "onchain_state_path": "...",
 *     "result_path": "...",
 *     "raw_output": "...",  // wallet_summary returns the JSON summary here
 *     "log": "..."
 *   }
 *
 * On failure, return a non-zero status and/or a JSON object with
 * success=false and error="<message>".
 */
int midnight_ledger_build_transaction(const char *request_json, char **response_json);

/*
 * Derive canonical Midnight wallet addresses and public identifiers through
 * the official ledger helpers. request_json:
 *   { "seed": "<WalletSeed hex or mnemonic>", "network": "preprod" }
 *
 * response_json:
 *   {
 *     "success": true,
 *     "network": "preprod",
 *     "shielded": "...",
 *     "unshielded": "...",
 *     "dust": "...",
 *     "dust_public": "...",
 *     "coin_public": "...",
 *     "coin_public_tagged": "...",
 *     "verifying_key": "...",
 *     "user_address": "...",
 *     "unshielded_user_address_untagged": "",
 *     "error": ""
 *   }
 */
int midnight_ledger_derive_wallet_addresses(const char *request_json, char **response_json);

void midnight_ledger_free_string(char *value);

#ifdef __cplusplus
}
#endif
