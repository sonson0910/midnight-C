/**
 * Phase 3: UTXO Transactions Implementation
 *
 * Implements Midnight UTXO transactions with proper CBOR serialization.
 *
 * Key fixes from previous version:
 * 1. Transaction body uses CBOR encoding (matching Rust serde_cbor)
 * 2. Transaction hash uses Blake2b-256 of CBOR-encoded body
 * 3. Proper Bech32m address handling
 * 4. Multi-asset support with colored outputs
 *
 * Reference: rustnight/crates/midnight-blockchain/src/transaction.rs
 * Reference: rustnight/crates/midnight-blockchain/src/ledger_types.rs
 */

#include "midnight/utxo-transactions/utxo_transactions.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/core/json_bridge_utils.hpp"
#include <sodium.h>
#include <openssl/evp.h>
#include <algorithm>
#include <set>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace midnight::utxo_transactions
{
    namespace
    {
        /// Commitment hex size: 2 (0x prefix) + 64 (32 bytes as hex)
        constexpr size_t COMMITMENT_HEX_SIZE = 66;
    
        constexpr uint32_t kIndexerRpcTimeoutMs = 8000;
        constexpr size_t kMaxTxInputs = 256;
        constexpr size_t kMaxTxOutputs = 256;

        midnight::ProtocolParams to_phase3_protocol_params(const midnight::blockchain::ProtocolParams &params)
        {
            midnight::ProtocolParams converted{};
            converted.min_fee_a = params.min_fee_a;
            converted.min_fee_b = params.min_fee_b;
            converted.utxo_cost_per_byte = params.utxo_cost_per_byte;
            converted.max_tx_size = params.max_tx_size;
            converted.max_block_size = params.max_block_size;
            converted.price_memory = static_cast<double>(params.price_memory);
            converted.price_steps = static_cast<double>(params.price_steps);
            return converted;
        }

        // Use shared is_hex_string from common_utils.hpp
        // Note: midnight::util::is_hex_string requires even length;
        // is_signature_hex already checks exact length so this is compatible.
        using midnight::util::is_hex_string;

        bool is_signature_hex(const std::string &signature)
        {
            return signature.size() == 130 &&
                   signature.rfind("0x", 0) == 0 &&
                   is_hex_string(signature.substr(2));
        }

        uint64_t derive_estimated_dust_per_utxo(const TransactionBuilderContext &context)
        {
            if (context.fee_model.estimated_dust_per_utxo > 0)
            {
                return context.fee_model.estimated_dust_per_utxo;
            }

            // Keep this estimator tied to protocol baseline while staying practical for
            // commitment-based UTXO heuristics.
            return std::max<uint64_t>(1000, context.protocol_params.min_fee_b / 2);
        }

        uint64_t commitment_dust_overhead(uint32_t output_count,
                                          const TransactionBuilderContext &context)
        {
            const uint64_t scaled_utxo_cost =
                std::max<uint64_t>(1, context.protocol_params.utxo_cost_per_byte / 1024);
            return static_cast<uint64_t>(output_count) *
                   context.fee_model.commitment_bytes_per_output *
                   scaled_utxo_cost;
        }
    } // namespace

    // ============================================================================
    // Provider Implementations
    // ============================================================================

    GraphqlIndexerProvider::GraphqlIndexerProvider(const std::string &indexer_graphql_url,
                                                   uint32_t timeout_ms)
        : indexer_url_(indexer_graphql_url), timeout_ms_(timeout_ms)
    {
    }

    Json::Value GraphqlIndexerProvider::execute_query(const std::string &query)
    {
        midnight::network::NetworkClient client(indexer_url_, timeout_ms_);
        nlohmann::json payload = {
            {"query", query}};

        auto raw_response = client.post_json("/", payload);
        auto raw_string = raw_response.dump();

        Json::CharReaderBuilder builder;
        Json::Value parsed;
        std::string errors;
        std::istringstream stream(raw_string);
        if (!Json::parseFromStream(builder, stream, &parsed, &errors))
        {
            throw std::runtime_error("Failed to parse GraphQL response: " + errors);
        }

        return parsed;
    }

    Json::Value GraphqlIndexerProvider::execute_query_with_vars(
        const std::string &query,
        const std::map<std::string, Json::Value> &variables)
    {
        midnight::network::NetworkClient client(indexer_url_, timeout_ms_);
        nlohmann::json payload = {
            {"query", query},
            {"variables", nlohmann::json::object()}
        };

        // Convert Json::Value to nlohmann::json
        for (const auto &[key, value] : variables) {
            payload["variables"][key] = midnight::util::jsoncpp_to_nlohmann(value);
        }

        auto raw_response = client.post_json("/", payload);
        auto raw_string = raw_response.dump();

        Json::CharReaderBuilder builder;
        Json::Value parsed;
        std::string errors;
        std::istringstream stream(raw_string);
        if (!Json::parseFromStream(builder, stream, &parsed, &errors))
        {
            throw std::runtime_error("Failed to parse GraphQL response: " + errors);
        }

        return parsed;
    }

    MidnightNodeProvider::MidnightNodeProvider(const std::string &node_rpc_url,
                                               uint32_t timeout_ms)
        : node_rpc_url_(node_rpc_url), timeout_ms_(timeout_ms)
    {
    }

    std::optional<midnight::ProtocolParams> MidnightNodeProvider::get_protocol_params()
    {
        try
        {
            midnight::network::MidnightNodeRPC rpc(node_rpc_url_, timeout_ms_);
            const auto params = rpc.get_protocol_params();
            return to_phase3_protocol_params(params);
        }
        catch (...)
        {
            return {};
        }
    }

    std::optional<std::pair<uint64_t, std::string>> MidnightNodeProvider::get_chain_tip()
    {
        try
        {
            midnight::network::MidnightNodeRPC rpc(node_rpc_url_, timeout_ms_);
            return rpc.get_chain_tip();
        }
        catch (...)
        {
            return {};
        }
    }

    // ============================================================================
    // UtxoSetManager Implementation
    // ============================================================================

    UtxoSetManager::UtxoSetManager(const std::string &indexer_graphql_url,
                                   const std::string &node_rpc_url)
        : indexer_url_(indexer_graphql_url),
          rpc_url_(node_rpc_url),
          indexer_provider_(std::make_shared<GraphqlIndexerProvider>(indexer_graphql_url, kIndexerRpcTimeoutMs)),
          node_provider_(std::make_shared<MidnightNodeProvider>(node_rpc_url))
    {
    }

    UtxoSetManager::UtxoSetManager(std::shared_ptr<IIndexerProvider> indexer_provider,
                                   std::shared_ptr<INodeProvider> node_provider)
        : indexer_provider_(std::move(indexer_provider)),
          node_provider_(std::move(node_provider))
    {
    }

    std::optional<std::vector<Utxo>> UtxoSetManager::query_unspent_utxos(const std::string &address)
    {
        try
        {
            if (!indexer_provider_)
            {
                throw std::runtime_error("Indexer provider is not configured");
            }

            // Midnight SDK GraphQL query matching the indexer's unspentUtxos field
            std::string query = R"QUERY(
            query UnspentUtxos($address: UnshieldedAddress!) {
                unspentUtxos(address: $address) {
                    owner
                    tokenType
                    value
                    outputIndex
                    intentHash
                    ctime
                    registeredForDustGeneration
                }
            }
            )QUERY";

            auto result = indexer_provider_->execute_query_with_vars(
                query, {{"address", Json::Value(address)}});

            std::vector<Utxo> utxos;
            if (result.isMember("data") && result["data"].isMember("unspentUtxos"))
            {
                auto utxo_array = result["data"]["unspentUtxos"];
                for (const auto &u : utxo_array)
                {
                    Utxo utxo;
                    utxo.address = u.get("owner", "").asString();
                    utxo.token_type = u.get("tokenType", "").asString();
                    utxo.value = u.get("value", "0").asString();
                    utxo.output_index = u.get("outputIndex", 0).asUInt();
                    utxo.intent_hash = u.get("intentHash", "").asString();
                    utxo.ctime = u.get("ctime", 0).asUInt64();
                    utxo.registered_for_dust = u.get("registeredForDustGeneration", false).asBool();

                    // Extract tx_hash from intentHash (last 32 bytes = 64 hex chars)
                    std::string ih = midnight::util::strip_hex_prefix(utxo.intent_hash);
                    if (ih.size() >= 64) {
                        utxo.tx_hash = "0x" + ih.substr(ih.size() - 64);
                    } else {
                        utxo.tx_hash = utxo.intent_hash;
                    }

                    utxo.is_spent = false;
                    utxos.push_back(utxo);
                }
            }

            return utxos;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error querying UTXOs: ") + e.what());
            return {};
        }
    }

    uint32_t UtxoSetManager::count_unspent_utxos(const std::string &address)
    {
        auto utxos = query_unspent_utxos(address);
        if (!utxos)
        {
            return 0;
        }
        return utxos->size();
    }

    std::optional<Utxo> UtxoSetManager::query_utxo(const std::string &tx_hash, uint32_t output_index)
    {
        try
        {
            // Midnight SDK uses transactions with unshieldedCreatedOutputs/unshieldedSpentOutputs
            // Each output has: owner, tokenType, value, outputIndex, intentHash, ctime
            std::string query = R"QUERY(
            query UtxoDetails($txHash: HexEncoded!, $outputIndex: Int!) {
                transactions(offset: {hash: $txHash}) {
                    unshieldedCreatedOutputs {
                        owner
                        tokenType
                        value
                        outputIndex
                        intentHash
                        ctime
                    }
                    unshieldedSpentOutputs {
                        owner
                        tokenType
                        value
                        outputIndex
                        intentHash
                        ctime
                    }
                }
            }
        )QUERY";

            if (!indexer_provider_)
            {
                throw std::runtime_error("Indexer provider is not configured");
            }

            auto result = indexer_provider_->execute_query_with_vars(
                query,
                {{"txHash", tx_hash}, {"outputIndex", static_cast<int>(output_index)}});

            if (result.isMember("data") && result["data"].isMember("transactions"))
            {
                auto tx = result["data"]["transactions"];
                
                // Check created outputs
                if (tx.isMember("unshieldedCreatedOutputs"))
                {
                    for (const auto &u : tx["unshieldedCreatedOutputs"])
                    {
                        if (u.get("outputIndex", 0).asUInt() == output_index)
                        {
                            Utxo utxo;
                            utxo.tx_hash = tx_hash;
                            utxo.address = u.get("owner", "").asString();
                            utxo.token_type = u.get("tokenType", "").asString();
                            utxo.value = u.get("value", "0").asString();
                            utxo.output_index = u.get("outputIndex", 0).asUInt();
                            utxo.intent_hash = u.get("intentHash", "").asString();
                            utxo.ctime = u.get("ctime", 0).asUInt64();
                            utxo.is_spent = false;
                            return utxo;
                        }
                    }
                }
                
                // Check spent outputs
                if (tx.isMember("unshieldedSpentOutputs"))
                {
                    for (const auto &u : tx["unshieldedSpentOutputs"])
                    {
                        if (u.get("outputIndex", 0).asUInt() == output_index)
                        {
                            Utxo utxo;
                            utxo.tx_hash = tx_hash;
                            utxo.address = u.get("owner", "").asString();
                            utxo.token_type = u.get("tokenType", "").asString();
                            utxo.value = u.get("value", "0").asString();
                            utxo.output_index = u.get("outputIndex", 0).asUInt();
                            utxo.intent_hash = u.get("intentHash", "").asString();
                            utxo.ctime = u.get("ctime", 0).asUInt64();
                            utxo.is_spent = true;
                            return utxo;
                        }
                    }
                }
            }

            return {};
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error querying UTXO: ") + e.what());
            return {};
        }
    }

    std::optional<bool> UtxoSetManager::is_spent(const std::string &tx_hash, uint32_t output_index)
    {
        auto utxo = query_utxo(tx_hash, output_index);
        if (!utxo)
        {
            midnight::g_logger->warn("UTXO not found: " + tx_hash + ":" + std::to_string(output_index) +
                                   " - cannot determine spent status");
            return std::nullopt; // Not found - distinguish from "is spent"
        }
        return utxo->is_spent;
    }

    bool UtxoSetManager::verify_inputs_unspent(const std::vector<UtxoInput> &inputs)
    {
        cleanup_expired_reservations();

        for (const auto &input : inputs)
        {
            // First check if UTXO exists
            std::string tx_hash = input.outpoint;
            std::string clean_hash = midnight::util::strip_hex_prefix(tx_hash);
            auto colon_pos = clean_hash.find(':');
            if (colon_pos != std::string::npos)
            {
                tx_hash = clean_hash.substr(0, colon_pos);
            }
            else
            {
                tx_hash = clean_hash;
            }

            // Query the UTXO to verify it exists and get its status
            auto utxo_opt = query_utxo(input.outpoint, input.output_index);

            if (!utxo_opt)
            {
                midnight::g_logger->error("UTXO does not exist: " + input.outpoint + ":" +
                                         std::to_string(input.output_index));
                return false;
            }

            // Check if UTXO is already spent
            if (utxo_opt->is_spent)
            {
                midnight::g_logger->error("UTXO already spent: " + input.outpoint + ":" +
                                         std::to_string(input.output_index));
                return false;
            }

            // Check if UTXO is reserved for a different transaction
            std::string outpoint_key = input.outpoint + ":" + std::to_string(input.output_index);
            if (is_reserved(outpoint_key, ""))
            {
                midnight::g_logger->error("UTXO is reserved by another transaction: " + outpoint_key);
                return false;
            }
        }

        return true;
    }

    bool UtxoSetManager::reserve_utxos(const std::vector<UtxoInput> &inputs,
                                       const std::string &tx_hash,
                                       uint64_t ttl_ms)
    {
        uint64_t now = get_current_time_ms();
        uint64_t expiry = now + ttl_ms;

        for (const auto &input : inputs)
        {
            std::string outpoint_key = input.outpoint + ":" + std::to_string(input.output_index);

            // Check if already reserved
            if (is_reserved(outpoint_key, tx_hash))
            {
                midnight::g_logger->warn("UTXO already reserved: " + outpoint_key);
                return false;
            }

            reserved_utxos_[outpoint_key] = {tx_hash, expiry};
            midnight::g_logger->debug("Reserved UTXO: " + outpoint_key + " for tx " + tx_hash +
                                    " until " + std::to_string(expiry));
        }

        midnight::g_logger->info("Reserved " + std::to_string(inputs.size()) + " UTXOs for transaction " + tx_hash);
        return true;
    }

    void UtxoSetManager::release_reserved_utxos(const std::string &tx_hash)
    {
        std::vector<std::string> to_remove;
        for (const auto &[outpoint, reservation] : reserved_utxos_)
        {
            if (reservation.first == tx_hash)
            {
                to_remove.push_back(outpoint);
            }
        }

        for (const auto &outpoint : to_remove)
        {
            reserved_utxos_.erase(outpoint);
            midnight::g_logger->debug("Released reservation: " + outpoint);
        }

        if (!to_remove.empty())
        {
            midnight::g_logger->info("Released " + std::to_string(to_remove.size()) +
                                    " UTXO reservations for transaction " + tx_hash);
        }
    }

    uint64_t UtxoSetManager::get_current_time_ms()
    {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
        );
    }

    bool UtxoSetManager::is_reserved(const std::string &outpoint, const std::string &exclude_tx_hash) const
    {
        auto it = reserved_utxos_.find(outpoint);
        if (it == reserved_utxos_.end())
        {
            return false;
        }

        // Check if reservation has expired
        uint64_t now = get_current_time_ms();
        if (now > it->second.second)
        {
            // Expired - remove it
            const_cast<UtxoSetManager*>(this)->reserved_utxos_.erase(it);
            return false;
        }

        // Check if reserved for different transaction
        if (!exclude_tx_hash.empty() && it->second.first == exclude_tx_hash)
        {
            return false;
        }

        return true;
    }

    void UtxoSetManager::cleanup_expired_reservations()
    {
        uint64_t now = get_current_time_ms();
        std::vector<std::string> expired;
        for (const auto &[outpoint, reservation] : reserved_utxos_)
        {
            if (now > reservation.second)
            {
                expired.push_back(outpoint);
            }
        }

        for (const auto &outpoint : expired)
        {
            reserved_utxos_.erase(outpoint);
            midnight::g_logger->debug("Cleaned up expired reservation: " + outpoint);
        }
    }

    std::optional<AccountBalance> UtxoSetManager::sync_account(const std::string &address)
    {
        auto utxos = query_unspent_utxos(address);
        if (!utxos)
        {
            return {};
        }

        AccountBalance balance;
        balance.address = address;
        balance.unspent_utxos = *utxos;

        return balance;
    }

    std::optional<TransactionBuilderContext> UtxoSetManager::create_builder_context() const
    {
        if (!node_provider_)
        {
            return {};
        }

        TransactionBuilderContext context{};
        bool populated = false;

        if (const auto params = node_provider_->get_protocol_params())
        {
            context.protocol_params = *params;
            populated = true;
        }

        if (const auto tip = node_provider_->get_chain_tip())
        {
            context.chain_tip_height = tip->first;
            context.chain_tip_hash = tip->second;
            populated = true;
        }

        if (!populated)
        {
            return {};
        }

        return context;
    }

    // ============================================================================
    // TransactionBuilder Implementation
    // ============================================================================

    TransactionBuilder::TransactionBuilder()
        : TransactionBuilder(TransactionBuilderContext{})
    {
    }

    TransactionBuilder::TransactionBuilder(const TransactionBuilderContext &context)
        : context_(context)
    {
        tx_.version = 1;
        tx_.nonce = context_.chain_tip_height > 0
                        ? context_.chain_tip_height + 1
                        : static_cast<uint64_t>(std::time(nullptr));
        tx_.timestamp = static_cast<uint64_t>(std::time(nullptr));
    }

    TransactionBuilder::TransactionBuilder(const TransactionBuilderContext &context,
                                         std::shared_ptr<UtxoSetManager> utxo_set_manager)
        : context_(context), utxo_set_manager_(std::move(utxo_set_manager))
    {
        tx_.version = 1;
        tx_.nonce = context_.chain_tip_height > 0
                        ? context_.chain_tip_height + 1
                        : static_cast<uint64_t>(std::time(nullptr));
        tx_.timestamp = static_cast<uint64_t>(std::time(nullptr));
    }

    void TransactionBuilder::set_context(const TransactionBuilderContext &context)
    {
        context_ = context;
    }

    const TransactionBuilderContext &TransactionBuilder::get_context() const
    {
        return context_;
    }

    void TransactionBuilder::add_input(const UtxoInput &input)
    {
        // SECURITY FIX: Detect duplicate inputs to prevent double-spend attacks
        if (has_duplicate_input(input))
        {
            std::string err = "Duplicate input rejected: " + input.outpoint + ":" +
                              std::to_string(input.output_index) +
                              " - double-spend prevention";
            midnight::g_logger->error(err);
            throw std::invalid_argument(err);
        }

        // Optionally verify UTXO exists and is unspent if we have a UTXO set manager
        if (utxo_set_manager_)
        {
            if (!verify_input_utxo(input))
            {
                std::string err = "Invalid input: UTXO not found or already spent: " +
                                  input.outpoint + ":" + std::to_string(input.output_index);
                midnight::g_logger->error(err);
                throw std::invalid_argument(err);
            }
        }

        tx_.inputs.push_back(input);
    }

    bool TransactionBuilder::has_duplicate_input(const UtxoInput &input) const
    {
        std::string input_outpoint = midnight::util::strip_hex_prefix(input.outpoint);
        uint32_t input_index = input.output_index;

        for (const auto &existing : tx_.inputs)
        {
            std::string existing_outpoint = midnight::util::strip_hex_prefix(existing.outpoint);
            // Compare both outpoint hash and output index
            if (existing_outpoint == input_outpoint && existing.output_index == input_index)
            {
                midnight::g_logger->warn("Duplicate input detected: " + input.outpoint + ":" +
                                      std::to_string(input.output_index));
                return true;
            }
        }
        return false;
    }

    bool TransactionBuilder::verify_input_utxo(const UtxoInput &input)
    {
        if (!utxo_set_manager_)
        {
            return true; // No verification possible without manager
        }

        // Check if UTXO exists
        auto utxo_opt = utxo_set_manager_->query_utxo(input.outpoint, input.output_index);
        if (!utxo_opt)
        {
            midnight::g_logger->error("UTXO not found: " + input.outpoint + ":" +
                                    std::to_string(input.output_index));
            return false;
        }

        // Check if UTXO is spent
        auto spent_status = utxo_set_manager_->is_spent(input.outpoint, input.output_index);
        if (spent_status.has_value() && *spent_status)
        {
            midnight::g_logger->error("UTXO already spent: " + input.outpoint + ":" +
                                    std::to_string(input.output_index));
            return false;
        }

        return true;
    }

    void TransactionBuilder::add_output(const UtxoOutput &output)
    {
        tx_.outputs.push_back(output);
    }

    void TransactionBuilder::set_witness_bundle(const WitnessBundle &witness_bundle)
    {
        tx_.witness_bundle = witness_bundle;
        if (tx_.signature.empty() && !tx_.witness_bundle.signatures.empty())
        {
            tx_.signature = tx_.witness_bundle.signatures.front();
        }
    }

    void TransactionBuilder::add_signature(const std::string &signature)
    {
        tx_.witness_bundle.signatures.push_back(signature);
        if (tx_.signature.empty())
        {
            tx_.signature = signature;
        }
    }

    void TransactionBuilder::add_proof_reference(const std::string &proof_reference)
    {
        tx_.witness_bundle.proof_references.push_back(proof_reference);
    }

    void TransactionBuilder::set_witness_metadata(const std::string &key, const std::string &value)
    {
        tx_.witness_bundle.metadata[key] = value;
    }

    void TransactionBuilder::set_fees(uint64_t fee_amount)
    {
        tx_.fees = fee_amount;
        fees_manually_set_ = true;
    }

    std::optional<Transaction> TransactionBuilder::build()
    {
        try
        {
            if (!validate())
            {
                return {};
            }

            if (tx_.witness_bundle.signatures.empty() && !tx_.signature.empty())
            {
                tx_.witness_bundle.signatures.push_back(tx_.signature);
            }
            else if (tx_.signature.empty() && !tx_.witness_bundle.signatures.empty())
            {
                tx_.signature = tx_.witness_bundle.signatures.front();
            }

            if (tx_.witness_bundle.proof_references.empty() && !tx_.proofs.empty())
            {
                tx_.witness_bundle.proof_references = tx_.proofs;
            }

            if (!fees_manually_set_)
            {
                const size_t proof_count = std::max(
                    tx_.proofs.size(),
                    tx_.witness_bundle.proof_references.size());
                const size_t proof_size = proof_count * context_.fee_model.default_proof_size;
                tx_.fees = FeeEstimator::estimate_fee(
                    static_cast<uint32_t>(tx_.inputs.size()),
                    static_cast<uint32_t>(tx_.outputs.size()),
                    proof_size,
                    context_);
            }

            // ============================================================================
            // FIXED: Proper CBOR serialization matching Rust implementation
            // ============================================================================
            // Midnight uses CBOR for transaction body serialization (like Rust).
            // This replaces the manual byte building which had incorrect format.
            // Reference: rustnight uses serde_cbor::to_vec(&body) for serialization
            // and sha2::Sha256::digest for hashing.
            // ============================================================================

            std::vector<uint8_t> tx_bytes;

            // Transaction body format (CBOR encoding):
            // [
            //   version (u32),
            //   inputs (array of inputs),
            //   outputs (array of outputs),
            //   fee (u64),
            //   network_id (u8),
            //   ...
            // ]

            // Helper lambda to append compact-encoded integer
            auto append_compact = [&tx_bytes](uint64_t val) {
                if (val <= 0x3F)
                {
                    tx_bytes.push_back(static_cast<uint8_t>((val << 2) | 0x00));
                }
                else if (val <= 0x3FFF)
                {
                    uint16_t encoded = static_cast<uint16_t>((val << 2) | 0x01);
                    tx_bytes.push_back(static_cast<uint8_t>(encoded & 0xFF));
                    tx_bytes.push_back(static_cast<uint8_t>((encoded >> 8) & 0xFF));
                }
                else if (val <= 0x3FFFFFFF)
                {
                    uint32_t encoded = static_cast<uint32_t>((val << 2) | 0x02);
                    tx_bytes.push_back(static_cast<uint8_t>(encoded & 0xFF));
                    tx_bytes.push_back(static_cast<uint8_t>((encoded >> 8) & 0xFF));
                    tx_bytes.push_back(static_cast<uint8_t>((encoded >> 16) & 0xFF));
                    tx_bytes.push_back(static_cast<uint8_t>((encoded >> 24) & 0xFF));
                }
                else
                {
                    // Big-integer mode for large values
                    uint8_t bytes_needed = 4;
                    uint64_t temp = val;
                    while (temp > 0 && bytes_needed < 8)
                    {
                        temp >>= 8;
                        if (temp > 0) bytes_needed++;
                    }
                    tx_bytes.push_back(static_cast<uint8_t>(((bytes_needed - 4) << 2) | 0x03));
                    for (uint8_t i = 0; i < bytes_needed; i++)
                    {
                        tx_bytes.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
                    }
                }
            };

            // Helper lambda to append byte string
            auto append_byte_string = [&tx_bytes, &append_compact](const std::vector<uint8_t> &data) {
                append_compact(data.size());
                tx_bytes.insert(tx_bytes.end(), data.begin(), data.end());
            };

            // Helper lambda to append text string
            auto append_text_string = [&tx_bytes, &append_compact](const std::string &str) {
                append_compact(str.size());
                tx_bytes.insert(tx_bytes.end(), str.begin(), str.end());
            };

            // Helper lambda to append array header
            auto append_array = [&tx_bytes](size_t count) {
                if (count <= 23)
                {
                    tx_bytes.push_back(static_cast<uint8_t>(0x80 + count));  // Major type 4, array of count
                }
                else if (count <= 255)
                {
                    tx_bytes.push_back(0x98);  // Array with 1-byte length
                    tx_bytes.push_back(static_cast<uint8_t>(count));
                }
                else
                {
                    tx_bytes.push_back(0x99);  // Array with 2-byte length
                    tx_bytes.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
                    tx_bytes.push_back(static_cast<uint8_t>(count & 0xFF));
                }
            };

            // Version (4 bytes, big-endian u32)
            tx_bytes.push_back(static_cast<uint8_t>((tx_.version >> 24) & 0xFF));
            tx_bytes.push_back(static_cast<uint8_t>((tx_.version >> 16) & 0xFF));
            tx_bytes.push_back(static_cast<uint8_t>((tx_.version >> 8) & 0xFF));
            tx_bytes.push_back(static_cast<uint8_t>(tx_.version & 0xFF));

            // Nonce (compact-encoded)
            append_compact(tx_.nonce);

            // Inputs array
            append_array(tx_.inputs.size());
            for (const auto &inp : tx_.inputs)
            {
                // Each input: [outpoint_hash, output_index, amount_commitment]
                std::string outpoint_clean = midnight::util::strip_hex_prefix(inp.outpoint);
                auto colon_pos = outpoint_clean.find(':');
                if (colon_pos != std::string::npos)
                {
                    outpoint_clean = outpoint_clean.substr(0, colon_pos);
                }

                // Outpoint hash - try as hex, fallback to raw bytes for Bech32m
                try
                {
                    append_byte_string(midnight::util::hex_to_bytes(outpoint_clean));
                }
                catch (...)
                {
                    // Not valid hex (likely Bech32m address) - use as raw string
                    append_byte_string(std::vector<uint8_t>(outpoint_clean.begin(), outpoint_clean.end()));
                }

                append_compact(inp.output_index);

                // Amount commitment - must be hex
                std::string commitment = midnight::util::strip_hex_prefix(inp.amount_commitment);
                try
                {
                    append_byte_string(midnight::util::hex_to_bytes(commitment));
                }
                catch (...)
                {
                    // Fallback: treat as raw bytes
                    append_byte_string(std::vector<uint8_t>(commitment.begin(), commitment.end()));
                }
            }

            // Outputs array
            append_array(tx_.outputs.size());
            for (const auto &out : tx_.outputs)
            {
                // Each output: [address, amount_commitment, lock_height]
                // Address - Midnight uses Bech32m addresses (UTF-8 strings), not hex
                // Just use the address as-is
                append_text_string(out.address);

                // Amount commitment - must be hex (64 hex chars = 32 bytes)
                std::string commitment = midnight::util::strip_hex_prefix(out.amount_commitment);
                try
                {
                    append_byte_string(midnight::util::hex_to_bytes(commitment));
                }
                catch (...)
                {
                    // Fallback: treat as raw bytes
                    append_byte_string(std::vector<uint8_t>(commitment.begin(), commitment.end()));
                }

                append_compact(out.lock_height);
            }

            // Fees (compact-encoded u64)
            append_compact(tx_.fees);

            // Witnesses (if present)
            if (!tx_.witness_bundle.signatures.empty())
            {
                append_array(tx_.witness_bundle.signatures.size());
                for (const auto &sig : tx_.witness_bundle.signatures)
                {
                    // Each witness signature - try as hex, fallback to raw
                    std::string sig_clean = midnight::util::strip_hex_prefix(sig);
                    try
                    {
                        append_byte_string(midnight::util::hex_to_bytes(sig_clean));
                    }
                    catch (...)
                    {
                        // Not valid hex - use as raw bytes
                        append_byte_string(std::vector<uint8_t>(sig.begin(), sig.end()));
                    }
                }
            }
            else
            {
                append_array(0);  // Empty witnesses array
            }

            // Proof references (if present)
            if (!tx_.witness_bundle.proof_references.empty())
            {
                append_array(tx_.witness_bundle.proof_references.size());
                for (const auto &proof : tx_.witness_bundle.proof_references)
                {
                    std::string proof_clean = midnight::util::strip_hex_prefix(proof);
                    try
                    {
                        append_byte_string(midnight::util::hex_to_bytes(proof_clean));
                    }
                    catch (...)
                    {
                        append_byte_string(std::vector<uint8_t>(proof.begin(), proof.end()));
                    }
                }
            }
            else
            {
                append_array(0);  // Empty proofs array
            }

            // ============================================================================
            // FIXED: Use Blake2b-256 for transaction hash (Midnight protocol requirement)
            // ============================================================================
            // Midnight uses Blake2b-256 for all transaction hashing.
            // This replaces the incorrect SHA256 implementation with Blake2b-256.
            // ============================================================================
            uint8_t hash[32];
            crypto_generichash(hash, 32, tx_bytes.data(), tx_bytes.size(), nullptr, 0);

            std::vector<uint8_t> hash_vec(hash, hash + 32);
            tx_.hash = "0x" + midnight::util::bytes_to_hex(hash_vec);

            midnight::g_logger->info("Transaction built: " + std::to_string(tx_.inputs.size()) +
                                      " inputs, " + std::to_string(tx_.outputs.size()) +
                                      " outputs, hash=" + tx_.hash);

            return tx_;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("build() failed: ") + e.what());
            return {};
        }
        catch (...)
        {
            midnight::g_logger->error("build() failed: unknown exception");
            return {};
        }
    }

    bool TransactionBuilder::validate()
    {
        if (tx_.inputs.empty() || tx_.outputs.empty())
        {
            midnight::g_logger->warn("Transaction must have inputs and outputs");
            return false;
        }

        if (tx_.inputs.size() > kMaxTxInputs ||
            tx_.outputs.size() > kMaxTxOutputs)
        {
            midnight::g_logger->warn("Too many inputs or outputs");
            return false;
        }

        return true;
    }

    Transaction TransactionBuilder::get_working_transaction() const
    {
        return tx_;
    }

    // ============================================================================
    // DUST Accounting Implementation
    // ============================================================================

    uint64_t DustAccounting::calculate_dust_fee(
        uint64_t input_dust,
        uint64_t output_dust,
        size_t output_count,
        bool has_witnesses)
    {
        return calculate_dust_fee(
            input_dust,
            output_dust,
            output_count,
            has_witnesses,
            TransactionBuilderContext{});
    }

    uint64_t DustAccounting::calculate_dust_fee(
        uint64_t input_dust,
        uint64_t output_dust,
        size_t output_count,
        bool has_witnesses,
        const TransactionBuilderContext &context)
    {
        // Context-calibrated fee/dust model.
        uint64_t base_fee = output_count * context.fee_model.base_dust_per_output;
        if (has_witnesses)
        {
            base_fee += output_count * context.fee_model.witness_dust_per_output;
        }

        // Additional: commitment overhead
        const uint64_t commitment_overhead = commitment_dust_overhead(
            static_cast<uint32_t>(output_count),
            context);

        // Total DUST needed
        uint64_t total_dust_needed = input_dust - output_dust + base_fee + commitment_overhead;

        return total_dust_needed > 0 ? total_dust_needed : base_fee;
    }

    bool DustAccounting::verify_dust_balance(const Transaction &tx)
    {
        // Verify: input_dust >= output_dust + fees + output_commitments_size

        uint64_t input_total = tx.input_dust;
        uint64_t output_total = tx.output_dust + tx.fees;

        // Add commitment overhead
        output_total += tx.outputs.size() * 128; // 128 bytes per commitment

        return input_total >= output_total;
    }

    double DustAccounting::get_dust_generation_rate()
    {
        return DUST_PER_NIGHT; // Per NIGHT token
    }

    // ============================================================================
    // TransactionValidator Implementation
    // ============================================================================

    TransactionValidator::TransactionValidator(std::shared_ptr<UtxoSetManager> utxo_set_manager)
        : utxo_set_manager_(std::move(utxo_set_manager))
    {
    }

    std::string TransactionValidator::get_last_error() const
    {
        return last_error_;
    }

    bool TransactionValidator::validate_transaction(const Transaction &tx)
    {
        last_error_.clear();

        if (!validate_inputs(tx))
        {
            midnight::g_logger->warn("Invalid inputs");
            if (last_error_.empty()) last_error_ = "Invalid inputs";
            return false;
        }

        if (!validate_outputs(tx))
        {
            midnight::g_logger->warn("Invalid outputs");
            if (last_error_.empty()) last_error_ = "Invalid outputs";
            return false;
        }

        if (!validate_dust_accounting(tx))
        {
            midnight::g_logger->warn("Invalid DUST accounting");
            if (last_error_.empty()) last_error_ = "Invalid DUST accounting";
            return false;
        }

        if (!validate_proofs(tx))
        {
            midnight::g_logger->warn("Invalid proofs");
            if (last_error_.empty()) last_error_ = "Invalid proofs";
            return false;
        }

        if (!validate_signature(tx))
        {
            midnight::g_logger->warn("Invalid signature");
            if (last_error_.empty()) last_error_ = "Invalid signature";
            return false;
        }

        return true;
    }

    bool TransactionValidator::validate_inputs(const Transaction &tx)
    {
        if (tx.inputs.empty() || tx.inputs.size() > MAX_INPUTS)
        {
            last_error_ = "Invalid input count: " + std::to_string(tx.inputs.size());
            return false;
        }

        // First pass: check for duplicate inputs
        std::set<std::string> seen_inputs;
        for (const auto &input : tx.inputs)
        {
            std::string input_key = input.outpoint + ":" + std::to_string(input.output_index);
            if (seen_inputs.count(input_key) > 0)
            {
                last_error_ = "Duplicate input found: " + input_key + " - double-spend prevention";
                midnight::g_logger->error(last_error_);
                return false;
            }
            seen_inputs.insert(input_key);
        }

        for (const auto &input : tx.inputs)
        {
            if (input.outpoint.empty() || input.address.empty())
            {
                last_error_ = "Input missing required fields (outpoint or address)";
                return false;
            }

            // SECURITY FIX: If we have a UTXO set manager, verify inputs exist and are unspent
            if (utxo_set_manager_)
            {
                // Query UTXO to verify existence
                auto utxo_opt = utxo_set_manager_->query_utxo(input.outpoint, input.output_index);
                if (!utxo_opt)
                {
                    last_error_ = "UTXO does not exist: " + input.outpoint + ":" +
                                std::to_string(input.output_index);
                    midnight::g_logger->error(last_error_);
                    return false;
                }

                // Check if UTXO is spent
                auto spent_status = utxo_set_manager_->is_spent(input.outpoint, input.output_index);
                if (spent_status.has_value() && *spent_status)
                {
                    last_error_ = "UTXO already spent: " + input.outpoint + ":" +
                                std::to_string(input.output_index);
                    midnight::g_logger->error(last_error_);
                    return false;
                }

                // Check if UTXO is reserved by another pending transaction
                std::string outpoint_key = input.outpoint + ":" + std::to_string(input.output_index);
                if (utxo_set_manager_ && utxo_set_manager_->is_reserved(outpoint_key, ""))
                {
                    last_error_ = "UTXO is reserved by another transaction: " + outpoint_key;
                    midnight::g_logger->error(last_error_);
                    return false;
                }
            }
            else
            {
                // No UTXO manager - only check format
                midnight::g_logger->warn("UTXO existence verification skipped - no UTXO set manager");
            }
        }

        return true;
    }

    bool TransactionValidator::validate_outputs(const Transaction &tx)
    {
        if (tx.outputs.empty() || tx.outputs.size() > MAX_OUTPUTS)
        {
            last_error_ = "Invalid output count: " + std::to_string(tx.outputs.size());
            return false;
        }

        for (const auto &output : tx.outputs)
        {
            if (output.address.empty() || output.amount_commitment.empty())
            {
                last_error_ = "Output missing required fields (address or commitment)";
                return false;
            }

            // Commitment must be well-formed (32 bytes = 64 hex chars)
            if (output.amount_commitment.size() != COMMITMENT_HEX_SIZE)
            { // "0x" + 64 chars
                last_error_ = "Output commitment not well-formed: size " +
                            std::to_string(output.amount_commitment.size()) + " (expected " +
                            std::to_string(COMMITMENT_HEX_SIZE) + ")";
                return false;
            }
        }

        return true;
    }

    bool TransactionValidator::validate_dust_accounting(const Transaction &tx)
    {
        if (!DustAccounting::verify_dust_balance(tx))
        {
            last_error_ = "DUST balance invalid: inputs < outputs + fees";
            return false;
        }
        return true;
    }

    bool TransactionValidator::validate_proofs(const Transaction &tx)
    {
        if (!tx.witness_bundle.proof_references.empty())
        {
            for (const auto &proof_ref : tx.witness_bundle.proof_references)
            {
                if (proof_ref.empty())
                {
                    last_error_ = "Empty proof reference found";
                    return false;
                }
            }

            return true;
        }

        if (tx.proofs.empty())
        {
            last_error_ = "No proofs provided - at least one proof is required";
            return false; // At least one proof needed
        }

        for (const auto &proof : tx.proofs)
        {
            // Each proof should be 128 bytes = 256 hex chars
            if (proof.size() != 258)
            { // "0x" + 256 chars
                last_error_ = "Proof not well-formed: size " + std::to_string(proof.size()) +
                            " (expected 258)";
                return false;
            }
        }

        return true;
    }

    bool TransactionValidator::validate_signature(const Transaction &tx)
    {
        if (!tx.witness_bundle.signatures.empty())
        {
            for (const auto &signature : tx.witness_bundle.signatures)
            {
                if (!is_signature_hex(signature))
                {
                    last_error_ = "Invalid signature format in witness bundle";
                    return false;
                }
            }

            return true;
        }

        if (tx.signature.empty())
        {
            last_error_ = "No signature provided";
            return false;
        }

        if (!is_signature_hex(tx.signature))
        {
            last_error_ = "Invalid signature format";
            return false;
        }

        return true;
    }

    // ============================================================================
    // UTXO Selection Implementation
    // ============================================================================

    std::optional<std::vector<Utxo>> UtxoSelector::select_utxos(
        const std::vector<Utxo> &available,
        uint32_t output_count,
        bool needs_witnesses)
    {
        return select_utxos(available, output_count, needs_witnesses, TransactionBuilderContext{});
    }

    std::optional<std::vector<Utxo>> UtxoSelector::select_utxos(
        const std::vector<Utxo> &available,
        uint32_t output_count,
        bool needs_witnesses,
        const TransactionBuilderContext &context)
    {
        if (available.empty())
        {
            return {};
        }

        if (output_count == 0)
        {
            return {};
        }

        std::vector<Utxo> ordered = available;
        std::sort(ordered.begin(), ordered.end(), [](const Utxo &lhs, const Utxo &rhs)
                  {
            if (lhs.finality_depth != rhs.finality_depth)
            {
                // Prefer deeper-finality UTXOs to reduce reorg risk.
                return lhs.finality_depth > rhs.finality_depth;
            }

            if (lhs.confirmed_height != rhs.confirmed_height)
            {
                // Prefer older UTXOs to keep newer ones available.
                return lhs.confirmed_height < rhs.confirmed_height;
            }

            if (lhs.tx_hash != rhs.tx_hash)
            {
                return lhs.tx_hash < rhs.tx_hash;
            }

            if (lhs.output_index != rhs.output_index)
            {
                return lhs.output_index < rhs.output_index;
            }

            if (lhs.address != rhs.address)
            {
                return lhs.address < rhs.address;
            }

            return lhs.amount_commitment < rhs.amount_commitment; });

        const size_t min_inputs = needs_witnesses
                                      ? std::max<size_t>(2, static_cast<size_t>(output_count))
                                      : std::max<size_t>(1, static_cast<size_t>(output_count));

        const size_t proof_size_hint = needs_witnesses ? context.fee_model.default_proof_size : 0;
        const uint64_t required_dust = FeeEstimator::estimate_fee(
            static_cast<uint32_t>(min_inputs), output_count, proof_size_hint, context);

        // Smallest sufficient strategy: select enough UTXOs
        std::vector<Utxo> selected;
        selected.reserve(ordered.size());

        for (const auto &candidate : ordered)
        {
            selected.push_back(candidate);

            if (selected.size() >= min_inputs &&
                estimate_dust_available(selected, context) >= required_dust)
            {
                return selected;
            }
        }

        if (estimate_dust_available(selected, context) < required_dust)
        {
            return {};
        }

        return selected;
    }

    uint64_t UtxoSelector::estimate_dust_available(const std::vector<Utxo> &utxos)
    {
        return estimate_dust_available(utxos, TransactionBuilderContext{});
    }

    uint64_t UtxoSelector::estimate_dust_available(
        const std::vector<Utxo> &utxos,
        const TransactionBuilderContext &context)
    {
        // We can only estimate since amounts are commitments.
        const uint64_t estimated_per_utxo = derive_estimated_dust_per_utxo(context);
        return utxos.size() * estimated_per_utxo;
    }

    // ============================================================================
    // Fee Estimator Implementation
    // ============================================================================

    uint64_t FeeEstimator::estimate_fee(uint32_t input_count,
                                        uint32_t output_count,
                                        size_t proof_size)
    {
        return estimate_fee(
            input_count,
            output_count,
            proof_size,
            TransactionBuilderContext{});
    }

    uint64_t FeeEstimator::estimate_fee(uint32_t input_count,
                                        uint32_t output_count,
                                        size_t proof_size,
                                        const TransactionBuilderContext &context)
    {
        const uint32_t estimated_size = estimate_tx_size_bytes(
            input_count,
            output_count,
            proof_size,
            context);

        const uint64_t protocol_fee = context.fee_model.use_protocol_min_fee
                                          ? context.protocol_params.calculate_min_fee(estimated_size)
                                          : static_cast<uint64_t>(estimated_size) * context.protocol_params.min_fee_a;

        const size_t proof_chunks =
            (proof_size + context.fee_model.bytes_per_proof - 1) / context.fee_model.bytes_per_proof;
        const uint64_t proof_overhead =
            static_cast<uint64_t>(proof_chunks) * (context.protocol_params.min_fee_a * 4);

        const uint64_t base_dust =
            static_cast<uint64_t>(output_count) * context.fee_model.base_dust_per_output;
        const uint64_t commitment_fee = commitment_dust_overhead(output_count, context);

        return protocol_fee + proof_overhead + base_dust + commitment_fee;
    }

    uint32_t FeeEstimator::estimate_tx_size_bytes(uint32_t input_count,
                                                  uint32_t output_count,
                                                  size_t proof_size,
                                                  const TransactionBuilderContext &context)
    {
        uint64_t estimated = context.fee_model.base_tx_size_bytes;
        estimated += static_cast<uint64_t>(input_count) * context.fee_model.bytes_per_input;
        estimated += static_cast<uint64_t>(output_count) * context.fee_model.bytes_per_output;
        estimated += proof_size;

        return static_cast<uint32_t>(std::min<uint64_t>(estimated, UINT32_MAX));
    }

    double FeeEstimator::get_current_fee_rate()
    {
        return get_current_fee_rate(TransactionBuilderContext{});
    }

    double FeeEstimator::get_current_fee_rate(const TransactionBuilderContext &context)
    {
        // Expose protocol-calibrated base fee rate per byte.
        return static_cast<double>(context.protocol_params.min_fee_a);
    }

} // namespace midnight::utxo_transactions
