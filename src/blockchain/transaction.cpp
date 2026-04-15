#include "midnight/blockchain/transaction.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <numeric>
#include <iomanip>

namespace midnight::blockchain
{

    Transaction::Transaction(const std::string &tx_id) : tx_id_(tx_id)
    {
        if (tx_id.empty())
        {
            tx_id_ = "tx_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        }
    }

    void Transaction::add_input(const Input &input)
    {
        inputs_.push_back(input);
        std::ostringstream msg;
        msg << "Added input: " << input.tx_hash.substr(0, 16) << "...#" << input.output_index;
        midnight::g_logger->debug(msg.str());
    }

    void Transaction::add_output(const Output &output)
    {
        outputs_.push_back(output);
        std::ostringstream msg;
        msg << "Added output: " << output.address.substr(0, 30) << "... ("
            << output.amount << " lovelace)";
        midnight::g_logger->debug(msg.str());
    }

    void Transaction::add_certificate(const Certificate &cert)
    {
        certificates_.push_back(cert);
        std::ostringstream msg;
        msg << "Added certificate type: " << static_cast<int>(cert.type);
        midnight::g_logger->debug(msg.str());
    }

    void Transaction::set_validity(uint64_t invalid_hereafter, uint64_t invalid_before)
    {
        invalid_hereafter_ = invalid_hereafter;
        invalid_before_ = invalid_before;

        std::ostringstream msg;
        msg << "Set validity: " << invalid_before << " to " << invalid_hereafter;
        midnight::g_logger->debug(msg.str());
    }

    std::string Transaction::calculate_hash() const
    {
        // SHA256 hash - simplified for demo
        // In production, would use proper SHA256
        std::ostringstream hash;
        hash << std::hex;

        // Simple hash of transaction contents
        for (const auto &input : inputs_)
        {
            for (char c : input.tx_hash)
            {
                hash << (static_cast<int>(c) & 0xF);
            }
        }

        for (const auto &output : outputs_)
        {
            for (char c : output.address)
            {
                hash << (static_cast<int>(c) & 0xF);
            }
        }

        std::string result = hash.str();
        if (result.length() < 64)
        {
            result.append(64 - result.length(), '0');
        }

        return result.substr(0, 64);
    }

    uint64_t Transaction::get_total_inputs() const
    {
        // In real implementation, would sum actual UTXO amounts
        // For now, just return count as placeholder
        return inputs_.size() * 2000000; // Mock: 2 ADA per input
    }

    uint64_t Transaction::get_total_outputs() const
    {
        return std::accumulate(outputs_.begin(), outputs_.end(), static_cast<uint64_t>(0),
                               [](uint64_t sum, const Output &output)
                               { return sum + output.amount; });
    }

    std::string Transaction::to_cbor_hex() const
    {
        // CBOR hex serialization based on industry-standard patterns
        // Mock implementation - in production would use proper CBOR encoding

        // Mock CBOR hex - simplified representation
        std::ostringstream cbor;
        cbor << "a5"   // Map with 5 items (Cardano tx format)
             << "0001" // Key 0 (inputs)
             << std::hex << std::setfill('0') << std::setw(2) << (int)inputs_.size();

        for (const auto &input : inputs_)
        {
            cbor << "1820"; // Tag and encoded data
        }

        cbor << "0101" // Key 1 (outputs)
             << std::hex << std::setfill('0') << std::setw(2) << (int)outputs_.size();

        for (const auto &output : outputs_)
        {
            cbor << "a2"                               // Map with 2 items
                 << "00" << std::dec << output.amount; // Address
        }

        cbor << "0202" // Key 2 (fee)
             << std::dec << calculate_min_fee();

        cbor << "0303" // Key 3 (ttl)
             << std::dec << invalid_hereafter_;

        return cbor.str();
    }

    std::string Transaction::to_json() const
    {
        // CIP-116 JSON serialization format
        std::ostringstream json;
        json << "{"
             << "\"type\": \"Tx\","
             << "\"description\": \"Transaction\","
             << "\"cborHex\": \"" << to_cbor_hex() << "\""
             << "}";

        return json.str();
    }

    size_t Transaction::get_size() const
    {
        // Estimate transaction size in bytes
        size_t size = 0;

        // Inputs: ~45 bytes each
        size += inputs_.size() * 45;

        // Outputs: ~50 bytes each
        size += outputs_.size() * 50;

        // Certificates: ~100 bytes each
        size += certificates_.size() * 100;

        // Overhead: ~200 bytes
        size += 200;

        return size;
    }

    Transaction Transaction::from_cbor_hex(const std::string &cbor_hex)
    {
        Transaction tx("deserialized");

#ifdef ENABLE_CARDANO_REAL
        // In production, would use proper libraries:
        // uint8_t cbor_buffer[4096];
        // from_hex_string(cbor_hex, cbor_buffer);
        // cardano_transaction_t* parsed_tx = NULL;
        // cardano_transaction_from_cbor(cbor_buffer, cbor_hex.length()/2, &parsed_tx);
        // ... extract fields from parsed_tx ...
#endif

        midnight::g_logger->debug("Deserialized transaction from CBOR");
        return tx;
    }

    // Helper for fee calculation (simplified)
    uint64_t Transaction::calculate_min_fee() const
    {
        // Cardano fee formula: fee = minFeeA * size + minFeeB
        // Using defaults: minFeeA = 44, minFeeB = 155381
        const uint64_t MIN_FEE_A = 44;
        const uint64_t MIN_FEE_B = 155381;

        return MIN_FEE_A * get_size() + MIN_FEE_B;
    }

} // namespace midnight::blockchain
