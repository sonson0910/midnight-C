# Cardano-C Library API Reference
## Complete Function Signatures and Usage Patterns

### Project Information
- **Repository**: https://github.com/Biglup/cardano-c
- **Latest Version**: 1.2.1
- **Language**: C99
- **Memory Model**: Reference counting with `_ref()` and `_unref()` functions
- **Key Features**: Conway era support, MISRA 2012 compliant, transaction builder, multi-asset support

---

## 1. TRANSACTION BUILDER API

### Main Structure
```c
typedef struct cardano_tx_builder_t cardano_tx_builder_t;
```

### Transaction Builder Lifecycle Functions

#### Create Transaction Builder
```c
// Header: lib/include/cardano/transaction_builder/transaction_builder.h
cardano_tx_builder_t* cardano_tx_builder_new(
  cardano_protocol_parameters_t* params,
  const cardano_slot_config_t*  slot_config
);
```

**Usage Pattern:**
```c
cardano_tx_builder_t* tx_builder =
  cardano_tx_builder_new(protocol_params, &CARDANO_PREPROD_SLOT_CONFIG);
```

**Slot Config Constants Available:**
- `CARDANO_MAINNET_SLOT_CONFIG`
- `CARDANO_PREVIEW_SLOT_CONFIG`
- `CARDANO_PREPROD_SLOT_CONFIG`

#### Release Transaction Builder
```c
void cardano_tx_builder_unref(cardano_tx_builder_t** transaction_builder);
void cardano_tx_builder_ref(cardano_tx_builder_t* transaction_builder);
size_t cardano_tx_builder_refcount(const cardano_tx_builder_t* transaction_builder);
```

### Configuration Functions

#### Set UTXOs and Change Address
```c
void cardano_tx_builder_set_utxos(
  cardano_tx_builder_t* builder,
  cardano_utxo_list_t* utxos
);

void cardano_tx_builder_set_change_address(
  cardano_tx_builder_t* builder,
  cardano_address_t* change_address
);

void cardano_tx_builder_set_change_address_ex(
  cardano_tx_builder_t* builder,
  const char* change_address,
  size_t address_size
);
```

#### Set Collateral
```c
void cardano_tx_builder_set_collateral_utxos(
  cardano_tx_builder_t* builder,
  cardano_utxo_list_t* utxos
);

void cardano_tx_builder_set_collateral_change_address(
  cardano_tx_builder_t* builder,
  cardano_address_t* collateral_change_address
);

void cardano_tx_builder_set_collateral_change_address_ex(
  cardano_tx_builder_t* builder,
  const char* collateral_change_address,
  size_t address_size
);
```

#### Set Network and Fees
```c
void cardano_tx_builder_set_network_id(
  cardano_tx_builder_t* builder,
  cardano_network_id_t network_id
);

void cardano_tx_builder_set_minimum_fee(
  cardano_tx_builder_t* builder,
  uint64_t minimum_fee
);

void cardano_tx_builder_set_donation(
  cardano_tx_builder_t* builder,
  uint64_t donation
);
```

#### Set Transaction Validity
```c
// Expiration (invalid after)
void cardano_tx_builder_set_invalid_after(
  cardano_tx_builder_t* builder,
  uint64_t slot
);

// Unix timestamp version
void cardano_tx_builder_set_invalid_after_ex(
  cardano_tx_builder_t* builder,
  uint64_t unix_time
);

// Minimum valid slot (invalid before)
void cardano_tx_builder_set_invalid_before(
  cardano_tx_builder_t* builder,
  uint64_t slot
);

// Unix timestamp version
void cardano_tx_builder_set_invalid_before_ex(
  cardano_tx_builder_t* builder,
  uint64_t unix_time
);
```

### Output Functions (Send Funds)

#### Send Lovelace
```c
void cardano_tx_builder_send_lovelace(
  cardano_tx_builder_t* builder,
  cardano_address_t* address,
  uint64_t amount
);

void cardano_tx_builder_send_lovelace_ex(
  cardano_tx_builder_t* builder,
  const char* address,
  size_t address_size,
  uint64_t amount
);
```

#### Send Multi-Asset Value
```c
void cardano_tx_builder_send_value(
  cardano_tx_builder_t* builder,
  cardano_address_t* address,
  cardano_value_t* value
);

void cardano_tx_builder_send_value_ex(
  cardano_tx_builder_t* builder,
  const char* address,
  size_t address_size,
  cardano_value_t* value
);
```

#### Lock at Script Address (with Datum)
```c
void cardano_tx_builder_lock_lovelace(
  cardano_tx_builder_t* builder,
  cardano_address_t* script_address,
  uint64_t amount,
  cardano_datum_t* datum
);

void cardano_tx_builder_lock_lovelace_ex(
  cardano_tx_builder_t* builder,
  const char* script_address,
  size_t script_address_size,
  uint64_t amount,
  cardano_datum_t* datum
);

void cardano_tx_builder_lock_value(
  cardano_tx_builder_t* builder,
  cardano_address_t* script_address,
  cardano_value_t* value,
  cardano_datum_t* datum
);

void cardano_tx_builder_lock_value_ex(
  cardano_tx_builder_t* builder,
  const char* script_address,
  size_t script_address_size,
  cardano_value_t* value,
  cardano_datum_t* datum
);
```

### Input Functions

#### Add Input/UTXO
```c
void cardano_tx_builder_add_input(
  cardano_tx_builder_t* builder,
  cardano_utxo_t* utxo,
  cardano_plutus_data_t* redeemer,    // Optional for Plutus
  cardano_plutus_data_t* datum        // Optional for Plutus
);
```

#### Add Reference Input
```c
void cardano_tx_builder_add_reference_input(
  cardano_tx_builder_t* builder,
  cardano_utxo_t* utxo
);
```

#### Add Output
```c
void cardano_tx_builder_add_output(
  cardano_tx_builder_t* builder,
  cardano_transaction_output_t* output
);
```

### Token Minting Functions

#### Mint Token
```c
void cardano_tx_builder_mint_token(
  cardano_tx_builder_t* builder,
  cardano_blake2b_hash_t* policy_id,
  cardano_asset_name_t* name,
  int64_t amount,                      // Positive = mint, negative = burn
  cardano_plutus_data_t* redeemer      // Optional
);

void cardano_tx_builder_mint_token_ex(
  cardano_tx_builder_t* builder,
  const char* policy_id_hex,
  size_t policy_id_size,
  const char* name_hex,
  size_t name_size,
  int64_t amount,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_mint_token_with_id(
  cardano_tx_builder_t* builder,
  cardano_asset_id_t* asset_id,
  int64_t amount,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_mint_token_with_id_ex(
  cardano_tx_builder_t* builder,
  const char* asset_id_hex,
  size_t hex_size,
  int64_t amount,
  cardano_plutus_data_t* redeemer
);
```

### Metadata Functions

#### Set Metadata
```c
void cardano_tx_builder_set_metadata(
  cardano_tx_builder_t* builder,
  uint64_t tag,
  cardano_metadatum_t* metadata
);

void cardano_tx_builder_set_metadata_ex(
  cardano_tx_builder_t* builder,
  uint64_t tag,
  const char* metadata_json,
  size_t json_size
);
```

### Staking/Reward Functions

#### Withdraw Rewards
```c
void cardano_tx_builder_withdraw_rewards(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* address,
  int64_t amount,
  cardano_plutus_data_t* redeemer      // Optional
);

void cardano_tx_builder_withdraw_rewards_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t address_size,
  int64_t amount,
  cardano_plutus_data_t* redeemer
);
```

#### Register/Deregister Reward Address
```c
void cardano_tx_builder_register_reward_address(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* address,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_register_reward_address_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t address_size,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_deregister_reward_address(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* address,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_deregister_reward_address_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t address_size,
  cardano_plutus_data_t* redeemer
);
```

#### Delegate Stake
```c
void cardano_tx_builder_delegate_stake(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* address,
  cardano_blake2b_hash_t* pool_id,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_delegate_stake_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t address_size,
  const char* pool_id,
  size_t pool_id_size,
  cardano_plutus_data_t* redeemer
);
```

### Governance Functions (Conway Era)

#### Delegate Voting Power to DRep
```c
void cardano_tx_builder_delegate_voting_power(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* address,
  cardano_drep_t* drep,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_delegate_voting_power_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t address_size,
  const char* drep_id,           // CIP-105 or CIP-129 format
  size_t drep_id_size,
  cardano_plutus_data_t* redeemer
);
```

#### Register/Update/Deregister DRep
```c
void cardano_tx_builder_register_drep(
  cardano_tx_builder_t* builder,
  cardano_drep_t* drep,
  cardano_anchor_t* anchor,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_register_drep_ex(
  cardano_tx_builder_t* builder,
  const char* drep_id,
  size_t drep_id_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_update_drep(
  cardano_tx_builder_t* builder,
  cardano_drep_t* drep,
  cardano_anchor_t* anchor,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_update_drep_ex(
  cardano_tx_builder_t* builder,
  const char* drep_id,
  size_t drep_id_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_deregister_drep(
  cardano_tx_builder_t* builder,
  cardano_drep_t* drep,
  cardano_plutus_data_t* redeemer
);

void cardano_tx_builder_deregister_drep_ex(
  cardano_tx_builder_t* builder,
  const char* drep_id,
  size_t drep_id_size,
  cardano_plutus_data_t* redeemer
);
```

#### Vote on Governance Actions
```c
void cardano_tx_builder_vote(
  cardano_tx_builder_t* builder,
  cardano_voter_t* voter,
  cardano_governance_action_id_t* action_id,
  cardano_voting_procedure_t* vote,
  cardano_plutus_data_t* redeemer
);
```

#### Propose Governance Actions
```c
// Parameter Change
void cardano_tx_builder_propose_parameter_change(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* reward_address,
  cardano_anchor_t* anchor,
  cardano_protocol_param_update_t* protocol_param_update,
  cardano_governance_action_id_t* governance_action_id,
  cardano_blake2b_hash_t* policy_hash
);

void cardano_tx_builder_propose_parameter_change_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t reward_address_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  const char* gov_action_id,
  size_t gov_action_id_size,
  const char* policy_hash_hash_hex,
  size_t policy_hash_hash_hex_size,
  cardano_protocol_param_update_t* protocol_param_update
);

// Hard Fork
void cardano_tx_builder_propose_hardfork(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* reward_address,
  cardano_anchor_t* anchor,
  cardano_protocol_version_t* version,
  cardano_governance_action_id_t* governance_action_id
);

void cardano_tx_builder_propose_hardfork_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t reward_address_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  const char* gov_action_id,
  size_t gov_action_id_size,
  uint64_t minor_protocol_version,
  uint64_t major_protocol_version
);

// Treasury Withdrawal
void cardano_tx_builder_propose_treasury_withdrawals(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* reward_address,
  cardano_anchor_t* anchor,
  cardano_withdrawal_map_t* withdrawals,
  cardano_blake2b_hash_t* policy_hash
);

void cardano_tx_builder_propose_treasury_withdrawals_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t reward_address_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  const char* policy_hash_hash_hex,
  size_t policy_hash_hash_hex_size,
  cardano_withdrawal_map_t* withdrawals
);

// No Confidence
void cardano_tx_builder_propose_no_confidence(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* reward_address,
  cardano_anchor_t* anchor,
  cardano_governance_action_id_t* governance_action_id
);

void cardano_tx_builder_propose_no_confidence_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t reward_address_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  const char* gov_action_id,
  size_t gov_action_id_size
);

// Committee Update
void cardano_tx_builder_propose_update_committee(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* reward_address,
  cardano_anchor_t* anchor,
  cardano_governance_action_id_t* governance_action_id,
  cardano_credential_set_t* members_to_be_removed,
  cardano_committee_members_map_t* members_to_be_added,
  cardano_unit_interval_t* new_quorum
);

void cardano_tx_builder_propose_update_committee_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t reward_address_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  const char* gov_action_id,
  size_t gov_action_id_size,
  cardano_credential_set_t* members_to_be_removed,
  cardano_committee_members_map_t* members_to_be_added,
  double new_quorum
);

// New Constitution
void cardano_tx_builder_propose_new_constitution(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* reward_address,
  cardano_anchor_t* anchor,
  cardano_governance_action_id_t* governance_action_id,
  cardano_constitution_t* constitution
);

void cardano_tx_builder_propose_new_constitution_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t reward_address_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size,
  const char* gov_action_id,
  size_t gov_action_id_size,
  cardano_constitution_t* constitution
);

// Info Action
void cardano_tx_builder_propose_info(
  cardano_tx_builder_t* builder,
  cardano_reward_address_t* reward_address,
  cardano_anchor_t* anchor
);

void cardano_tx_builder_propose_info_ex(
  cardano_tx_builder_t* builder,
  const char* reward_address,
  size_t reward_address_size,
  const char* metadata_url,
  size_t metadata_url_size,
  const char* metadata_hash_hex,
  size_t metadata_hash_hex_size
);
```

### Certificate and Script Functions

#### Add Certificate
```c
void cardano_tx_builder_add_certificate(
  cardano_tx_builder_t* builder,
  cardano_certificate_t* certificate,
  cardano_plutus_data_t* redeemer     // Optional
);
```

#### Add Script
```c
void cardano_tx_builder_add_script(
  cardano_tx_builder_t* builder,
  cardano_script_t* script
);
```

#### Add Datum and Signers
```c
void cardano_tx_builder_add_datum(
  cardano_tx_builder_t* builder,
  cardano_plutus_data_t* datum
);

void cardano_tx_builder_add_signer(
  cardano_tx_builder_t* builder,
  cardano_blake2b_hash_t* pub_key_hash
);

void cardano_tx_builder_add_signer_ex(
  cardano_tx_builder_t* builder,
  const char* pub_key_hash,
  size_t hash_size
);

void cardano_tx_builder_pad_signer_count(
  cardano_tx_builder_t* builder,
  size_t count
);
```

### Build and Error Functions

#### Build Transaction
```c
cardano_error_t cardano_tx_builder_build(
  cardano_tx_builder_t* builder,
  cardano_transaction_t** transaction
);
```

#### Error Handling
```c
const char* cardano_tx_builder_get_last_error(
  const cardano_tx_builder_t* transaction_builder
);

void cardano_tx_builder_set_last_error(
  cardano_tx_builder_t* transaction_builder,
  const char* message
);
```

---

## 2. ADDRESS API

### Main Structures
```c
typedef struct cardano_address_t cardano_address_t;
typedef struct cardano_base_address_t cardano_base_address_t;
typedef struct cardano_byron_address_t cardano_byron_address_t;
typedef struct cardano_enterprise_address_t cardano_enterprise_address_t;
typedef struct cardano_pointer_address_t cardano_pointer_address_t;
typedef struct cardano_reward_address_t cardano_reward_address_t;
```

### Address Creation and Parsing

#### From Bytes
```c
cardano_error_t cardano_address_from_bytes(
  const byte_t* data,
  size_t size,
  cardano_address_t** address
);
```

#### From String
```c
cardano_error_t cardano_address_from_string(
  const char* data,
  size_t size,
  cardano_address_t** address
);
```

### Address Serialization

#### To Bytes
```c
size_t cardano_address_get_bytes_size(const cardano_address_t* address);

cardano_error_t cardano_address_to_bytes(
  const cardano_address_t* address,
  byte_t* data,
  size_t size
);

const byte_t* cardano_address_get_bytes(const cardano_address_t* address);
```

#### To String
```c
size_t cardano_address_get_string_size(const cardano_address_t* address);

cardano_error_t cardano_address_to_string(
  const cardano_address_t* address,
  char* data,
  size_t size
);

const char* cardano_address_get_string(const cardano_address_t* address);
```

### Address Validation

```c
bool cardano_address_is_valid(const char* data, size_t size);
bool cardano_address_is_valid_bech32(const char* data, size_t size);
bool cardano_address_is_valid_byron(const char* data, size_t size);
```

### Address Type and Network Detection

```c
cardano_error_t cardano_address_get_type(
  const cardano_address_t* address,
  cardano_address_type_t* type
);

cardano_error_t cardano_address_get_network_id(
  const cardano_address_t* address,
  cardano_network_id_t* network_id
);
```

### Address Type Conversion

```c
cardano_byron_address_t* cardano_address_to_byron_address(
  const cardano_address_t* address
);

cardano_reward_address_t* cardano_address_to_reward_address(
  const cardano_address_t* address
);

cardano_pointer_address_t* cardano_address_to_pointer_address(
  const cardano_address_t* address
);

cardano_enterprise_address_t* cardano_address_to_enterprise_address(
  const cardano_address_t* address
);

cardano_base_address_t* cardano_address_to_base_address(
  const cardano_address_t* address
);
```

### Address Comparison and Reference Management

```c
bool cardano_address_equals(
  const cardano_address_t* lhs,
  const cardano_address_t* rhs
);

void cardano_address_unref(cardano_address_t** address);
void cardano_address_ref(cardano_address_t* address);
size_t cardano_address_refcount(const cardano_address_t* address);

const char* cardano_address_get_last_error(const cardano_address_t* address);
void cardano_address_set_last_error(cardano_address_t* address, const char* message);
```

---

## 3. TRANSACTION API

### Main Structure
```c
typedef struct cardano_transaction_t cardano_transaction_t;
typedef struct cardano_utxo_list_t cardano_utxo_list_t;
```

### Transaction Creation

#### From Components
```c
cardano_error_t cardano_transaction_new(
  cardano_transaction_body_t* body,
  cardano_witness_set_t* witness_set,
  cardano_auxiliary_data_t* auxiliary_data,  // Optional
  cardano_transaction_t** transaction
);
```

#### From CBOR
```c
cardano_error_t cardano_transaction_from_cbor(
  cardano_cbor_reader_t* reader,
  cardano_transaction_t** transaction
);
```

### Transaction Serialization

#### To CBOR
```c
cardano_error_t cardano_transaction_to_cbor(
  const cardano_transaction_t* transaction,
  cardano_cbor_writer_t* writer
);
```

#### To CIP-116 JSON
```c
cardano_error_t cardano_transaction_to_cip116_json(
  const cardano_transaction_t* tx,
  cardano_json_writer_t* writer
);
```

### Transaction Component Access

#### Get/Set Body
```c
cardano_transaction_body_t* cardano_transaction_get_body(
  cardano_transaction_t* transaction
);

cardano_error_t cardano_transaction_set_body(
  cardano_transaction_t* transaction,
  cardano_transaction_body_t* body
);
```

#### Get/Set Witness Set
```c
cardano_witness_set_t* cardano_transaction_get_witness_set(
  cardano_transaction_t* transaction
);

cardano_error_t cardano_transaction_set_witness_set(
  cardano_transaction_t* transaction,
  cardano_witness_set_t* witness_set
);
```

#### Get/Set Auxiliary Data
```c
cardano_auxiliary_data_t* cardano_transaction_get_auxiliary_data(
  cardano_transaction_t* transaction
);

cardano_error_t cardano_transaction_set_auxiliary_data(
  cardano_transaction_t* transaction,
  cardano_auxiliary_data_t* auxiliary_data
);
```

### Transaction Validity

#### Get/Set Validity Flag
```c
bool cardano_transaction_get_is_valid(cardano_transaction_t* transaction);

cardano_error_t cardano_transaction_set_is_valid(
  cardano_transaction_t* transaction,
  bool is_valid
);
```

### Transaction ID

#### Get Transaction Hash
```c
cardano_blake2b_hash_t* cardano_transaction_get_id(
  cardano_transaction_t* transaction
);
```

### Transaction Signers

#### Get Unique Signers
```c
cardano_error_t cardano_transaction_get_unique_signers(
  cardano_transaction_t* tx,
  cardano_utxo_list_t* resolved_inputs,
  cardano_blake2b_hash_set_t** unique_signers
);
```

#### Apply Vkey Witnesses
```c
cardano_error_t cardano_transaction_apply_vkey_witnesses(
  cardano_transaction_t* transaction,
  cardano_vkey_witness_set_t* new_vkeys
);
```

### Transaction Script Data

#### Check Script Data
```c
bool cardano_transaction_has_script_data(cardano_transaction_t* transaction);
```

### CBOR Cache Management

```c
void cardano_transaction_clear_cbor_cache(cardano_transaction_t* transaction);
```

### Reference Management

```c
void cardano_transaction_unref(cardano_transaction_t** transaction);
void cardano_transaction_ref(cardano_transaction_t* transaction);
size_t cardano_transaction_refcount(const cardano_transaction_t* transaction);

const char* cardano_transaction_get_last_error(const cardano_transaction_t* transaction);
void cardano_transaction_set_last_error(cardano_transaction_t* transaction, const char* message);
```

---

## 4. EXAMPLE USAGE PATTERNS

### Complete Transaction Build and Sign Example

```c
// From send_lovelace_example.c pattern

// 0.- Initialize dependencies
cardano_secure_key_handler_t*  key_handler = create_secure_key_handler(...);
cardano_provider_t*            provider = create_provider(...);
cardano_address_t*             payment_address = create_address_from_derivation_paths(...);
cardano_utxo_list_t*           utxo_list = get_unspent_utxos(provider, payment_address);
cardano_protocol_parameters_t* protocol_params = get_protocol_parameters(provider);

// Calculate 2 hours from now
const uint64_t invalid_after = cardano_utils_get_time() + (60UL * 60UL * 2UL);

// 1.- Build transaction
cardano_tx_builder_t* tx_builder =
  cardano_tx_builder_new(protocol_params, &CARDANO_PREPROD_SLOT_CONFIG);

cardano_tx_builder_set_utxos(tx_builder, utxo_list);
cardano_tx_builder_set_change_address(tx_builder, payment_address);
cardano_tx_builder_set_invalid_after_ex(tx_builder, invalid_after);
cardano_tx_builder_send_lovelace_ex(tx_builder, RECEIVING_ADDRESS,
  cardano_utils_safe_strlen(RECEIVING_ADDRESS, 128), LOVELACE_TO_SEND);

cardano_transaction_t* transaction = NULL;
cardano_error_t result = cardano_tx_builder_build(tx_builder, &transaction);

if (result != CARDANO_SUCCESS)
{
  printf("Failed to build transaction: %s\n",
    cardano_tx_builder_get_last_error(tx_builder));
  return result;
}

// 2.- Sign transaction
sign_transaction(key_handler, SIGNER_DERIVATION_PATH, transaction);

// 3.- Submit transaction & confirm
submit_transaction(provider, CONFIRM_TX_TIMEOUT_MS, transaction);

// Cleanup
cardano_provider_unref(&provider);
cardano_address_unref(&payment_address);
cardano_utxo_list_unref(&utxo_list);
cardano_protocol_parameters_unref(&protocol_params);
cardano_tx_builder_unref(&tx_builder);
cardano_transaction_unref(&transaction);
cardano_secure_key_handler_unref(&key_handler);
```

### Key Function Call Sequence for Transaction Creation

1. **Initialize Protocol Parameters**: `get_protocol_parameters(provider)`
2. **Create Transaction Builder**: `cardano_tx_builder_new(params, slot_config)`
3. **Configure Inputs**: `cardano_tx_builder_set_utxos(tx_builder, utxo_list)`
4. **Set Change Address**: `cardano_tx_builder_set_change_address(tx_builder, address)`
5. **Set Validity Window**:
   - `cardano_tx_builder_set_invalid_after_ex(tx_builder, unix_time_seconds)`
7. **Add Outputs**: `cardano_tx_builder_send_lovelace_ex(tx_builder, address, size, amount)`
8. **Build Transaction**: `cardano_tx_builder_build(tx_builder, &transaction)`
9. **Sign Transaction**: Use key handler to sign
10. **Submit Transaction**: Submit to blockchain via provider
11. **Cleanup**: Call `_unref()` on all allocated objects

---

## 5. KEY TYPES AND TYPEDEFS

```c
// Core types
typedef struct cardano_blake2b_hash_t cardano_blake2b_hash_t;
typedef struct cardano_asset_name_t cardano_asset_name_t;
typedef struct cardano_asset_id_t cardano_asset_id_t;
typedef struct cardano_value_t cardano_value_t;
typedef struct cardano_utxo_t cardano_utxo_t;
typedef struct cardano_output_t cardano_transaction_output_t;
typedef struct cardano_datum_t cardano_datum_t;
typedef struct cardano_plutus_data_t cardano_plutus_data_t;
typedef struct cardano_script_t cardano_script_t;
typedef struct cardano_metadatum_t cardano_metadatum_t;
typedef struct cardano_certificate_t cardano_certificate_t;
typedef struct cardano_cbor_reader_t cardano_cbor_reader_t;
typedef struct cardano_cbor_writer_t cardano_cbor_writer_t;

// Network types
typedef enum { CARDANO_MAINNET = 1, CARDANO_TESTNET = 0 } cardano_network_id_t;

// Error type
typedef int cardano_error_t;
#define CARDANO_SUCCESS 0
```

---

## 6. MEMORY MANAGEMENT PATTERN

All cardano-c objects use reference counting:

```c
// Create object (ref count = 1)
cardano_some_type_t* obj = cardano_some_type_new(...);

// Increment reference count
cardano_some_type_ref(obj);

// Decrement reference count (when ref count = 0, object is freed)
cardano_some_type_unref(&obj);  // obj will be set to NULL

// Get reference count for debugging
size_t count = cardano_some_type_refcount(obj);

// Get/Set error messages
const char* error = cardano_some_type_get_last_error(obj);
cardano_some_type_set_last_error(obj, "Error message");
```

---

## 7. IMPORTANT CONSTANTS

### Slot Config Constants
```c
// From cardano/slot_config.h
extern const cardano_slot_config_t CARDANO_MAINNET_SLOT_CONFIG;
extern const cardano_slot_config_t CARDANO_PREVIEW_SLOT_CONFIG;
extern const cardano_slot_config_t CARDANO_PREPROD_SLOT_CONFIG;
```

### Network Magic Constants
```c
// From cardano/common/network_magic.h
#define CARDANO_NETWORK_MAGIC_MAINNET   764824073
#define CARDANO_NETWORK_MAGIC_PREVIEW   2
#define CARDANO_NETWORK_MAGIC_PREPROD   1
```

---

## 8. DOCUMENTATION REFERENCES

- **Main Documentation**: https://cardano-c.readthedocs.io/
- **Code Examples**: https://github.com/Biglup/cardano-c/tree/main/examples/src
- **API Headers**: https://github.com/Biglup/cardano-c/tree/main/lib/include/cardano
- **License**: Apache License 2.0

---

## 9. WRAPPER INTEGRATION GUIDELINES

When creating C++ wrappers around these functions:

1. **Memory Management**: Wrap `_ref()` and `_unref()` in C++ constructors/destructors
2. **Error Handling**: Convert `cardano_error_t` to exceptions or error codes
3. **String Handling**: Convert `const char*` + `size_t` pairs to `std::string`
4. **Address Strings**:
   - Mainnet: `addr1...` prefix
   - Testnet: `addr_test1...` prefix
   - Reward: `stake1...` or `stake_test1...` prefix
5. **Amounts**: All ADA amounts are in **lovelace** (1 ADA = 1,000,000 lovelace)
6. **Timeouts**: Use Unix timestamps (seconds) for validity windows
7. **Hashes**: Hex-encoded strings for policy IDs, asset names, and governance action IDs
