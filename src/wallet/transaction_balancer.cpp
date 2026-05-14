#include "midnight/wallet/transaction_balancer.hpp"
#include "midnight/core/logger.hpp"
#include <sodium.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace midnight::wallet {

// ─── Static Helpers ─────────────────────────────────────────────

static std::string to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data)
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

static std::string sha256_hex(const std::vector<uint8_t>& data) {
    uint8_t hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, data.data(), data.size());
    return to_hex(std::vector<uint8_t>(hash, hash + 32));
}

static uint64_t safe_add(uint64_t a, uint64_t b) {
    uint64_t result = a + b;
    if (result < a) {
        throw std::overflow_error("Fee calculation overflow");
    }
    return result;
}

// ─── TransactionBalancer Implementation ─────────────────────────

TransactionBalancer::TransactionBalancer(
    const std::vector<UtxoWithMeta>& available_utxos,
    uint64_t target_amount,
    const std::string& token_type)
    : available_(available_utxos)
    , target_(target_amount)
    , token_type_(token_type)
    , rng_(std::random_device{}())
{
}

std::vector<UtxoWithMeta> TransactionBalancer::filter_by_token_type() const {
    std::vector<UtxoWithMeta> filtered;
    for (const auto& utxo : available_) {
        if (utxo.token_type == token_type_) {
            filtered.push_back(utxo);
        }
    }
    return filtered;
}

void TransactionBalancer::shuffle_utxos(std::vector<UtxoWithMeta>& utxos) const {
    std::shuffle(utxos.begin(), utxos.end(), rng_);
}

UtxoSelectionResult TransactionBalancer::random_improve_select(
    uint64_t target,
    uint64_t fee_per_input,
    uint64_t fee_per_output)
{
    UtxoSelectionResult result;

    // Filter UTXOs by token type
    auto candidates = filter_by_token_type();
    if (candidates.empty()) {
        return result;
    }

    // Calculate estimated fee to add to target
    size_t estimated_inputs = 0;
    uint64_t running_total = 0;

    // Phase 1: Random shuffle and accumulate
    shuffle_utxos(candidates);

    std::vector<UtxoWithMeta> selected;
    for (const auto& utxo : candidates) {
        selected.push_back(utxo);
        running_total += utxo.amount;
        estimated_inputs++;

        // Calculate current fee estimate
        uint64_t current_fee = estimated_inputs * fee_per_input + fee_per_output;
        uint64_t needed = target + current_fee;

        if (running_total >= needed) {
            result.selected = selected;
            result.total_amount = running_total;
            result.excess = running_total - target;
            return result;
        }
    }

    // Phase 2: Improvement pass - try to reduce waste
    // If waste is too high, try replacing largest selected UTXO with a smaller one
    uint64_t waste = running_total > target ? running_total - target : 0;

    if (waste > 0 && selected.size() > 1) {
        // Find largest selected UTXO
        size_t largest_idx = 0;
        for (size_t i = 1; i < selected.size(); ++i) {
            if (selected[i].amount > selected[largest_idx].amount) {
                largest_idx = i;
            }
        }

        // Try each unselected UTXO as replacement
        for (size_t candidate_idx = 0; candidate_idx < candidates.size(); ++candidate_idx) {
            // Skip if already selected
            bool already_selected = false;
            for (const auto& sel : selected) {
                if (sel.utxo_hash == candidates[candidate_idx].utxo_hash) {
                    already_selected = true;
                    break;
                }
            }
            if (already_selected) continue;

            uint64_t new_total = running_total - selected[largest_idx].amount + candidates[candidate_idx].amount;
            uint64_t new_waste = new_total > target ? new_total - target : 0;

            if (new_waste < waste) {
                // Found a better combination
                selected[largest_idx] = candidates[candidate_idx];
                running_total = new_total;
                waste = new_waste;
                break;
            }
        }
    }

    // Check if we have enough
    uint64_t final_fee = selected.size() * fee_per_input + fee_per_output;
    if (running_total >= target + final_fee) {
        result.selected = selected;
        result.total_amount = running_total;
        result.excess = running_total - target;
    }

    return result;
}

UtxoSelectionResult TransactionBalancer::select_utxos(uint64_t amount) {
    return random_improve_select(
        amount,
        fee_constants::PER_INPUT_SIZE * fee_constants::FEE_PER_BYTE,
        fee_constants::PER_OUTPUT_SIZE * fee_constants::FEE_PER_BYTE);
}

// ─── Fee Calculation ───────────────────────────────────────────

uint64_t TransactionBalancer::calculate_fee(
    size_t input_count,
    size_t output_count,
    bool needs_proof)
{
    return FeeCalculator::calculate_tx_fee(input_count, output_count, needs_proof);
}

uint64_t TransactionBalancer::estimate_fee_from_tx(const json& tx) {
    size_t inputs = count_inputs(tx);
    size_t outputs = count_outputs(tx);
    bool needs_proof = tx.value("needs_proof", false);
    return calculate_fee(inputs, outputs, needs_proof);
}

// ─── Change Management ─────────────────────────────────────────

std::optional<UtxoOutput> TransactionBalancer::create_change(
    uint64_t change_amount,
    uint64_t min_dust,
    const std::string& receiver,
    const std::string& token_type)
{
    if (change_amount >= min_dust) {
        UtxoOutput output;
        output.receiver = receiver;
        output.amount = change_amount;
        output.token_type = token_type;
        output.is_change = true;
        return output;
    }
    return std::nullopt;
}

std::vector<UtxoOutput> TransactionBalancer::split_change(
    uint64_t change_amount,
    uint64_t min_dust,
    const std::string& receiver,
    const std::string& token_type)
{
    std::vector<UtxoOutput> outputs;

    if (change_amount < min_dust) {
        return outputs; // Dust, can't create output
    }

    // If change is just above dust threshold, use it directly
    if (change_amount < 2 * min_dust) {
        outputs.push_back({receiver, change_amount, token_type, true});
        return outputs;
    }

    // Split change: create multiple outputs above dust threshold
    uint64_t remaining = change_amount;
    while (remaining >= min_dust) {
        uint64_t split_amount = remaining;

        // If remaining is much larger than min_dust, split in half
        if (remaining > 2 * min_dust) {
            split_amount = remaining / 2;
            // Ensure we don't create dust
            if (split_amount < min_dust) {
                split_amount = remaining - min_dust;
            }
        }

        if (split_amount >= min_dust) {
            outputs.push_back({receiver, split_amount, token_type, true});
            remaining -= split_amount;
        } else {
            break;
        }
    }

    return outputs;
}

// ─── Transaction Helpers ───────────────────────────────────────

size_t TransactionBalancer::count_inputs(const json& tx) {
    if (!tx.contains("inputs")) return 0;

    const auto& inputs = tx["inputs"];
    if (inputs.is_array()) {
        return inputs.size();
    } else if (inputs.is_object()) {
        // Object format: sum all token amounts
        size_t count = 0;
        for (auto& [key, val] : inputs.items()) {
            if (val.is_number()) {
                ++count; // Each entry is an input
            }
        }
        return count > 0 ? count : 1;
    }
    return 0;
}

size_t TransactionBalancer::count_outputs(const json& tx) {
    if (!tx.contains("outputs")) return 0;

    const auto& outputs = tx["outputs"];
    if (outputs.is_array()) {
        return outputs.size();
    } else if (outputs.is_object()) {
        size_t count = 0;
        for (auto& [key, val] : outputs.items()) {
            if (val.is_number()) {
                ++count;
            }
        }
        return count > 0 ? count : 1;
    }
    return 0;
}

uint64_t TransactionBalancer::extract_total_from_inputs(const json& tx) {
    uint64_t total = 0;
    if (!tx.contains("inputs")) return total;

    const auto& inputs = tx["inputs"];
    if (inputs.is_array()) {
        for (const auto& inp : inputs) {
            total += inp.value("amount", uint64_t(0));
        }
    } else if (inputs.is_object()) {
        for (auto& [key, val] : inputs.items()) {
            if (val.is_number()) {
                total += val.get<uint64_t>();
            }
        }
    }
    return total;
}

uint64_t TransactionBalancer::extract_total_from_outputs(const json& tx) {
    uint64_t total = 0;
    if (!tx.contains("outputs")) return total;

    const auto& outputs = tx["outputs"];
    if (outputs.is_array()) {
        for (const auto& out : outputs) {
            total += out.value("amount", uint64_t(0));
        }
    } else if (outputs.is_object()) {
        for (auto& [key, val] : outputs.items()) {
            if (val.is_number()) {
                total += val.get<uint64_t>();
            }
        }
    }
    return total;
}

std::string TransactionBalancer::hash_transaction(const json& tx) {
    std::string tx_str = tx.dump();
    std::vector<uint8_t> tx_bytes(tx_str.begin(), tx_str.end());
    return sha256_hex(tx_bytes);
}

// ─── Transaction Creation ──────────────────────────────────────

json TransactionBalancer::create_balancing_transaction(
    const json& original_tx,
    const std::string& sender,
    uint64_t fee,
    const std::string& balancing_type,
    std::chrono::system_clock::time_point ttl)
{
    json bal_tx;
    bal_tx["type"] = "balancing_transaction";
    bal_tx["original_tx_hash"] = original_tx.value("tx_hash", "");
    bal_tx["sender"] = sender;
    bal_tx["fee"] = fee;
    bal_tx["balancing_type"] = balancing_type;

    // TTL
    auto ttl_sec = std::chrono::duration_cast<std::chrono::seconds>(
                       ttl.time_since_epoch()).count();
    bal_tx["ttl"] = ttl_sec;

    // Add inputs if provided
    if (original_tx.contains("inputs")) {
        bal_tx["inputs"] = original_tx["inputs"];
    }

    // Add outputs if provided
    if (original_tx.contains("outputs")) {
        bal_tx["outputs"] = original_tx["outputs"];
    }

    // Hash for identification
    bal_tx["tx_hash"] = hash_transaction(bal_tx);

    return bal_tx;
}

// ─── Transaction Merging ───────────────────────────────────────

json TransactionBalancer::merge_into(json target, const json& source) {
    if (source.is_null()) return target;
    if (target.is_null()) return source;

    // Merge inputs
    if (target.contains("inputs") && source.contains("inputs")) {
        const auto& src_inputs = source["inputs"];
        if (src_inputs.is_array()) {
            for (const auto& inp : src_inputs) {
                target["inputs"].push_back(inp);
            }
        }
    } else if (source.contains("inputs")) {
        target["inputs"] = source["inputs"];
    }

    // Merge outputs
    if (target.contains("outputs") && source.contains("outputs")) {
        const auto& src_outputs = source["outputs"];
        if (src_outputs.is_array()) {
            for (const auto& out : src_outputs) {
                target["outputs"].push_back(out);
            }
        }
    } else if (source.contains("outputs")) {
        target["outputs"] = source["outputs"];
    }

    // Sum fees
    uint64_t fee_target = target.value("fee", uint64_t(0));
    uint64_t fee_source = source.value("fee", uint64_t(0));
    target["fee"] = safe_add(fee_target, fee_source);

    // Track merged transactions
    if (source.contains("tx_hash")) {
        std::vector<std::string> merged;
        if (target.contains("merged_with")) {
            if (target["merged_with"].is_array()) {
                merged = target["merged_with"].get<std::vector<std::string>>();
            } else {
                merged.push_back(target["merged_with"].get<std::string>());
            }
        }
        merged.push_back(source["tx_hash"].get<std::string>());
        target["merged_with"] = merged;
    }

    // Update hash
    target["tx_hash"] = hash_transaction(target);

    return target;
}

json TransactionBalancer::merge_transactions(const std::vector<json>& txs) {
    json result;
    for (const auto& tx : txs) {
        result = merge_into(result, tx);
    }
    return result;
}

// ─── Balance Finalized Transaction ──────────────────────────────

SimpleBalancingRecipe TransactionBalancer::balance_finalized(
    const json& finalized_tx,
    TokenKindsToBalance kinds,
    std::chrono::system_clock::time_point ttl)
{
    (void)kinds;
    SimpleBalancingRecipe recipe;
    recipe.type = BalancingType::Finalized;

    // For finalized TXs, we need to create a balancing transaction
    // because the original TX is already on-chain

    // Step 1: Calculate amounts needed
    uint64_t input_total = extract_total_from_inputs(finalized_tx);
    uint64_t output_total = extract_total_from_outputs(finalized_tx);
    uint64_t shortfall = input_total > output_total ? input_total - output_total : 0;

    // Step 2: Select UTXOs to cover the shortfall
    uint64_t fee_estimate = estimate_fee_from_tx(finalized_tx);
    uint64_t total_needed = shortfall + fee_estimate;

    auto selection = select_utxos(total_needed);
    if (selection.selected.empty() && shortfall > 0) {
        recipe.error = "Insufficient UTXOs to balance finalized transaction";
        return recipe;
    }

    // Step 3: Calculate actual fee
    uint64_t actual_fee = calculate_fee(selection.selected.size(), 1, false);
    uint64_t change_amount = selection.total_amount - shortfall - actual_fee;

    // Step 4: Create balancing transaction
    json bal_tx;
    bal_tx["type"] = "balancing_transaction";
    bal_tx["original_tx_hash"] = finalized_tx.value("tx_hash", "");
    bal_tx["balancing_type"] = "finalized";
    bal_tx["fee"] = actual_fee;

    // Add selected inputs
    json inputs_arr = json::array();
    for (const auto& utxo : selection.selected) {
        inputs_arr.push_back({
            {"utxo_hash", utxo.utxo_hash},
            {"amount", utxo.amount},
            {"token_type", utxo.token_type}
        });
    }
    bal_tx["inputs"] = inputs_arr;

    // Add output to original sender (or fee receiver)
    json outputs_arr = json::array();
    std::string sender = finalized_tx.value("sender", "");
    if (!sender.empty()) {
        outputs_arr.push_back({
            {"receiver", sender},
            {"amount", selection.total_amount - actual_fee},
            {"token_type", token_type_}
        });
    }
    bal_tx["outputs"] = outputs_arr;

    // TTL
    auto ttl_sec = std::chrono::duration_cast<std::chrono::seconds>(
                       ttl.time_since_epoch()).count();
    bal_tx["ttl"] = ttl_sec;

    // Hash
    bal_tx["tx_hash"] = hash_transaction(bal_tx);

    // Step 5: Add change output if needed
    if (change_amount >= fee_constants::MIN_DUST_OUTPUT && !sender.empty()) {
        json change_out;
        change_out["receiver"] = sender;
        change_out["amount"] = change_amount;
        change_out["token_type"] = token_type_;
        change_out["is_change"] = true;
        bal_tx["outputs"].push_back(change_out);
    }

    // Populate recipe
    recipe.transactions.push_back(finalized_tx);  // Original TX
    recipe.transactions.push_back(bal_tx);        // Balancing TX
    recipe.needs_signature = true;
    recipe.needs_proof = false;  // Balancing TX is unshielded
    recipe.total_fee = actual_fee;
    recipe.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("Balanced finalized TX: shortfall=" +
                                  std::to_string(shortfall) +
                                  ", fee=" + std::to_string(actual_fee));
    }

    return recipe;
}

// ─── Balance Unbound Transaction ───────────────────────────────

SimpleBalancingRecipe TransactionBalancer::balance_unbound(
    const json& unbound_tx,
    TokenKindsToBalance kinds,
    std::chrono::system_clock::time_point ttl)
{
    SimpleBalancingRecipe recipe;
    recipe.type = BalancingType::Unbound;

    // For unbound TXs, unshielded balancing happens IN-PLACE
    // We modify the base transaction directly

    // Step 1: Calculate amounts
    uint64_t input_total = extract_total_from_inputs(unbound_tx);
    uint64_t output_total = extract_total_from_outputs(unbound_tx);
    uint64_t shortfall = input_total > output_total ? input_total - output_total : 0;

    // Step 2: Select UTXOs if unshielded balancing is needed
    json modified_tx = unbound_tx;
    uint64_t actual_fee = 0;

    if (kinds.should_balance_unshielded && shortfall > 0) {
        uint64_t fee_estimate = estimate_fee_from_tx(unbound_tx);
        auto selection = select_utxos(shortfall + fee_estimate);

        if (selection.selected.empty()) {
            recipe.error = "Insufficient UTXOs for unshielded balancing";
            return recipe;
        }

        actual_fee = calculate_fee(selection.selected.size(), 1, false);

        // Add inputs to the base transaction IN-PLACE
        json inputs_arr = json::array();
        if (modified_tx.contains("inputs") && modified_tx["inputs"].is_array()) {
            for (const auto& inp : modified_tx["inputs"]) {
                inputs_arr.push_back(inp);
            }
        }
        for (const auto& utxo : selection.selected) {
            inputs_arr.push_back({
                {"utxo_hash", utxo.utxo_hash},
                {"amount", utxo.amount},
                {"token_type", utxo.token_type}
            });
        }
        modified_tx["inputs"] = inputs_arr;

        // Mark as balanced
        modified_tx["balanced_unshielded"] = true;
        modified_tx["balancing_fee_unshielded"] = actual_fee;
    }

    // Step 3: Create shielded balancing TX if needed
    json shielded_bal_tx;
    if (kinds.should_balance_shielded) {
        uint64_t shielded_fee = calculate_fee(1, 1, true);  // Needs proof
        shielded_bal_tx = create_balancing_transaction(
            unbound_tx,
            unbound_tx.value("sender", ""),
            shielded_fee,
            "shielded",
            ttl);
        recipe.needs_proof = true;
    }

    // Step 4: Create dust/fee balancing TX if needed
    json fee_bal_tx;
    if (kinds.should_balance_dust) {
        uint64_t dust_fee = calculate_fee(1, 1, false);
        fee_bal_tx = create_balancing_transaction(
            unbound_tx,
            unbound_tx.value("dust_receiver", unbound_tx.value("sender", "")),
            dust_fee,
            "dust_fee",
            ttl);
    }

    // Populate recipe
    recipe.transactions.push_back(modified_tx);  // Base TX (modified in-place)

    if (!shielded_bal_tx.is_null()) {
        recipe.transactions.push_back(shielded_bal_tx);
    }
    if (!fee_bal_tx.is_null()) {
        recipe.transactions.push_back(fee_bal_tx);
    }

    recipe.total_fee = actual_fee;
    recipe.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("Balanced unbound TX: " +
                                  unbound_tx.value("tx_hash", "?").substr(0, 16) + "...");
    }

    return recipe;
}

// ─── Balance Unproven Transaction ─────────────────────────────

SimpleBalancingRecipe TransactionBalancer::balance_unproven(
    const json& unproven_tx,
    TokenKindsToBalance kinds,
    std::chrono::system_clock::time_point ttl)
{
    (void)ttl;
    SimpleBalancingRecipe recipe;
    recipe.type = BalancingType::Unproven;

    // For unproven TXs, merge all balancing into single transaction

    // Step 1: Start with base TX
    json merged_tx = unproven_tx;
    uint64_t total_fee = 0;

    // Step 2: Add unshielded balancing if needed
    if (kinds.should_balance_unshielded) {
        uint64_t input_total = extract_total_from_inputs(unproven_tx);
        uint64_t output_total = extract_total_from_outputs(unproven_tx);
        uint64_t shortfall = input_total > output_total ? input_total - output_total : 0;

        if (shortfall > 0) {
            uint64_t fee_estimate = estimate_fee_from_tx(unproven_tx);
            auto selection = select_utxos(shortfall + fee_estimate);

            if (selection.selected.empty()) {
                recipe.error = "Insufficient UTXOs for unshielded balancing";
                return recipe;
            }

            uint64_t unshielded_fee = calculate_fee(selection.selected.size(), 1, false);

            // Add inputs to merged TX
            json inputs_arr = json::array();
            if (merged_tx.contains("inputs") && merged_tx["inputs"].is_array()) {
                for (const auto& inp : merged_tx["inputs"]) {
                    inputs_arr.push_back(inp);
                }
            }
            for (const auto& utxo : selection.selected) {
                inputs_arr.push_back({
                    {"utxo_hash", utxo.utxo_hash},
                    {"amount", utxo.amount},
                    {"token_type", utxo.token_type}
                });
            }
            merged_tx["inputs"] = inputs_arr;

            merged_tx["balanced_unshielded"] = true;
            merged_tx["balancing_fee_unshielded"] = unshielded_fee;
            total_fee = safe_add(total_fee, unshielded_fee);
        }
    }

    // Step 3: Add shielded balancing if needed
    if (kinds.should_balance_shielded) {
        uint64_t shielded_fee = calculate_fee(1, 1, true);
        total_fee = safe_add(total_fee, shielded_fee);
        merged_tx["needs_shielded_proof"] = true;
        merged_tx["shielded_fee"] = shielded_fee;
        recipe.needs_proof = true;
    }

    // Step 4: Add dust/fee balancing if needed
    if (kinds.should_balance_dust) {
        uint64_t dust_fee = calculate_fee(1, 1, false);
        total_fee = safe_add(total_fee, dust_fee);
        merged_tx["needs_dust_fee"] = true;
        merged_tx["dust_fee"] = dust_fee;
    }

    // Update total fee
    merged_tx["total_fee"] = total_fee;

    // Update hash
    merged_tx["tx_hash"] = hash_transaction(merged_tx);

    // Populate recipe - single merged transaction
    recipe.transactions.push_back(merged_tx);
    recipe.total_fee = total_fee;
    recipe.success = true;

    if (midnight::g_logger) {
        midnight::g_logger->info("Balanced unproven TX: total_fee=" +
                                  std::to_string(total_fee));
    }

    return recipe;
}

} // namespace midnight::wallet
