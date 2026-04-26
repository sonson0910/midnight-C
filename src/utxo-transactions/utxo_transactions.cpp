/**
 * Phase 3: UTXO Transactions Implementation
 */

#include "midnight/utxo-transactions/utxo_transactions.hpp"
#include "midnight/network/network_client.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace midnight::utxo_transactions
{
    namespace
    {
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
            std::string query = R"(
            query {
                utxoSet(address: ")" +
                                address + R"(") {
                    txHash
                    outputIndex
                    recipient
                    amountCommitment
                    confirmedHeight
                    finalityDepth
                }
            }
        )";

            if (!indexer_provider_)
            {
                throw std::runtime_error("Indexer provider is not configured");
            }

            auto result = indexer_provider_->execute_query(query);

            std::vector<Utxo> utxos;
            if (result.isMember("data") && result["data"].isMember("utxoSet"))
            {
                auto utxo_array = result["data"]["utxoSet"];
                for (const auto &u : utxo_array)
                {
                    Utxo utxo;
                    utxo.tx_hash = u.get("txHash", "").asString();
                    utxo.output_index = u.get("outputIndex", 0).asUInt();
                    utxo.address = u.get("recipient", "").asString();
                    utxo.amount_commitment = u.get("amountCommitment", "").asString();
                    utxo.confirmed_height = u.get("confirmedHeight", 0).asUInt();
                    utxo.finality_depth = u.get("finalityDepth", 0).asUInt();
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
            std::string query = R"(
            query {
                utxo(txHash: ")" +
                                tx_hash + R"(", outputIndex: )" +
                                std::to_string(output_index) + R"() {
                    txHash
                    outputIndex
                    recipient
                    amountCommitment
                    confirmedHeight
                    finalityDepth
                    isSpent
                }
            }
        )";

            if (!indexer_provider_)
            {
                throw std::runtime_error("Indexer provider is not configured");
            }

            auto result = indexer_provider_->execute_query(query);

            if (result.isMember("data") && result["data"].isMember("utxo"))
            {
                auto u = result["data"]["utxo"];
                Utxo utxo;
                utxo.tx_hash = u.get("txHash", "").asString();
                utxo.output_index = u.get("outputIndex", 0).asUInt();
                utxo.address = u.get("recipient", "").asString();
                utxo.amount_commitment = u.get("amountCommitment", "").asString();
                utxo.confirmed_height = u.get("confirmedHeight", 0).asUInt();
                utxo.finality_depth = u.get("finalityDepth", 0).asUInt();
                utxo.is_spent = u.get("isSpent", false).asBool();

                return utxo;
            }

            return {};
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error(std::string("Error querying UTXO: ") + e.what());
            return {};
        }
    }

    bool UtxoSetManager::is_spent(const std::string &tx_hash, uint32_t output_index)
    {
        auto utxo = query_utxo(tx_hash, output_index);
        if (!utxo)
        {
            return true; // Not found = treated as spent
        }
        return utxo->is_spent;
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
        tx_.inputs.push_back(input);
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

        tx_.hash = compute_hash();
        return tx_;
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

    std::string TransactionBuilder::compute_hash()
    {
        // Compute a deterministic hash over the UTXO transaction body.
        // In production this should use blake2b-256; we use a cascaded
        // FNV-1a-64 producing 32 bytes until libblake2 is integrated.
        // Hash input: all inputs (outpoint + commitment), all outputs
        // (address + commitment), nonce, and version.

        constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
        constexpr uint64_t kPrime = 1099511628211ULL;

        auto fnv_hash = [&](uint64_t state, const std::string &data) -> uint64_t
        {
            for (unsigned char c : data)
            {
                state ^= c;
                state *= kPrime;
            }
            return state;
        };

        // Build 4 independent 64-bit hashes → 32-byte digest
        std::array<uint8_t, 32> digest{};
        for (size_t block = 0; block < 4; ++block)
        {
            uint64_t state = kOffsetBasis ^ (0x9E3779B97F4A7C15ULL * (block + 1));

            // Mix version and nonce
            state = fnv_hash(state, std::to_string(tx_.version));
            state = fnv_hash(state, std::to_string(tx_.nonce));

            // Mix inputs
            for (const auto &inp : tx_.inputs)
            {
                state = fnv_hash(state, inp.outpoint);
                state = fnv_hash(state, inp.amount_commitment);
            }

            // Mix outputs
            for (const auto &out : tx_.outputs)
            {
                state = fnv_hash(state, out.address);
                state = fnv_hash(state, out.amount_commitment);
            }

            for (size_t i = 0; i < 8; ++i)
            {
                digest[(block * 8) + i] = static_cast<uint8_t>((state >> (i * 8)) & 0xFF);
            }
        }

        return "0x" + midnight::util::bytes_to_hex(
                          std::vector<uint8_t>(digest.begin(), digest.end()));
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

    bool TransactionValidator::validate_transaction(const Transaction &tx)
    {
        if (!validate_inputs(tx))
        {
            midnight::g_logger->warn("Invalid inputs");
            return false;
        }

        if (!validate_outputs(tx))
        {
            midnight::g_logger->warn("Invalid outputs");
            return false;
        }

        if (!validate_dust_accounting(tx))
        {
            midnight::g_logger->warn("Invalid DUST accounting");
            return false;
        }

        if (!validate_proofs(tx))
        {
            midnight::g_logger->warn("Invalid proofs");
            return false;
        }

        if (!validate_signature(tx))
        {
            midnight::g_logger->warn("Invalid signature");
            return false;
        }

        return true;
    }

    bool TransactionValidator::validate_inputs(const Transaction &tx)
    {
        if (tx.inputs.empty() || tx.inputs.size() > MAX_INPUTS)
        {
            return false;
        }

        for (const auto &input : tx.inputs)
        {
            if (input.outpoint.empty() || input.address.empty())
            {
                return false;
            }

            // In production: Check outpoint exists and is unspent
            // Check signature is valid
            // Check witness is provided if needed
        }

        return true;
    }

    bool TransactionValidator::validate_outputs(const Transaction &tx)
    {
        if (tx.outputs.empty() || tx.outputs.size() > MAX_OUTPUTS)
        {
            return false;
        }

        for (const auto &output : tx.outputs)
        {
            if (output.address.empty() || output.amount_commitment.empty())
            {
                return false;
            }

            // Commitment must be well-formed (32 bytes = 64 hex chars)
            if (output.amount_commitment.size() != 66)
            { // "0x" + 64 chars
                return false;
            }
        }

        return true;
    }

    bool TransactionValidator::validate_dust_accounting(const Transaction &tx)
    {
        return DustAccounting::verify_dust_balance(tx);
    }

    bool TransactionValidator::validate_proofs(const Transaction &tx)
    {
        if (!tx.witness_bundle.proof_references.empty())
        {
            for (const auto &proof_ref : tx.witness_bundle.proof_references)
            {
                if (proof_ref.empty())
                {
                    return false;
                }
            }

            return true;
        }

        if (tx.proofs.empty())
        {
            return false; // At least one proof needed
        }

        for (const auto &proof : tx.proofs)
        {
            // Each proof should be 128 bytes = 256 hex chars
            if (proof.size() != 258)
            { // "0x" + 256 chars
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
                    return false;
                }
            }

            return true;
        }

        if (tx.signature.empty())
        {
            return false;
        }

        return is_signature_hex(tx.signature);
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
