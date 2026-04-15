#include "midnight/zk/proof_types.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <fmt/format.h>

namespace midnight::zk
{

    // ============================================================================
    // ProofData Implementation
    // ============================================================================

    std::string ProofData::to_hex() const
    {
        std::stringstream ss;
        for (uint8_t byte : proof_bytes)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return ss.str();
    }

    ProofData ProofData::from_hex(const std::string &hex)
    {
        ProofData proof;
        proof.proof_bytes.reserve(hex.length() / 2);

        for (size_t i = 0; i < hex.length(); i += 2)
        {
            std::string byte_str = hex.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            proof.proof_bytes.push_back(byte);
        }

        return proof;
    }

    json ProofData::to_json() const
    {
        json j;
        j["proof_hex"] = to_hex();
        j["size"] = proof_bytes.size();
        return j;
    }

    ProofData ProofData::from_json(const json &j)
    {
        std::string hex = j.at("proof_hex").get<std::string>();
        return ProofData::from_hex(hex);
    }

    // ============================================================================
    // PublicInputs Implementation
    // ============================================================================

    void PublicInputs::add_input(const std::string &name, const std::string &hex_value)
    {
        inputs[name] = hex_value;
    }

    std::string PublicInputs::get_input(const std::string &name) const
    {
        auto it = inputs.find(name);
        if (it == inputs.end())
        {
            throw std::runtime_error(fmt::format("Input '{}' not found", name));
        }
        return it->second;
    }

    bool PublicInputs::has_input(const std::string &name) const
    {
        return inputs.find(name) != inputs.end();
    }

    json PublicInputs::to_json() const
    {
        json j;
        for (const auto &[name, hex_value] : inputs)
        {
            j["inputs"][name] = hex_value;
        }
        j["count"] = inputs.size();
        return j;
    }

    PublicInputs PublicInputs::from_json(const json &j)
    {
        PublicInputs pi;
        if (j.contains("inputs") && j["inputs"].is_object())
        {
            for (const auto &[name, hex_value] : j["inputs"].items())
            {
                pi.add_input(name, hex_value.get<std::string>());
            }
        }
        return pi;
    }

    // ============================================================================
    // WitnessOutput Implementation
    // ============================================================================

    std::string WitnessOutput::to_hex() const
    {
        std::stringstream ss;
        for (uint8_t byte : output_bytes)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return ss.str();
    }

    WitnessOutput WitnessOutput::from_hex(const std::string &hex)
    {
        WitnessOutput wo;
        wo.output_bytes.reserve(hex.length() / 2);

        for (size_t i = 0; i < hex.length(); i += 2)
        {
            std::string byte_str = hex.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            wo.output_bytes.push_back(byte);
        }

        return wo;
    }

    json WitnessOutput::to_json() const
    {
        json j;
        j["witness_name"] = witness_name;
        j["output_type"] = output_type;
        j["output_hex"] = to_hex();
        j["size"] = output_bytes.size();
        return j;
    }

    WitnessOutput WitnessOutput::from_json(const json &j)
    {
        WitnessOutput wo;
        wo.witness_name = j.at("witness_name").get<std::string>();
        wo.output_type = j.at("output_type").get<std::string>();
        wo.output_bytes = WitnessOutput::from_hex(j.at("output_hex").get<std::string>()).output_bytes;
        return wo;
    }

    // ============================================================================
    // CircuitProofMetadata Implementation
    // ============================================================================

    json CircuitProofMetadata::to_json() const
    {
        json j;
        j["circuit_name"] = circuit_name;
        j["circuit_hash"] = circuit_hash;
        j["num_constraints"] = num_constraints;
        j["compilation_version"] = compilation_version;
        return j;
    }

    CircuitProofMetadata CircuitProofMetadata::from_json(const json &j)
    {
        CircuitProofMetadata cpm;
        cpm.circuit_name = j.at("circuit_name").get<std::string>();
        cpm.circuit_hash = j.at("circuit_hash").get<std::string>();
        cpm.num_constraints = j.at("num_constraints").get<uint32_t>();
        cpm.compilation_version = j.at("compilation_version").get<std::string>();
        return cpm;
    }

    // ============================================================================
    // CircuitProof Implementation
    // ============================================================================

    size_t CircuitProof::total_size() const
    {
        return proof.size() + public_inputs.inputs.size() * 32; // Rough estimate
    }

    json CircuitProof::to_json() const
    {
        json j;
        j["proof"] = proof.to_json();
        j["public_inputs"] = public_inputs.to_json();
        j["metadata"] = metadata.to_json();
        j["generated_at_timestamp"] = generated_at_timestamp;
        j["proof_server_endpoint"] = proof_server_endpoint;
        j["verification_status"] = static_cast<int>(verification_status);

        json circuit_params;
        for (const auto &[key, value] : circuit_parameters)
        {
            circuit_params[key] = value;
        }
        j["circuit_parameters"] = circuit_params;

        return j;
    }

    CircuitProof CircuitProof::from_json(const json &j)
    {
        CircuitProof cp;
        cp.proof = ProofData::from_json(j.at("proof"));
        cp.public_inputs = PublicInputs::from_json(j.at("public_inputs"));
        cp.metadata = CircuitProofMetadata::from_json(j.at("metadata"));
        cp.generated_at_timestamp = j.at("generated_at_timestamp").get<uint64_t>();
        cp.proof_server_endpoint = j.at("proof_server_endpoint").get<std::string>();
        cp.verification_status = static_cast<CircuitProof::VerificationStatus>(
            j.at("verification_status").get<int>());

        if (j.contains("circuit_parameters") && j["circuit_parameters"].is_object())
        {
            for (const auto &[key, value] : j["circuit_parameters"].items())
            {
                cp.circuit_parameters[key] = value.get<std::string>();
            }
        }

        return cp;
    }

    // ============================================================================
    // ProofGenerationRequest Implementation
    // ============================================================================

    json ProofGenerationRequest::to_json() const
    {
        json j;
        j["circuit_name"] = circuit_name;

        // Encode circuit_data as hex
        std::stringstream ss;
        for (uint8_t byte : circuit_data)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        j["circuit_data_hex"] = ss.str();

        // Add circuit inputs
        json inputs_json;
        for (const auto &[name, value] : circuit_inputs)
        {
            inputs_json[name] = value;
        }
        j["circuit_inputs"] = inputs_json;

        // Add witnesses
        json witnesses_json;
        for (const auto &[name, wo] : witnesses)
        {
            witnesses_json[name] = wo.to_json();
        }
        j["witnesses"] = witnesses_json;

        return j;
    }

    ProofGenerationRequest ProofGenerationRequest::from_json(const json &j)
    {
        ProofGenerationRequest pgr;
        pgr.circuit_name = j.at("circuit_name").get<std::string>();

        // Decode circuit_data from hex
        std::string hex = j.at("circuit_data_hex").get<std::string>();
        for (size_t i = 0; i < hex.length(); i += 2)
        {
            std::string byte_str = hex.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            pgr.circuit_data.push_back(byte);
        }

        // Parse circuit inputs
        if (j.contains("circuit_inputs") && j["circuit_inputs"].is_object())
        {
            for (const auto &[name, value] : j["circuit_inputs"].items())
            {
                pgr.circuit_inputs[name] = value.get<std::string>();
            }
        }

        // Parse witnesses
        if (j.contains("witnesses") && j["witnesses"].is_object())
        {
            for (const auto &[name, wo_json] : j["witnesses"].items())
            {
                pgr.witnesses[name] = WitnessOutput::from_json(wo_json);
            }
        }

        return pgr;
    }

    // ============================================================================
    // ProofGenerationResponse Implementation
    // ============================================================================

    json ProofGenerationResponse::to_json() const
    {
        json j;
        j["success"] = success;

        if (success)
        {
            j["proof"] = generated_proof.to_json();
        }
        else
        {
            j["error_message"] = error_message;
            j["error_code"] = error_code;
        }

        json metadata_json;
        for (const auto &[key, value] : metadata)
        {
            metadata_json[key] = value;
        }
        j["metadata"] = metadata_json;
        j["generation_time_ms"] = generation_time_ms;
        j["constraints_evaluated"] = constraints_evaluated;

        return j;
    }

    ProofGenerationResponse ProofGenerationResponse::from_json(const json &j)
    {
        ProofGenerationResponse pgr;
        pgr.success = j.at("success").get<bool>();

        if (pgr.success && j.contains("proof"))
        {
            pgr.generated_proof = ProofData::from_json(j["proof"]);
        }
        else
        {
            pgr.error_message = j.value("error_message", "");
            pgr.error_code = j.value("error_code", "");
        }

        if (j.contains("metadata") && j["metadata"].is_object())
        {
            for (const auto &[key, value] : j["metadata"].items())
            {
                pgr.metadata[key] = value.get<std::string>();
            }
        }

        pgr.generation_time_ms = j.value("generation_time_ms", 0UL);
        pgr.constraints_evaluated = j.value("constraints_evaluated", 0UL);

        return pgr;
    }

    // ============================================================================
    // ProofEnabledTransaction Implementation
    // ============================================================================

    json ProofEnabledTransaction::to_json() const
    {
        json j;
        j["transaction_id"] = transaction_id;
        j["transaction_hex"] = transaction_hex;
        j["proof"] = circuit_proof.to_json();
        j["ed25519_signature"] = ed25519_signature;
        j["public_key"] = public_key;

        json ledger_json;
        for (const auto &[name, value] : ledger_updates)
        {
            ledger_json[name] = value;
        }
        j["ledger_updates"] = ledger_json;

        return j;
    }

    ProofEnabledTransaction ProofEnabledTransaction::from_json(const json &j)
    {
        ProofEnabledTransaction pet;
        pet.transaction_id = j.at("transaction_id").get<std::string>();
        pet.transaction_hex = j.at("transaction_hex").get<std::string>();
        pet.circuit_proof = CircuitProof::from_json(j.at("proof"));
        pet.ed25519_signature = j.value("ed25519_signature", "");
        pet.public_key = j.value("public_key", "");

        if (j.contains("ledger_updates") && j["ledger_updates"].is_object())
        {
            for (const auto &[name, value] : j["ledger_updates"].items())
            {
                pet.ledger_updates[name] = value.get<std::string>();
            }
        }

        return pet;
    }

    // ============================================================================
    // LedgerState Implementation
    // ============================================================================

    json LedgerState::to_json() const
    {
        json j;
        j["contract_address"] = contract_address;
        j["public_state"] = public_state;
        j["private_state"] = private_state;
        j["block_height"] = block_height;
        j["block_hash"] = block_hash;
        return j;
    }

    LedgerState LedgerState::from_json(const json &j)
    {
        LedgerState ls;
        ls.contract_address = j.at("contract_address").get<std::string>();
        ls.public_state = j.at("public_state");
        ls.private_state = j.value("private_state", json::object());
        ls.block_height = j.at("block_height").get<uint64_t>();
        ls.block_hash = j.at("block_hash").get<std::string>();
        return ls;
    }

    // ============================================================================
    // Utility Functions
    // ============================================================================

    namespace types_util
    {

        std::string commitment_to_hex(const std::vector<uint8_t> &commitment)
        {
            std::stringstream ss;
            for (uint8_t byte : commitment)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            return ss.str();
        }

        std::vector<uint8_t> hex_to_commitment(const std::string &hex)
        {
            std::vector<uint8_t> commitment;
            commitment.reserve(hex.length() / 2);

            for (size_t i = 0; i < hex.length(); i += 2)
            {
                std::string byte_str = hex.substr(i, 2);
                uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
                commitment.push_back(byte);
            }

            return commitment;
        }

        bool is_valid_proof_size(size_t size)
        {
            // Proofs should be at least 64 bytes and less than 10MB
            return size >= 64 && size <= (10 * 1024 * 1024);
        }

        bool is_valid_public_inputs(const PublicInputs &inputs)
        {
            // At least one public input should exist
            if (inputs.inputs.empty())
            {
                return false;
            }

            // Each input should be non-empty hex string
            for (const auto &[name, hex_value] : inputs.inputs)
            {
                if (name.empty() || hex_value.empty())
                {
                    return false;
                }
                // Verify it's valid hex
                for (char c : hex_value)
                {
                    if (!std::isxdigit(c))
                    {
                        return false;
                    }
                }
            }

            return true;
        }

        CircuitProof create_test_proof(const std::string &circuit_name)
        {
            CircuitProof proof;

            // Create dummy proof data
            ProofData pd;
            pd.proof_bytes.resize(128, 0xAA);
            proof.proof = pd;

            // Create dummy public inputs
            proof.public_inputs.add_input("state", "0000000000000000000000000000000000000000000000000000000000000000");
            proof.public_inputs.add_input("owner", "1111111111111111111111111111111111111111111111111111111111111111");

            // Create metadata
            proof.metadata.circuit_name = circuit_name;
            proof.metadata.circuit_hash = "test_hash_" + circuit_name;
            proof.metadata.num_constraints = 4569;
            proof.metadata.compilation_version = "0.22.0";

            proof.generated_at_timestamp = 0;
            proof.proof_server_endpoint = "http://localhost:6300";
            proof.verification_status = CircuitProof::VerificationStatus::UNVERIFIED;

            return proof;
        }

    } // namespace types_util

} // namespace midnight::zk
