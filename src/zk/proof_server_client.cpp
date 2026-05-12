#include "midnight/zk/proof_server_client.hpp"
#include "midnight/core/logger.hpp"
#include "midnight/core/common_utils.hpp"
#include "midnight/network/midnight_node_rpc.hpp"
#include <fmt/format.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace midnight::zk
{

    std::string ProofServerClient::Config::base_url() const
    {
        std::string protocol = use_https ? "https://" : "http://";
        return fmt::format("{}{}:{}", protocol, host, port);
    }

    ProofServerClient::ProofServerClient()
        : ProofServerClient(Config())
    {
    }

    ProofServerClient::ProofServerClient(const Config &config)
        : config_(config)
    {
        network_client_ = std::make_shared<midnight::network::NetworkClient>(
            config_.base_url(),
            static_cast<uint32_t>(config_.timeout_ms.count()));
    }

    bool ProofServerClient::connect()
    {
        try
        {
            json status_response = get_server_status();
            return status_response.value("status", "") == "ok" ||
                   status_response.contains("version");
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Failed to connect to Proof Server: {}", e.what()));
            return false;
        }
    }

    bool ProofServerClient::is_connected() const
    {
        if (!network_client_)
        {
            return false;
        }

        try
        {
            auto response = network_client_->get_json("/health");
            return response.value("status", "") == "ok" ||
                   response.contains("version");
        }
        catch (...)
        {
            return false;
        }
    }

    ProofResult ProofServerClient::generate_proof(
        const std::string &circuit_name,
        const std::vector<uint8_t> &circuit_data,
        const PublicInputs &inputs,
        const std::map<std::string, WitnessOutput> &witnesses)
    {
        ProofResult result;
        result.success = false;
        (void)circuit_name;
        (void)circuit_data;
        (void)inputs;
        (void)witnesses;
        set_error("Proof generation requires a ledger-built binary payload. "
                  "Use post_proving_payload() for createProvingPayload(...) bytes.");
        result.error_message = last_error_;
        return result;

        try
        {
            // Validate inputs
            if (circuit_name.empty())
            {
                throw std::runtime_error("Circuit name cannot be empty");
            }

            if (circuit_data.empty())
            {
                throw std::runtime_error("Circuit data cannot be empty");
            }

            if (!types_util::is_valid_public_inputs(inputs))
            {
                throw std::runtime_error("Invalid public inputs");
            }

            // Build request
            ProofGenerationRequest request;
            request.circuit_name = circuit_name;
            request.circuit_data = circuit_data;
            request.circuit_inputs = inputs.inputs;
            request.witnesses = witnesses;

            // Legacy JSON path is unreachable; the live proof-server requires
            // binary payloads on /prove.
            json response = network_client_->post_json(
                "/prove",
                request.to_json());

            // Parse response — do NOT override parse_proof_response's success flag
            ProofResult parsed = parse_proof_response(response);
            result.success = parsed.success;
            if (!result.success) {
                result.error_message = parsed.error_message;
                return result;
            }

            result.proof = std::move(parsed.proof);
            // Flags reflect what parse_proof_response set: we received proof bytes
            // from the server. Actual local verification must be done separately via
            // verify_proof_local() or verify_proof(). We do NOT set
            // proof_verified_locally = true here — that would be misleading.
            result.public_inputs_valid = true;   // server validated them
            result.witnesses_valid     = true;   // server validated them

            // Record generation timestamp
            result.proof.generated_at_timestamp =
                std::chrono::system_clock::now().time_since_epoch().count() / 1000000;

            result.proof.proof_server_endpoint = config_.base_url();
            result.proof.metadata.circuit_name = circuit_name;

            return result;
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Proof generation failed: {}", e.what()));
            result.error_message = last_error_;
            return result;
        }
    }

    bool ProofServerClient::verify_proof(const CircuitProof &proof)
    {
        try
        {
            // Validate proof structure
            if (types_util::is_valid_proof_size(proof.proof.size()))
            {
                // Basic size check passed
            }
            else
            {
                set_error("Invalid proof size");
                return false;
            }

            if (!types_util::is_valid_public_inputs(proof.public_inputs))
            {
                set_error("Invalid public inputs in proof");
                return false;
            }

            // Send verification request to Proof Server
            json verify_request;
            verify_request["circuit_name"] = proof.metadata.circuit_name;
            verify_request["proof"] = proof.proof.to_json();
            verify_request["public_inputs"] = proof.public_inputs.to_json();

            json response = network_client_->post_json(
                "/verify",
                verify_request);

            if (response.contains("valid"))
            {
                return response["valid"].get<bool>();
            }

            return false;
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Proof verification failed: {}", e.what()));
            return false;
        }
    }

    CircuitProofMetadata ProofServerClient::get_circuit_metadata(const std::string &circuit_name)
    {
        CircuitProofMetadata metadata;

        try
        {
            std::string endpoint = fmt::format("/circuits/{}", circuit_name);
            json response = network_client_->get_json(endpoint);

            metadata = CircuitProofMetadata::from_json(response);
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Failed to get circuit metadata: {}", e.what()));
            metadata.circuit_name = circuit_name;
            metadata.num_constraints = 0;
        }

        return metadata;
    }

    json ProofServerClient::get_server_status()
    {
        try
        {
            json response = network_client_->get_json("/health");
            return response;
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Failed to get server status: {}", e.what()));
            return json::object();
        }
    }

    std::vector<uint8_t> ProofServerClient::post_check_payload(
        const std::vector<uint8_t> &check_payload)
    {
        if (check_payload.empty())
        {
            throw std::runtime_error("Check payload cannot be empty");
        }
        return network_client_->post_bytes("/check", check_payload);
    }

    std::vector<uint8_t> ProofServerClient::post_proving_payload(
        const std::vector<uint8_t> &proving_payload)
    {
        if (proving_payload.empty())
        {
            throw std::runtime_error("Proving payload cannot be empty");
        }
        return network_client_->post_bytes("/prove", proving_payload);
    }

    std::vector<uint8_t> ProofServerClient::post_prove_tx_payload(
        const std::vector<uint8_t> &prove_tx_payload)
    {
        if (prove_tx_payload.empty())
        {
            throw std::runtime_error("Prove-tx payload cannot be empty");
        }
        return network_client_->post_bytes("/prove-tx", prove_tx_payload);
    }

    std::string ProofServerClient::submit_proof_transaction(
        const ProofEnabledTransaction &transaction,
        const std::string &rpc_endpoint)
    {
        try
        {
            if (transaction.transaction_hex.empty())
            {
                throw std::runtime_error("Proof transaction has empty transaction_hex");
            }

            midnight::network::MidnightNodeRPC rpc(rpc_endpoint);
            return rpc.submit_transaction(
                midnight::util::ensure_hex_prefix(transaction.transaction_hex));
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Failed to submit proof transaction: {}", e.what()));
            return "";
        }
    }

    CircuitProof ProofServerClient::create_test_proof(const std::string &circuit_name)
    {
        return types_util::create_test_proof(circuit_name);
    }

    void ProofServerClient::set_config(const Config &config)
    {
        config_ = config;
        network_client_ = std::make_shared<midnight::network::NetworkClient>(
            config_.base_url(),
            static_cast<uint32_t>(config_.timeout_ms.count()));
    }

    ProofResult ProofServerClient::prove(
        const std::string &circuit_name,
        const std::vector<uint8_t> &prover_key_data,
        const json &witness_data,
        const json &public_inputs)
    {
        ProofResult result;
        result.success = false;
        (void)circuit_name;
        (void)prover_key_data;
        (void)witness_data;
        (void)public_inputs;
        set_error("Proof proving requires a ledger-built binary payload. "
                  "Use post_proving_payload() for createProvingPayload(...) bytes.");
        result.error_message = last_error_;
        return result;

        try
        {
            if (circuit_name.empty())
                throw std::runtime_error("Circuit name cannot be empty");

            if (prover_key_data.empty())
                throw std::runtime_error("Prover key data cannot be empty");

            // Build /prove request
            // Format matches httpClientProofProvider's request format
            json request;
            request["circuitName"] = circuit_name;

            // Encode prover key as base64 or hex
            std::ostringstream hex_key;
            for (auto b : prover_key_data)
                hex_key << std::hex << std::setfill('0') << std::setw(2)
                        << static_cast<int>(b);
            request["proverKey"] = hex_key.str();

            if (!witness_data.is_null())
                request["witnesses"] = witness_data;

            if (!public_inputs.is_null())
                request["publicInputs"] = public_inputs;

            // POST to /prove endpoint
            json response = network_client_->post_json("/prove", request);

            result = parse_proof_response(response);
            if (result.success)
            {
                result.proof.metadata.circuit_name = circuit_name;
                result.proof.generated_at_timestamp =
                    std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
                result.proof.proof_server_endpoint = config_.base_url();
            }
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Prove failed: {}", e.what()));
            result.error_message = last_error_;
        }

        return result;
    }

    ProofResult ProofServerClient::prove_tx(
        const std::vector<uint8_t> &tx_data,
        const std::vector<uint8_t> &zk_config)
    {
        ProofResult result;
        result.success = false;
        (void)zk_config;

        try
        {
            if (tx_data.empty())
                throw std::runtime_error("Transaction data cannot be empty");

            const auto proven_tx = post_prove_tx_payload(tx_data);
            result.success = true;
            result.proof.proof.proof_bytes = proven_tx;
            result.proof.generated_at_timestamp =
                std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
            result.proof.proof_server_endpoint = config_.base_url();
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Prove-tx failed: {}", e.what()));
            result.error_message = last_error_;
        }

        return result;
    }

    bool ProofServerClient::check_circuit(
        const std::string &circuit_name,
        const std::vector<uint8_t> &prover_key_data)
    {
        try
        {
            json request;
            request["circuitName"] = circuit_name;

            // Encode prover key as hex
            std::ostringstream hex_key;
            for (auto b : prover_key_data)
                hex_key << std::hex << std::setfill('0') << std::setw(2)
                        << static_cast<int>(b);
            request["proverKey"] = hex_key.str();

            json response = network_client_->post_json("/check", request);

            // /check returns {"capable": true/false} — require explicit confirmation
            if (response.contains("capable"))
                return response["capable"].get<bool>();

            // Require explicit capable/available flag — do NOT assume capable
            if (response.contains("available"))
                return response["available"].get<bool>();

            // No explicit field AND no error — still require the server to have
            // explicitly said capable. Treat ambiguous responses as unavailable.
            return false;
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Check failed: {}", e.what()));
            return false;
        }
    }


    json ProofServerClient::build_proof_request(
        const std::string &circuit_name,
        const std::vector<uint8_t> &circuit_data,
        const PublicInputs &inputs,
        const std::map<std::string, WitnessOutput> &witnesses)
    {
        ProofGenerationRequest request;
        request.circuit_name = circuit_name;
        request.circuit_data = circuit_data;
        request.circuit_inputs = inputs.inputs;
        request.witnesses = witnesses;

        return request.to_json();
    }

    ProofResult ProofServerClient::parse_proof_response(const json &response)
    {
        ProofResult result;
        result.success = false;

        try
        {
            if (response.contains("result"))
            {
                result.proof = CircuitProof::from_json(response["result"]);
                // proof_verified_locally stays false: we received proof bytes from the
                // server but have NOT done local verification here. Call verify_proof_local()
                // or verify_proof() to perform actual verification.
                result.public_inputs_valid = true;   // server said the response is well-formed
                result.witnesses_valid     = true;   // server confirmed witnesses
                result.success = true;
            }
            else
            {
                throw std::runtime_error("Invalid response from Proof Server");
            }
        }
        catch (const std::exception &e)
        {
            result.error_message = e.what();
        }

        return result;
    }

    void ProofServerClient::set_error(const std::string &error_msg)
    {
        last_error_ = error_msg;
    }

} // namespace midnight::zk
