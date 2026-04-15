#include "midnight/zk/proof_server_client.hpp"
#include <fmt/format.h>
#include <chrono>

namespace midnight::zk
{

    std::string ProofServerClient::Config::base_url() const
    {
        std::string protocol = use_https ? "https://" : "http://";
        return fmt::format("{}{}:{}", protocol, host, port);
    }

    ProofServerClient::ProofServerClient(const Config &config)
        : config_(config)
    {
        network_client_ = std::make_shared<midnight::network::NetworkClient>();
    }

    bool ProofServerClient::connect()
    {
        try
        {
            json status_response = get_server_status();
            return status_response.contains("version");
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
            std::string endpoint = fmt::format("{}/status", config_.base_url());
            auto response = network_client_->get_json(endpoint, config_.timeout_ms.count());
            return response.contains("version");
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

            // Send to Proof Server
            std::string endpoint = fmt::format("{}/generate", config_.base_url());
            json response = network_client_->post_json(
                endpoint,
                request.to_json(),
                config_.timeout_ms.count());

            // Parse response
            result = parse_proof_response(response);
            result.success = true;

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
            std::string endpoint = fmt::format("{}/verify", config_.base_url());

            json verify_request;
            verify_request["circuit_name"] = proof.metadata.circuit_name;
            verify_request["proof"] = proof.proof.to_json();
            verify_request["public_inputs"] = proof.public_inputs.to_json();

            json response = network_client_->post_json(
                endpoint,
                verify_request,
                config_.timeout_ms.count());

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
            std::string endpoint = fmt::format("{}/circuits/{}", config_.base_url(), circuit_name);
            json response = network_client_->get_json(endpoint, config_.timeout_ms.count());

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
            std::string endpoint = fmt::format("{}/status", config_.base_url());
            json response = network_client_->get_json(endpoint, config_.timeout_ms.count());
            return response;
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Failed to get server status: {}", e.what()));
            return json::object();
        }
    }

    std::string ProofServerClient::submit_proof_transaction(
        const ProofEnabledTransaction &transaction,
        const std::string &rpc_endpoint)
    {
        try
        {
            // Create RPC call to submit transaction with proof
            json rpc_request;
            rpc_request["jsonrpc"] = "2.0";
            rpc_request["id"] = 1;
            rpc_request["method"] = "submitTransaction";

            json params;
            params["transaction"] = transaction.to_json();
            rpc_request["params"] = params;

            // Send to Midnight RPC node
            json response = network_client_->post_json(
                rpc_endpoint,
                rpc_request,
                config_.timeout_ms.count());

            // Extract transaction hash from response
            if (response.contains("result"))
            {
                return response["result"].get<std::string>();
            }
            else if (response.contains("error"))
            {
                throw std::runtime_error(response["error"].dump());
            }

            throw std::runtime_error("No result in RPC response");
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
                result.proof_verified_locally = true;
                result.public_inputs_valid = true;
                result.witnesses_valid = true;
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
