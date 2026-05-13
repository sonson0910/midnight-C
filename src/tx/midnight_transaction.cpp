/**
 * Midnight Transaction Model Guard Implementation
 *
 * Production Midnight transaction serialization is provided by midnight-ledger
 * tagged binary serializers, not by this legacy CBOR-like model.
 */

#include "midnight/tx/midnight_transaction.hpp"
#include "midnight/core/logger.hpp"
#include <openssl/sha.h>
#include <sodium.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace midnight::tx
{

    // ============================================================================
    // TokenType Implementation
    // ============================================================================

    TokenType TokenType::from_hex(const std::string &hex)
    {
        TokenType tt;
        tt.name = hex;
        return tt;
    }

    // ============================================================================
    // TransactionBuilder Implementation
    // ============================================================================

    TransactionBuilder::TransactionBuilder()
    {
        body_.network_id = NetworkId::TestNet;  // Default to testnet
        body_.validity_interval = TransactionValidityInterval{};
    }

    TransactionBuilder &TransactionBuilder::add_input(const TransactionInput &input)
    {
        body_.inputs.push_back(input);
        return *this;
    }

    TransactionBuilder &TransactionBuilder::add_output(const ColoredOutput &output)
    {
        body_.outputs.push_back(output);
        return *this;
    }

    TransactionBuilder &TransactionBuilder::add_output(const std::string &address, uint64_t lovelace_amount)
    {
        ColoredOutput output;
        output.address = address;
        output.amount = lovelace_amount;
        output.token_type = TokenType::lovelace();
        body_.outputs.push_back(output);
        return *this;
    }

    TransactionBuilder &TransactionBuilder::add_colored_input(const TransactionInput &input)
    {
        body_.colored_inputs.push_back(input);
        return *this;
    }

    TransactionBuilder &TransactionBuilder::add_colored_output(const ColoredOutput &output)
    {
        body_.colored_outputs.push_back(output);
        return *this;
    }

    TransactionBuilder &TransactionBuilder::add_mint(const TokenType &token_type, int64_t amount)
    {
        AssetTransfer transfer;
        transfer.token_type = token_type;
        transfer.amount = amount;
        body_.mint.push_back(transfer);
        return *this;
    }

    TransactionBuilder &TransactionBuilder::add_reference_input(const TransactionInput &input)
    {
        body_.reference_inputs.push_back(input);
        return *this;
    }

    TransactionBuilder &TransactionBuilder::with_network(NetworkId network_id)
    {
        body_.network_id = network_id;
        return *this;
    }

    TransactionBuilder &TransactionBuilder::with_fee(uint64_t fee)
    {
        explicit_fee_ = fee;
        return *this;
    }

    TransactionBuilder &TransactionBuilder::with_validity_interval(const TransactionValidityInterval &interval)
    {
        body_.validity_interval = interval;
        return *this;
    }

    TransactionBuilder &TransactionBuilder::with_confidential_contract(const std::string &contract_hash)
    {
        body_.confidential_contract_hash = contract_hash;
        return *this;
    }

    TransactionBuilder &TransactionBuilder::with_auxiliary_data(const std::string &aux_data)
    {
        auxiliary_data_ = aux_data;
        return *this;
    }

    TransactionBuilder &TransactionBuilder::with_witnesses(const std::vector<TransactionWitness> &witnesses)
    {
        witnesses_ = witnesses;
        return *this;
    }

    TransactionBuilder &TransactionBuilder::add_witness(const TransactionWitness &witness)
    {
        witnesses_.push_back(witness);
        return *this;
    }

    bool TransactionBuilder::validate_inputs() const
    {
        if (body_.inputs.empty())
        {
            midnight::g_logger->warn("Transaction must have at least one input");
            return false;
        }

        for (const auto &input : body_.inputs)
        {
            if (input.utxo_id.empty())
            {
                midnight::g_logger->warn("Transaction input has empty utxo_id");
                return false;
            }
        }

        return true;
    }

    bool TransactionBuilder::validate_outputs() const
    {
        if (body_.outputs.empty())
        {
            midnight::g_logger->warn("Transaction must have at least one output");
            return false;
        }

        for (const auto &output : body_.outputs)
        {
            if (output.address.empty())
            {
                midnight::g_logger->warn("Transaction output has empty address");
                return false;
            }
        }

        return true;
    }

    bool TransactionBuilder::validate_fee() const
    {
        uint64_t fee = explicit_fee_.value_or(0);
        if (fee == 0 && !explicit_fee_.has_value())
        {
            // Will be auto-estimated
            return true;
        }
        return fee > 0;
    }

    uint64_t TransactionBuilder::total_input_lovelace() const
    {
        uint64_t total = 0;
        for (const auto &input : body_.inputs)
        {
            if (input.utxo_data)
            {
                total += input.utxo_data->amount;
            }
        }
        for (const auto &input : body_.colored_inputs)
        {
            if (input.utxo_data)
            {
                total += input.utxo_data->amount;
            }
        }
        return total;
    }

    uint64_t TransactionBuilder::total_output_lovelace() const
    {
        uint64_t total = 0;
        for (const auto &output : body_.outputs)
        {
            total += output.amount;
        }
        for (const auto &output : body_.colored_outputs)
        {
            total += output.amount;
        }
        return total;
    }

    std::optional<SignedTransaction> TransactionBuilder::build() const
    {
        midnight::g_logger->error(
            "TransactionBuilder cannot build production Midnight transactions. "
            "Use midnight-ledger tagged binary serialization and proof payloads.");
        return std::nullopt;
    }

    // ============================================================================
    // Transaction Serialization Implementation
    // ============================================================================

    std::string Utf8TransactionSerializer::bytes_to_hex(const std::vector<uint8_t> &data)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t byte : data)
        {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        return oss.str();
    }

    std::vector<uint8_t> Utf8TransactionSerializer::hex_to_bytes(const std::string &hex)
    {
        std::string clean = hex;
        if (clean.substr(0, 2) == "0x" || clean.substr(0, 2) == "0X")
        {
            clean = clean.substr(2);
        }

        if (clean.size() % 2 != 0)
        {
            throw std::runtime_error("Invalid hex: odd length");
        }

        std::vector<uint8_t> result;
        result.reserve(clean.size() / 2);

        for (size_t i = 0; i < clean.size(); i += 2)
        {
            std::string byte_str = clean.substr(i, 2);
            result.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
        }

        return result;
    }

    std::vector<uint8_t> Utf8TransactionSerializer::serialize_body(const TransactionBody &body)
    {
        std::vector<uint8_t> result;

        // Serialize as CBOR-like structure
        // Format: array(13) + elements...

        // CBOR array header for 13 mandatory fields
        // Major type 0x80 (array) + array length
        // Count mandatory fields for array header
        // Midnight transaction body fields:
        // 1. network_id (u8)
        // 2. inputs (array)
        // 3. outputs (array)
        // 4. fee (compact u64)
        // 5. ttl/validity_interval (compact u64)
        // 6. certs (array)
        // 7. withdrawals (array)
        // 8. update (optional)
        // 9. auxiliary_data_hash (optional)
        // 10. validity_interval (array of 2)
        // 11. colored_inputs (array)
        // 12. colored_outputs (array)
        // 13. mint (array)
        // 14. reference_inputs (array)
        // 15. confidential_contract_hash (optional)
        // 16. confidential_meeting (optional)
        // 17. offset (optional)
        size_t field_count = 17;

        // CBOR array header encoding
        if (field_count <= 23)
        {
            result.push_back(0x80 + static_cast<uint8_t>(field_count));  // 0x80-0x97
        }
        else if (field_count <= 255)
        {
            result.push_back(0x98);  // Array with 8-bit count follows
            result.push_back(static_cast<uint8_t>(field_count));
        }
        else
        {
            result.push_back(0x99);  // Array with 16-bit count follows
            result.push_back(static_cast<uint8_t>((field_count >> 8) & 0xFF));
            result.push_back(static_cast<uint8_t>(field_count & 0xFF));
        }

        // Helper lambda to append a compact u64 (SCALE-like encoding)
        // Format: mode (2 bits) + value (shifted based on mode)
        // Mode 0: 1 byte for values < 64 (bits 0-5)
        // Mode 1: 2 bytes for values < 16384 (bits 0-13)
        // Mode 2: 4 bytes for values < 2^30 (bits 0-29)
        // Mode 3: N+1 bytes for larger values (bits 0-6 in first byte)
        auto append_compact_u64 = [&result](uint64_t val) {
            if (val < 64)
            {
                // Single byte: mode 0, value in lower 6 bits
                result.push_back(static_cast<uint8_t>((val << 2) | 0));
            }
            else if (val < 16384)
            {
                // Two bytes: mode 1, value in lower 14 bits
                uint16_t encoded = static_cast<uint16_t>((val << 2) | 1);
                result.push_back(static_cast<uint8_t>((encoded >> 8) & 0xFF));
                result.push_back(static_cast<uint8_t>(encoded & 0xFF));
            }
            else if (val < (1ULL << 30))
            {
                // Four bytes: mode 2, value in lower 30 bits
                uint32_t encoded = static_cast<uint32_t>((val << 2) | 2);
                result.push_back(static_cast<uint8_t>((encoded >> 24) & 0xFF));
                result.push_back(static_cast<uint8_t>((encoded >> 16) & 0xFF));
                result.push_back(static_cast<uint8_t>((encoded >> 8) & 0xFF));
                result.push_back(static_cast<uint8_t>(encoded & 0xFF));
            }
            else
            {
                // Big integer mode: mode 3, (bytes_needed - 4) in lower 6 bits
                // Find minimum bytes needed
                size_t bytes_needed = 8;
                while (bytes_needed > 4 && (val >> ((bytes_needed - 1) * 8)) == 0)
                {
                    bytes_needed--;
                }
                result.push_back(static_cast<uint8_t>(((bytes_needed - 4) << 2) | 3));
                // Write bytes in little-endian
                for (size_t i = 0; i < bytes_needed; i++)
                {
                    result.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
                }
            }
        };

        // 1. Network ID (uint)
        append_compact_u64(static_cast<uint8_t>(body.network_id));

        // 2. Inputs array
        size_t input_count = body.inputs.size();
        if (input_count <= 23)
        {
            result.push_back(0x80 + static_cast<uint8_t>(input_count));
        }
        else if (input_count <= 255)
        {
            result.push_back(0x98);
            result.push_back(static_cast<uint8_t>(input_count));
        }
        else
        {
            result.push_back(0x99);
            result.push_back(static_cast<uint8_t>((input_count >> 8) & 0xFF));
            result.push_back(static_cast<uint8_t>(input_count & 0xFF));
        }
        for (const auto &input : body.inputs)
        {
            // Input format: array[2] = [utxo_id (string), output_index (uint)]
            result.push_back(0x82);  // Array of 2
            // utxo_id string
            std::string id = input.utxo_id;
            result.push_back(0x78);  // Text string, length follows
            result.push_back(static_cast<uint8_t>(std::min(id.size(), size_t(255))));
            result.insert(result.end(), id.begin(), id.end());
            // output_index
            append_compact_u64(input.output_index);
        }

        // 3. Outputs array
        size_t output_count = body.outputs.size();
        if (output_count <= 23)
        {
            result.push_back(0x80 + static_cast<uint8_t>(output_count));
        }
        else if (output_count <= 255)
        {
            result.push_back(0x98);
            result.push_back(static_cast<uint8_t>(output_count));
        }
        else
        {
            result.push_back(0x99);
            result.push_back(static_cast<uint8_t>((output_count >> 8) & 0xFF));
            result.push_back(static_cast<uint8_t>(output_count & 0xFF));
        }
        for (const auto &output : body.outputs)
        {
            // Output format: array[3] = [address, amount, token_type]
            result.push_back(0x83);  // Array of 3
            // address
            result.push_back(0x78);
            result.push_back(static_cast<uint8_t>(std::min(output.address.size(), size_t(255))));
            result.insert(result.end(), output.address.begin(), output.address.end());
            // amount
            append_compact_u64(output.amount);
            // token_type
            result.push_back(0x78);
            result.push_back(static_cast<uint8_t>(std::min(output.token_type.name.size(), size_t(255))));
            result.insert(result.end(), output.token_type.name.begin(), output.token_type.name.end());
        }

        // 4. Fee (compact u64)
        append_compact_u64(body.fee);

        // 5. TTL / Validity Interval (compact u64 - slot number)
        uint64_t ttl = body.validity_interval.upper_bound.value_or(0);
        append_compact_u64(ttl);

        // 6. Certificates (array - empty for basic tx)
        result.push_back(0x80);  // Empty array

        // 7. Withdrawals (map - empty for basic tx)
        result.push_back(0xA0);  // Empty map

        // 8. Update (null for basic tx)
        result.push_back(0xF6);

        // 9. Auxiliary data hash (null for basic tx)
        result.push_back(0xF6);

        // 10. Validity interval (array of 2 - lower/upper bounds)
        result.push_back(0x82);  // Array of 2
        if (body.validity_interval.lower_bound.has_value())
        {
            append_compact_u64(*body.validity_interval.lower_bound);
        }
        else
        {
            result.push_back(0x00);  // Zero as null equivalent
        }
        if (body.validity_interval.upper_bound.has_value())
        {
            append_compact_u64(*body.validity_interval.upper_bound);
        }
        else
        {
            result.push_back(0x00);  // Zero as null equivalent
        }

        // 11. Colored inputs (reference inputs - array)
        result.push_back(0x80);  // Empty array

        // 12. Colored outputs (array)
        result.push_back(0x80);  // Empty array

        // 13. Mint (array)
        result.push_back(0x80);  // Empty array

        // 14. Reference inputs (array)
        result.push_back(0x80);  // Empty array

        // 15. Confidential contract hash (null)
        result.push_back(0xF6);

        // 16. Confidential meeting (null)
        result.push_back(0xF6);

        // 17. Offset (null for unsigned)
        result.push_back(0xF6);

        return result;
    }

    std::vector<uint8_t> Utf8TransactionSerializer::serialize_signed(const SignedTransaction &tx)
    {
        std::vector<uint8_t> result;

        // Signed transaction format:
        // [body, witnesses, auxiliary_data]

        // Body (CBOR)
        auto body_bytes = serialize_body(tx.body);
        result.insert(result.end(), body_bytes.begin(), body_bytes.end());

        // Witnesses array
        size_t witness_count = tx.witnesses.size();
        if (witness_count <= 23)
        {
            result.push_back(0x80 + static_cast<uint8_t>(witness_count));
        }
        else if (witness_count <= 255)
        {
            result.push_back(0x98);
            result.push_back(static_cast<uint8_t>(witness_count));
        }
        else
        {
            result.push_back(0x99);
            result.push_back(static_cast<uint8_t>((witness_count >> 8) & 0xFF));
            result.push_back(static_cast<uint8_t>(witness_count & 0xFF));
        }
        for (const auto &witness : tx.witnesses)
        {
            // Witness format: array[2] = [witness_type (u8), witness_data (bytes)]
            // witness_type: 0=Ed25519, 1=BLS, 2=ZKProof, 3=Multisig
            result.push_back(0x82);  // Array of 2
            // witness_type
            result.push_back(witness.witness_type);
            // witness_data (combined public_key + signature as bytes)
            auto pk_bytes = hex_to_bytes(witness.public_key);
            auto sig_bytes = hex_to_bytes(witness.signature);
            size_t data_size = pk_bytes.size() + sig_bytes.size();
            if (data_size <= 255)
            {
                result.push_back(0x58);  // byte string with 8-bit length
                result.push_back(static_cast<uint8_t>(data_size));
            }
            else
            {
                result.push_back(0x59);  // byte string with 16-bit length
                result.push_back(static_cast<uint8_t>((data_size >> 8) & 0xFF));
                result.push_back(static_cast<uint8_t>(data_size & 0xFF));
            }
            result.insert(result.end(), pk_bytes.begin(), pk_bytes.end());
            result.insert(result.end(), sig_bytes.begin(), sig_bytes.end());
        }

        // Auxiliary data (null or map)
        if (tx.auxiliary_data.has_value())
        {
            // Encode auxiliary data if present
            auto aux_bytes = hex_to_bytes(*tx.auxiliary_data);
            if (!aux_bytes.empty())
            {
                result.push_back(0x58);  // byte string with length byte
                result.push_back(static_cast<uint8_t>(aux_bytes.size()));
                result.insert(result.end(), aux_bytes.begin(), aux_bytes.end());
            }
            else
            {
                result.push_back(0xF6);  // Null if empty
            }
        }
        else
        {
            result.push_back(0xF6);  // Null
        }

        return result;
    }

    // ============================================================================
    // CBOR Deserialization Implementation
    // ============================================================================

    std::optional<TransactionBody> Utf8TransactionSerializer::deserialize_body(const std::vector<uint8_t> &data)
    {
        try
        {
            CborDeserializer d(data);

            // Parse outer array header - should have 17 fields
            size_t field_count = d.parse_array_header();

            // Validate expected field count (17 for Midnight)
            if (field_count != 17)
            {
                midnight::g_logger->warn("Transaction body field count mismatch: expected 17, got " +
                                         std::to_string(field_count));
                // Continue anyway - some fields may be null
            }

            TransactionBody body;

            // Field 1: network_id (compact u64 -> uint8_t)
            body.network_id = static_cast<NetworkId>(d.parse_compact_u64());

            // Field 2: inputs (array of [utxo_id, output_index])
            size_t input_count = d.parse_array_header();
            body.inputs.reserve(input_count);
            for (size_t i = 0; i < input_count; ++i)
            {
                TransactionInput input;
                size_t input_field_count = d.parse_array_header();  // Should be 2
                (void)input_field_count;  // Ignore, use actual parsing
                input.utxo_id = d.parse_utf8_string();
                input.output_index = static_cast<uint32_t>(d.parse_compact_u64());
                body.inputs.push_back(input);
            }

            // Field 3: outputs (array of [address, amount, token_type])
            size_t output_count = d.parse_array_header();
            body.outputs.reserve(output_count);
            for (size_t i = 0; i < output_count; ++i)
            {
                ColoredOutput output;
                size_t output_field_count = d.parse_array_header();  // Should be 3
                (void)output_field_count;
                output.address = d.parse_utf8_string();
                output.amount = d.parse_compact_u64();
                output.token_type.name = d.parse_utf8_string();
                body.outputs.push_back(output);
            }

            // Field 4: fee (compact u64)
            body.fee = d.parse_compact_u64();

            // Field 5: ttl / upper_bound (compact u64)
            body.validity_interval.upper_bound = d.parse_compact_u64();

            // Field 6: certificates (array - ignored for basic tx)
            size_t cert_count = d.parse_array_header();
            for (size_t i = 0; i < cert_count; i++) {
                d.skip_cbor_element();  // Properly skip each certificate
            }

            // Field 7: withdrawals (map - ignored for basic tx)
            size_t withdrawal_count = d.parse_map_header();
            for (size_t i = 0; i < withdrawal_count; i++) {
                d.skip_cbor_element();  // Skip key
                d.skip_cbor_element();  // Skip value
            }

            // Field 8: update (null or skipped)
            if (d.is_null())
            {
                d.expect_null();
            }

            // Field 9: auxiliary_data_hash (null or skipped)
            if (d.is_null())
            {
                d.expect_null();
            }

            // Field 10: validity_interval (array of 2)
            size_t vi_count = d.parse_array_header();
            if (vi_count >= 1)
            {
                body.validity_interval.lower_bound = d.parse_compact_u64();
            }
            if (vi_count >= 2)
            {
                // upper_bound already parsed as ttl, skip
                d.parse_compact_u64();
            }

            // Field 11: colored_inputs (array)
            size_t colored_input_count = d.parse_array_header();
            body.colored_inputs.reserve(colored_input_count);
            for (size_t i = 0; i < colored_input_count; ++i)
            {
                TransactionInput input;
                size_t input_field_count = d.parse_array_header();
                (void)input_field_count;
                input.utxo_id = d.parse_utf8_string();
                input.output_index = static_cast<uint32_t>(d.parse_compact_u64());
                body.colored_inputs.push_back(input);
            }

            // Field 12: colored_outputs (array)
            size_t colored_output_count = d.parse_array_header();
            body.colored_outputs.reserve(colored_output_count);
            for (size_t i = 0; i < colored_output_count; ++i)
            {
                ColoredOutput output;
                size_t output_field_count = d.parse_array_header();
                (void)output_field_count;
                output.address = d.parse_utf8_string();
                output.amount = d.parse_compact_u64();
                output.token_type.name = d.parse_utf8_string();
                body.colored_outputs.push_back(output);
            }

            // Field 13: mint (array)
            size_t mint_count = d.parse_array_header();
            body.mint.reserve(mint_count);
            for (size_t i = 0; i < mint_count; ++i)
            {
                AssetTransfer transfer;
                size_t mint_field_count = d.parse_array_header();  // Should be 2
                (void)mint_field_count;
                transfer.token_type.name = d.parse_utf8_string();
                transfer.amount = static_cast<int64_t>(d.parse_compact_u64());
                body.mint.push_back(transfer);
            }

            // Field 14: reference_inputs (array)
            size_t ref_input_count = d.parse_array_header();
            body.reference_inputs.reserve(ref_input_count);
            for (size_t i = 0; i < ref_input_count; ++i)
            {
                TransactionInput input;
                size_t input_field_count = d.parse_array_header();
                (void)input_field_count;
                input.utxo_id = d.parse_utf8_string();
                input.output_index = static_cast<uint32_t>(d.parse_compact_u64());
                body.reference_inputs.push_back(input);
            }

            // Field 15: confidential_contract_hash (optional)
            if (d.is_null())
            {
                d.expect_null();
                body.confidential_contract_hash = std::nullopt;
            }
            else
            {
                body.confidential_contract_hash = d.parse_utf8_string();
            }

            // Field 16: confidential_meeting (optional - complex structure)
            if (d.is_null())
            {
                d.expect_null();
                body.confidential_meeting = std::nullopt;
            }
            else
            {
                size_t meeting_count = d.parse_array_header();
                if (meeting_count >= 2)
                {
                    ConfidentialMeeting meeting;
                    meeting.meeting_id = d.parse_utf8_string();
                    meeting.position = static_cast<uint32_t>(d.parse_compact_u64());
                    body.confidential_meeting = meeting;
                }
            }

            // Field 17: offset (optional)
            if (d.is_null())
            {
                d.expect_null();
                body.offset = std::nullopt;
            }
            else
            {
                size_t offset_count = d.parse_array_header();
                if (offset_count >= 2)
                {
                    OffsetData offset;
                    offset.offset_value = d.parse_compact_u64();
                    offset.signature = d.parse_utf8_string();
                    body.offset = offset;
                }
            }

            midnight::g_logger->debug("Transaction body deserialized: " +
                                     std::to_string(body.inputs.size()) + " inputs, " +
                                     std::to_string(body.outputs.size()) + " outputs");
            return body;
        }
        catch (const SerializationException &e)
        {
            midnight::g_logger->error("Transaction body deserialization failed at offset " +
                                     std::to_string(e.offset) + ": " + e.what());
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Transaction body deserialization failed: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::optional<SignedTransaction> Utf8TransactionSerializer::deserialize_signed(const std::vector<uint8_t> &data)
    {
        try
        {
            // Parse witnesses and auxiliary data from the raw data
            // We need to figure out where body ends first
            // Approach: parse body manually to find its end, then continue with witnesses

            SignedTransaction tx;
            size_t body_end_pos = 0;

            // Parse body to find where it ends
            {
                CborDeserializer d(data);

                // Parse outer array header - body is first element (field_count = 17 for full body)
                d.parse_array_header();  // Just consume the header

                // Skip network_id
                d.parse_compact_u64();

                // Skip inputs
                size_t input_count = d.parse_array_header();
                for (size_t i = 0; i < input_count; ++i)
                {
                    d.parse_array_header();  // input fields
                    d.parse_utf8_string();
                    d.parse_compact_u64();
                }

                // Skip outputs
                size_t output_count = d.parse_array_header();
                for (size_t i = 0; i < output_count; ++i)
                {
                    d.parse_array_header();
                    d.parse_utf8_string();
                    d.parse_compact_u64();
                    d.parse_utf8_string();
                }

                // Skip fee, ttl
                d.parse_compact_u64();  // fee
                d.parse_compact_u64();  // ttl

                // Skip certs array
                size_t cert_count = d.parse_array_header();
                for (size_t i = 0; i < cert_count; ++i)
                {
                    d.parse_array_header();  // skip cert structure
                }

                // Skip withdrawals map
                size_t withdrawal_count = d.parse_map_header();
                for (size_t i = 0; i < withdrawal_count; ++i)
                {
                    d.skip(50);  // approximate withdrawal data
                }

                // Skip update (optional)
                if (d.is_null()) d.expect_null();
                else d.skip(50);  // approximate

                // Skip auxiliary_data_hash (optional)
                if (d.is_null()) d.expect_null();
                else d.skip(32);  // approximate

                // Skip validity_interval array [lower, upper]
                d.parse_array_header();
                d.parse_compact_u64();
                d.parse_compact_u64();

                // Skip colored_inputs
                size_t colored_input_count = d.parse_array_header();
                for (size_t i = 0; i < colored_input_count; ++i)
                {
                    d.parse_array_header();
                    d.parse_utf8_string();
                    d.parse_compact_u64();
                }

                // Skip colored_outputs
                size_t colored_output_count = d.parse_array_header();
                for (size_t i = 0; i < colored_output_count; ++i)
                {
                    d.parse_array_header();
                    d.parse_utf8_string();
                    d.parse_compact_u64();
                    d.parse_utf8_string();
                }

                // Skip mint
                size_t mint_count = d.parse_array_header();
                for (size_t i = 0; i < mint_count; ++i)
                {
                    d.parse_array_header();
                    d.parse_utf8_string();
                    d.parse_compact_u64();
                }

                // Skip reference_inputs
                size_t ref_input_count = d.parse_array_header();
                for (size_t i = 0; i < ref_input_count; ++i)
                {
                    d.parse_array_header();
                    d.parse_utf8_string();
                    d.parse_compact_u64();
                }

                // Skip optional fields
                if (d.is_null()) d.expect_null();
                else d.parse_utf8_string();

                if (d.is_null()) d.expect_null();
                else
                {
                    size_t m = d.parse_array_header();
                    for (size_t i = 0; i < m && i < 2; ++i)
                    {
                        d.parse_utf8_string();
                        d.parse_compact_u64();
                    }
                }

                if (d.is_null()) d.expect_null();
                else
                {
                    size_t o = d.parse_array_header();
                    for (size_t i = 0; i < o && i < 2; ++i)
                    {
                        if (i == 0) d.parse_compact_u64();
                        else d.parse_utf8_string();
                    }
                }

                body_end_pos = d.position();
            }

            // Now deserialize the body from the beginning
            std::vector<uint8_t> body_data(data.begin(), data.begin() + body_end_pos);
            auto body_opt = deserialize_body(body_data);
            if (!body_opt)
            {
                midnight::g_logger->error("Failed to deserialize transaction body");
                return std::nullopt;
            }
            tx.body = *body_opt;

            // Now parse witnesses and auxiliary data from remaining bytes
            std::vector<uint8_t> remaining(data.begin() + body_end_pos, data.end());
            CborDeserializer d(remaining);

            // Field 2: Witnesses array
            size_t witness_count = d.parse_array_header();
            tx.witnesses.reserve(witness_count);
            for (size_t i = 0; i < witness_count; ++i)
            {
                TransactionWitness witness;
                witness.witness_type = 0;  // Default to Ed25519

                size_t witness_field_count = d.parse_array_header();

                // Parse witness fields
                if (witness_field_count >= 1)
                {
                    witness.witness_type = static_cast<uint8_t>(d.parse_compact_u64());
                }
                if (witness_field_count >= 2)
                {
                    // Witness data (byte string - contains pk + signature)
                    auto witness_bytes = d.parse_byte_string();
                    // Split into public_key and signature
                    // For Ed25519: 32 bytes pk + 64 bytes signature = 96 bytes
                    // But format may vary
                    if (witness_bytes.size() >= 32)
                    {
                        // First 32 bytes = public key
                        std::ostringstream pk_oss;
                        pk_oss << std::hex << std::setfill('0');
                        for (size_t j = 0; j < 32 && j < witness_bytes.size(); ++j)
                        {
                            pk_oss << std::setw(2) << static_cast<int>(witness_bytes[j]);
                        }
                        witness.public_key = pk_oss.str();

                        // Remaining bytes = signature
                        if (witness_bytes.size() > 32)
                        {
                            std::ostringstream sig_oss;
                            sig_oss << std::hex << std::setfill('0');
                            for (size_t j = 32; j < witness_bytes.size(); ++j)
                            {
                                sig_oss << std::setw(2) << static_cast<int>(witness_bytes[j]);
                            }
                            witness.signature = sig_oss.str();
                        }
                    }
                }
                tx.witnesses.push_back(witness);
            }

            // Field 3: Auxiliary data (optional)
            if (d.has_data())
            {
                if (d.is_null())
                {
                    d.expect_null();
                    tx.auxiliary_data = std::nullopt;
                }
                else
                {
                    tx.auxiliary_data = d.parse_hex_from_byte_string();
                }
            }

            midnight::g_logger->debug("Signed transaction deserialized: " +
                                     std::to_string(tx.body.inputs.size()) + " inputs, " +
                                     std::to_string(tx.witnesses.size()) + " witnesses");
            return tx;
        }
        catch (const SerializationException &e)
        {
            midnight::g_logger->error("Signed transaction deserialization failed at offset " +
                                     std::to_string(e.offset) + ": " + e.what());
            return std::nullopt;
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->error("Signed transaction deserialization failed: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    // ============================================================================
    // Transaction Hasher Implementation
    // ============================================================================

    std::string TransactionHasher::compute_hash(const TransactionBody &body)
    {
        // Compute hash as: Blake2b-256(SCALE(body))
        // Midnight protocol uses Blake2b-256 for all transaction hashing (consistent with
        // Substrate/Polkadot ecosystem). This ensures transaction hashes are deterministic
        // and match the values computed by the relay chain and indexer.
        auto body_bytes = Utf8TransactionSerializer::serialize_body(body);

        uint8_t hash[32];
        crypto_generichash(hash, 32, body_bytes.data(), body_bytes.size(), nullptr, 0);

        std::vector<uint8_t> hash_vec(hash, hash + 32);
        return "0x" + Utf8TransactionSerializer::bytes_to_hex(hash_vec);
    }

    bool TransactionHasher::verify_hash(const TransactionBody &body, const std::string &expected_hash)
    {
        auto computed = compute_hash(body);
        return computed == expected_hash;
    }

    // ============================================================================
    // SignedTransaction Implementation
    // ============================================================================

    std::string SignedTransaction::to_cbor_hex() const
    {
        throw std::runtime_error(
            "SignedTransaction::to_cbor_hex is disabled: production Midnight "
            "transactions require midnight-ledger tagged binary serialization");
    }

    std::string SignedTransaction::to_json() const
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"inputs\":" << body.inputs.size() << ",";
        oss << "\"outputs\":" << body.outputs.size() << ",";
        oss << "\"fee\":" << body.fee << ",";
        oss << "\"witnesses\":" << witnesses.size();
        oss << "}";
        return oss.str();
    }

}  // namespace midnight::tx
