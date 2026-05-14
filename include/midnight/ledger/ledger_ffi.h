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
 *
 * request_json is a UTF-8 JSON object:
 *   {
 *     "operation": "transfer_night|register_dust|deregister_dust|deploy_simple_contract|call_simple_contract|custom_contract_transaction",
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
 *     "raw_output": "...",
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
