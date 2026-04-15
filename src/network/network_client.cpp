#include "midnight/network/network_client.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <stdexcept>
#include <chrono>

// cpp-httplib is a header-only HTTP client library
// Download from: https://github.com/yhirose/cpp-httplib
// Single file: httplib.h
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

namespace midnight::network
{
    namespace
    {
        std::string normalize_joined_path(const std::string &base_path, const std::string &path)
        {
            std::string normalized_base = base_path.empty() ? "/" : base_path;
            if (normalized_base.front() != '/')
            {
                normalized_base.insert(normalized_base.begin(), '/');
            }
            while (normalized_base.size() > 1 && normalized_base.back() == '/')
            {
                normalized_base.pop_back();
            }

            if (path.empty() || path == "/")
            {
                return normalized_base;
            }

            std::string normalized_path = path;
            if (normalized_path.front() != '/')
            {
                normalized_path.insert(normalized_path.begin(), '/');
            }

            if (normalized_path == normalized_base ||
                (normalized_base != "/" && normalized_path.rfind(normalized_base + "/", 0) == 0))
            {
                return normalized_path;
            }

            if (normalized_base == "/")
            {
                return normalized_path;
            }

            return normalized_base + normalized_path;
        }
    } // namespace


    NetworkClient::NetworkClient(const std::string &base_url, uint32_t timeout_ms)
        : base_url_(base_url), timeout_ms_(timeout_ms)
    {
        midnight::g_logger->info("NetworkClient created: " + base_url);

        // Validate URL format
        if (base_url.empty())
        {
            throw std::invalid_argument("Base URL cannot be empty");
        }
    }

    NetworkClient::~NetworkClient() = default;

    json NetworkClient::post_json(const std::string &path, const json &payload)
    {
        std::string body = payload.dump();
        std::string response_body = http_post(path, body, "application/json");

        try
        {
            return json::parse(response_body);
        }
        catch (const json::exception &e)
        {
            std::string error = "Failed to parse JSON response: " + std::string(e.what());
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }
    }

    json NetworkClient::get_json(const std::string &path)
    {
        std::string response_body = http_get(path);

        try
        {
            return json::parse(response_body);
        }
        catch (const json::exception &e)
        {
            std::string error = "Failed to parse JSON response: " + std::string(e.what());
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }
    }

    void NetworkClient::set_header(const std::string &header_name, const std::string &header_value)
    {
        // Store custom headers for future requests - not yet persisted
        midnight::g_logger->debug("Set header: " + header_name + ": " + header_value);
    }

    bool NetworkClient::is_connected() const
    {
        try
        {
            // Parse URL to get host
            std::string url = base_url_;
            bool is_https = (url.find("https://") == 0);

            if (url.find("://") != std::string::npos)
            {
                url = url.substr(url.find("://") + 3);
            }

            // Extract host and port
            std::string host = url;
            std::string request_path = "/";
            if (url.find('/') != std::string::npos)
            {
                host = url.substr(0, url.find('/'));
                request_path = url.substr(url.find('/'));
            }

            // Check if host has port
            uint16_t port = is_https ? 443 : 80;
            if (host.find(':') != std::string::npos)
            {
                port = std::stoul(host.substr(host.find(':') + 1));
                host = host.substr(0, host.find(':'));
            }

            request_path = normalize_joined_path(request_path, "/");

            midnight::g_logger->debug("Checking connectivity to: " + host + ":" + std::to_string(port));

            // Probe endpoint with a lightweight GET request to verify real connectivity
            if (is_https)
            {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                httplib::SSLClient cli(host, port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                auto res = cli.Get(request_path.c_str());
                if (res)
                {
                    return true;
                }
                midnight::g_logger->debug("Connectivity probe failed for HTTPS endpoint");
                return false;
#else
                midnight::g_logger->warn("HTTPS not supported - OpenSSL required");
                return false;
#endif
            }
            else
            {
                httplib::Client cli(host, port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                auto res = cli.Get(request_path.c_str());
                if (res)
                {
                    return true;
                }
                midnight::g_logger->debug("Connectivity probe failed for HTTP endpoint");
                return false;
            }
        }
        catch (const std::exception &e)
        {
            midnight::g_logger->warn("Connectivity check failed: " + std::string(e.what()));
            return false;
        }
    }

    std::string NetworkClient::http_post(
        const std::string &path,
        const std::string &body,
        const std::string &content_type)
    {
        std::ostringstream msg;
        msg << "HTTP POST " << base_url_ << path << " (" << body.length() << " bytes)";
        midnight::g_logger->debug(msg.str());

        try
        {
            // Parse URL
            std::string url = base_url_;
            bool is_https = (url.find("https://") == 0);

            if (url.find("://") != std::string::npos)
            {
                url = url.substr(url.find("://") + 3);
            }

            // Extract host and port
            std::string host = url;
            std::string request_path = "/";

            if (url.find('/') != std::string::npos)
            {
                host = url.substr(0, url.find('/'));
                request_path = url.substr(url.find('/'));
            }

            uint16_t port = is_https ? 443 : 80;
            if (host.find(':') != std::string::npos)
            {
                port = std::stoul(host.substr(host.find(':') + 1));
                host = host.substr(0, host.find(':'));
            }

            request_path = normalize_joined_path(request_path, path);

            msg.str("");
            msg << "POST " << host << ":" << port << request_path;
            midnight::g_logger->debug(msg.str());

            // Make HTTP request
            if (is_https)
            {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                httplib::SSLClient cli(host, port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));

                auto res = cli.Post(
                    request_path.c_str(),
                    body,
                    content_type.c_str());

                if (!res || res->status != 200)
                {
                    std::string error = "HTTP error: " + std::to_string(res ? res->status : 0);
                    midnight::g_logger->error(error);
                    throw std::runtime_error(error);
                }

                return res->body;
#else
                throw std::runtime_error("HTTPS not supported - OpenSSL required");
#endif
            }
            else
            {
                httplib::Client cli(host, port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));

                auto res = cli.Post(
                    request_path.c_str(),
                    body,
                    content_type.c_str());

                if (!res || res->status != 200)
                {
                    std::string error = "HTTP error: " + std::to_string(res ? res->status : 0);
                    midnight::g_logger->error(error);
                    throw std::runtime_error(error);
                }

                return res->body;
            }
        }
        catch (const std::exception &e)
        {
            msg.str("");
            msg << "HTTP POST failed: " << e.what();
            midnight::g_logger->error(msg.str());
            throw;
        }
    }

    std::string NetworkClient::http_get(const std::string &path)
    {
        std::ostringstream msg;
        msg << "HTTP GET " << base_url_ << path;
        midnight::g_logger->debug(msg.str());

        try
        {
            // Parse URL (same logic as http_post)
            std::string url = base_url_;
            bool is_https = (url.find("https://") == 0);

            if (url.find("://") != std::string::npos)
            {
                url = url.substr(url.find("://") + 3);
            }

            std::string host = url;
            std::string request_path = "/";

            if (url.find('/') != std::string::npos)
            {
                host = url.substr(0, url.find('/'));
                request_path = url.substr(url.find('/'));
            }

            uint16_t port = is_https ? 443 : 80;
            if (host.find(':') != std::string::npos)
            {
                port = std::stoul(host.substr(host.find(':') + 1));
                host = host.substr(0, host.find(':'));
            }

            request_path = normalize_joined_path(request_path, path);

            msg.str("");
            msg << "GET " << host << ":" << port << request_path;
            midnight::g_logger->debug(msg.str());

            // Make HTTP request
            if (is_https)
            {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                httplib::SSLClient cli(host, port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));

                auto res = cli.Get(request_path.c_str());

                if (!res || res->status != 200)
                {
                    std::string error = "HTTP error: " + std::to_string(res ? res->status : 0);
                    midnight::g_logger->error(error);
                    throw std::runtime_error(error);
                }

                return res->body;
#else
                throw std::runtime_error("HTTPS not supported - OpenSSL required");
#endif
            }
            else
            {
                httplib::Client cli(host, port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));

                auto res = cli.Get(request_path.c_str());

                if (!res || res->status != 200)
                {
                    std::string error = "HTTP error: " + std::to_string(res ? res->status : 0);
                    midnight::g_logger->error(error);
                    throw std::runtime_error(error);
                }

                return res->body;
            }
        }
        catch (const std::exception &e)
        {
            msg.str("");
            msg << "HTTP GET failed: " << e.what();
            midnight::g_logger->error(msg.str());
            throw;
        }
    }

} // namespace midnight::network
