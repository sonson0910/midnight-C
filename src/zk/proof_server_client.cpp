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
    namespace
    {
        std::string trim_copy(std::string value)
        {
            auto not_space = [](unsigned char ch) {
                return !std::isspace(ch);
            };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
            value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
            return value;
        }
    }

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

    json ProofServerClient::get_server_status()
    {
        try
        {
            json response = network_client_->get_json("/health");
            try
            {
                response["version"] = get_server_version();
            }
            catch (const std::exception &e)
            {
                response["version_error"] = e.what();
            }
            try
            {
                response["proof_versions"] = get_supported_proof_versions();
            }
            catch (const std::exception &e)
            {
                response["proof_versions_error"] = e.what();
            }
            return response;
        }
        catch (const std::exception &e)
        {
            set_error(fmt::format("Failed to get server status: {}", e.what()));
            return json::object();
        }
    }

    std::string ProofServerClient::get_server_version()
    {
        return trim_copy(network_client_->get_text("/version"));
    }

    json ProofServerClient::get_supported_proof_versions()
    {
        return network_client_->get_json("/proof-versions");
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

    void ProofServerClient::set_config(const Config &config)
    {
        config_ = config;
        network_client_ = std::make_shared<midnight::network::NetworkClient>(
            config_.base_url(),
            static_cast<uint32_t>(config_.timeout_ms.count()));
    }

    void ProofServerClient::set_error(const std::string &error_msg)
    {
        last_error_ = error_msg;
    }

} // namespace midnight::zk
