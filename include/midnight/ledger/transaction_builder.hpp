#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace midnight::ledger
{

    struct SourceConfig
    {
        std::string src_url;
        std::vector<std::string> src_files;
        bool fetch_only_cached = false;
        bool ignore_block_context = false;
        bool dust_warp = false;
        std::string fetch_cache = "redb:toolkit_cache/fetch_cache.db";
        std::string ledger_state_db = "toolkit_cache/wallet_state";
    };

    struct ToolkitConfig
    {
        /**
         * Path or executable name for the official Midnight node toolkit.
         * This must be built from midnight-research/midnight-node/util/toolkit
         * or otherwise match the running network ledger version.
         */
        std::string toolkit_binary = "midnight-node-toolkit";

        /**
         * Optional working directory. Useful when relative toolkit-js paths,
         * compiled contract directories, or cache paths are used.
         */
        std::string working_directory;

        std::string proof_server_url;
        std::string temp_directory;
        bool keep_artifacts = false;
    };

    struct BuildResult
    {
        bool success = false;
        std::vector<uint8_t> transaction_bytes;
        std::string transaction_hex;
        std::string transaction_hash;
        std::string output_file;
        std::string command;
        std::string log;
        std::string error;
    };

    struct IntentResult
    {
        bool success = false;
        std::string output_intent;
        std::string output_private_state;
        std::string output_zswap_state;
        std::optional<std::string> output_onchain_state;
        std::optional<std::string> output_result;
        std::string command;
        std::string log;
        std::string error;
    };

    struct TransferNightParams
    {
        SourceConfig source;
        std::string source_seed;
        std::optional<std::string> funding_seed;
        std::vector<std::string> destination_addresses;
        std::string amount; ///< u128 decimal string in the ledger's smallest NIGHT unit
        std::string token_type =
            "0000000000000000000000000000000000000000000000000000000000000000";
        std::vector<std::string> input_utxos; ///< "<intent_hash>#<output_index>"
        std::optional<std::string> rng_seed;  ///< 32-byte hex
        std::string coin_selection = "largest-first";
    };

    struct RegisterDustParams
    {
        SourceConfig source;
        std::string wallet_seed;
        std::optional<std::string> funding_seed;
        std::optional<std::string> destination_dust_address;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct DeregisterDustParams
    {
        SourceConfig source;
        std::string wallet_seed;
        std::string funding_seed;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct SimpleContractDeployParams
    {
        SourceConfig source;
        std::string funding_seed;
        std::vector<std::string> authority_seeds;
        std::optional<uint32_t> authority_threshold;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct SimpleContractCallParams
    {
        SourceConfig source;
        std::string funding_seed;
        std::string contract_address; ///< 32-byte contract hex, no Bech32 wallet address
        std::string call_key = "store";
        std::string fee = "1300000";  ///< u128 decimal string
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct CustomContractIntentParams
    {
        SourceConfig source;
        std::string funding_seed;
        std::vector<std::string> compiled_contract_dirs;
        std::vector<std::string> intent_files;
        std::vector<std::string> input_utxos;
        std::optional<std::string> zswap_state_file;
        std::vector<std::string> shielded_destinations;
        std::optional<std::string> rng_seed; ///< 32-byte hex
    };

    struct CustomDeployIntentParams
    {
        std::string toolkit_js_path;
        std::string config_path;
        std::string network = "preprod";
        std::string coin_public;
        std::optional<std::string> authority_seed;
        std::string output_intent;
        std::string output_private_state;
        std::string output_zswap_state;
        std::vector<std::string> constructor_args;
    };

    struct CustomCircuitIntentParams
    {
        SourceConfig source;
        std::optional<std::string> wallet_seed;
        std::string toolkit_js_path;
        std::string config_path;
        std::string contract_address;
        std::string network = "preprod";
        std::string coin_public;
        std::string input_onchain_state;
        std::string input_private_state;
        std::optional<std::string> input_zswap_state;
        std::string output_intent;
        std::optional<std::string> output_onchain_state;
        std::string output_private_state;
        std::string output_zswap_state;
        std::optional<std::string> output_result;
        std::optional<std::string> custom_ledger_parameters;
        std::string circuit_id;
        std::vector<std::string> call_args;
    };

    /**
     * @brief Production transaction builder bridge for Midnight ledger bytes.
     *
     * The canonical Midnight transaction format is the tagged binary
     * midnight-ledger `Transaction`. The node accepts those bytes through
     * pallet Midnight `send_mn_transaction`. This class deliberately delegates
     * construction to the official `midnight-node-toolkit` generated from
     * midnight-node/midnight-ledger and then parses the resulting `.mn` file.
     */
    class TransactionBuilder
    {
    public:
        explicit TransactionBuilder(ToolkitConfig config = {});

        BuildResult build_transfer_night(const TransferNightParams &params) const;
        BuildResult build_register_dust(const RegisterDustParams &params) const;
        BuildResult build_deregister_dust(const DeregisterDustParams &params) const;

        BuildResult build_simple_contract_deploy(const SimpleContractDeployParams &params) const;
        BuildResult build_simple_contract_call(const SimpleContractCallParams &params) const;

        /**
         * Build a transaction from custom Compact intents produced by
         * `midnight-node-toolkit generate-intent ...` / toolkit-js.
         */
        BuildResult build_custom_contract_transaction(
            const CustomContractIntentParams &params) const;

        IntentResult generate_custom_deploy_intent(
            const CustomDeployIntentParams &params) const;

        IntentResult generate_custom_circuit_intent(
            const CustomCircuitIntentParams &params) const;

        /**
         * Load a `.mn` file written by the toolkit and extract the raw
         * serialized Midnight transaction bytes.
         */
        static BuildResult load_serialized_transaction_file(const std::string &path);

    private:
        ToolkitConfig config_;

        BuildResult run_generate_txs(
            const SourceConfig &source,
            const std::vector<std::string> &builder_args) const;

        BuildResult run_send_intent(const CustomContractIntentParams &params) const;
    };

} // namespace midnight::ledger
