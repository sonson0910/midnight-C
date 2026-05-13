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
    }

    bool ProofServerClient::verify_proof(const CircuitProof &proof)
    {
        if (!types_util::is_valid_proof_size(proof.proof.size()))
        {
            set_error("Invalid proof size");
            return false;
        }

        if (!types_util::is_valid_public_inputs(proof.public_inputs))
        {
            set_error("Invalid public inputs in proof");
            return false;
        }

        set_error("JSON /verify is not a Midnight proof-server endpoint. "
                  "Verify via ledger/proof-server binary check payloads or node submission.");
        return false;
    }

    CircuitProofMetadata ProofServerClient::get_circuit_metadata(const std::string &circuit_name)
    {
        CircuitProofMetadata metadata;
        metadata.circuit_name = circuit_name;
        metadata.num_constraints = 0;
        set_error("Circuit metadata is not exposed by the Midnight proof-server JSON API. "
                  "Load circuit metadata from Compact/ledger artifacts.");
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
        (void)circuit_name;
        (void)prover_key_data;
        set_error("Circuit checks require ledger createCheckPayload(...) binary bytes. "
                  "Use post_check_payload() instead of JSON /check.");
        return false;
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
