/**
 * Phase 3: UTXO Transactions Implementation
 */

#include "midnight/utxo-transactions/utxo_transactions.hpp"
#include "midnight/network/network_client.hpp"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace midnight::phase3
{

    // ============================================================================
    // UtxoSetManager Implementation
    // ============================================================================

    UtxoSetManager::UtxoSetManager(const std::string &indexer_graphql_url,
                                   const std::string &node_rpc_url,
                                   DataSourceMode mode)
        : indexer_url_(indexer_graphql_url), rpc_url_(node_rpc_url), data_source_mode_(mode)
    {
    }

    void UtxoSetManager::set_data_source_mode(DataSourceMode mode)
    {
        data_source_mode_ = mode;
    }

    DataSourceMode UtxoSetManager::get_data_source_mode() const
    {
        return data_source_mode_;
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

            auto result = graphql_query(query);

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
            std::cerr << "Error querying UTXOs: " << e.what() << std::endl;
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

            auto result = graphql_query(query);

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
            std::cerr << "Error querying UTXO: " << e.what() << std::endl;
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

    Json::Value UtxoSetManager::graphql_query(const std::string &query)
    {
        if (data_source_mode_ == DataSourceMode::MOCK)
        {
            Json::Value response;
            response["data"]["utxoSet"] = Json::Value(Json::arrayValue);
            return response;
        }

        midnight::network::NetworkClient client(indexer_url_, 8000);
        nlohmann::json payload = {
            {"query", query}
        };

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

    // ============================================================================
    // TransactionBuilder Implementation
    // ============================================================================

    TransactionBuilder::TransactionBuilder()
    {
        tx_.version = 1;
        tx_.nonce = static_cast<uint64_t>(std::time(nullptr));
        tx_.timestamp = static_cast<uint64_t>(std::time(nullptr));
    }

    void TransactionBuilder::add_input(const UtxoInput &input)
    {
        tx_.inputs.push_back(input);
    }

    void TransactionBuilder::add_output(const UtxoOutput &output)
    {
        tx_.outputs.push_back(output);
    }

    void TransactionBuilder::set_fees(uint64_t fee_amount)
    {
        tx_.fees = fee_amount;
    }

    std::optional<Transaction> TransactionBuilder::build()
    {
        if (!validate())
        {
            return {};
        }

        tx_.hash = compute_hash();
        return tx_;
    }

    bool TransactionBuilder::validate()
    {
        if (tx_.inputs.empty() || tx_.outputs.empty())
        {
            std::cerr << "Transaction must have inputs and outputs" << std::endl;
            return false;
        }

        if (tx_.inputs.size() > TransactionValidator::MAX_INPUTS ||
            tx_.outputs.size() > TransactionValidator::MAX_OUTPUTS)
        {
            std::cerr << "Too many inputs or outputs" << std::endl;
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
        // In production: Compute blake2_256 hash
        // For now: Return mock hash
        return "0x" + std::string(64, 'b');
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

        // Basic fee: 1000 per output + 500 per witness
        uint64_t base_fee = output_count * 1000;
        if (has_witnesses)
        {
            base_fee += output_count * 500;
        }

        // Additional: commitment overhead
        uint64_t commitment_overhead = output_count * 128; // 128 bytes per commitment

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
            std::cerr << "Invalid inputs" << std::endl;
            return false;
        }

        if (!validate_outputs(tx))
        {
            std::cerr << "Invalid outputs" << std::endl;
            return false;
        }

        if (!validate_dust_accounting(tx))
        {
            std::cerr << "Invalid DUST accounting" << std::endl;
            return false;
        }

        if (!validate_proofs(tx))
        {
            std::cerr << "Invalid proofs" << std::endl;
            return false;
        }

        if (!validate_signature(tx))
        {
            std::cerr << "Invalid signature" << std::endl;
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
        if (tx.signature.empty())
        {
            return false;
        }

        // sr25519 signature should be 64 bytes = 128 hex chars
        // In practice: verify actual signature
        return tx.signature.size() == 130; // "0x" + 128 chars
    }

    // ============================================================================
    // UTXO Selection Implementation
    // ============================================================================

    std::optional<std::vector<Utxo>> UtxoSelector::select_utxos(
        const std::vector<Utxo> &available,
        uint32_t output_count,
        bool needs_witnesses)
    {

        if (available.empty())
        {
            return {};
        }

        // Smallest sufficient strategy: select enough UTXOs
        // For now: select all

        std::vector<Utxo> selected = available;

        // Could implement coin selection algorithm here
        // For now: return first N suitable UTXOs

        return selected;
    }

    uint64_t UtxoSelector::estimate_dust_available(const std::vector<Utxo> &utxos)
    {
        // We can only estimate since amounts are commitments
        // Assume average UTXO size
        uint64_t estimated_per_utxo = 1000; // Mock value
        return utxos.size() * estimated_per_utxo;
    }

    // ============================================================================
    // Fee Estimator Implementation
    // ============================================================================

    uint64_t FeeEstimator::estimate_fee(uint32_t input_count,
                                        uint32_t output_count,
                                        size_t proof_size)
    {
        // Fee calculation:
        // Base: 1000 per output
        // Proof overhead: 100 per proof
        // Commitment overhead: 128 per output

        uint64_t base_fee = output_count * 1000;
        uint64_t proof_fee = (proof_size / 128) * 100;
        uint64_t commitment_fee = output_count * 128;

        return base_fee + proof_fee + commitment_fee;
    }

    double FeeEstimator::get_current_fee_rate()
    {
        // DUST per byte
        return 0.1; // Mock value
    }

} // namespace midnight::phase3
