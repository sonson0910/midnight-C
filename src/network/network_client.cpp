#include "midnight/network/network_client.hpp"
#include "midnight/core/logger.hpp"
#include <sstream>
#include <stdexcept>
#include <chrono>

// cpp-httplib is a header-only HTTP client library
// Download from: https://github.com/yhirose/cpp-httplib
// Single file: httplib.h
#include <httplib.h>

namespace midnight::network
{

    // ─── URL Parsing ─────────────────────────────────────────────

    NetworkClient::ParsedEndpoint NetworkClient::parse_url(const std::string &url) const
    {
        ParsedEndpoint result;
        std::string rest = url;

        // Detect scheme
        if (rest.rfind("https://", 0) == 0) {
            result.use_tls = true;
            result.port = 443;
            rest = rest.substr(8);
        } else if (rest.rfind("http://", 0) == 0) {
            result.use_tls = false;
            result.port = 80;
            rest = rest.substr(7);
        } else if (rest.rfind("wss://", 0) == 0) {
            result.use_tls = true;
            result.port = 443;
            rest = rest.substr(6);
        } else if (rest.rfind("ws://", 0) == 0) {
            result.use_tls = false;
            result.port = 80;
            rest = rest.substr(5);
        }

        // Extract host (and optional port)
        size_t slash_pos = rest.find('/');
        size_t colon_pos = rest.find(':');

        std::string host_part;
        if (slash_pos != std::string::npos && colon_pos != std::string::npos) {
            if (colon_pos < slash_pos) {
                host_part = rest.substr(0, colon_pos);
                result.port = static_cast<uint16_t>(std::stoi(rest.substr(colon_pos + 1, slash_pos - colon_pos - 1)));
                result.path_prefix = rest.substr(slash_pos);
            } else {
                host_part = rest.substr(0, slash_pos);
                result.path_prefix = rest.substr(slash_pos);
            }
        } else if (colon_pos != std::string::npos) {
            host_part = rest.substr(0, colon_pos);
            result.port = static_cast<uint16_t>(std::stoi(rest.substr(colon_pos + 1)));
        } else if (slash_pos != std::string::npos) {
            host_part = rest.substr(0, slash_pos);
            result.path_prefix = rest.substr(slash_pos);
        } else {
            host_part = rest;
        }

        result.host = host_part;
        return result;
    }

    // ─── Constructor / Destructor ────────────────────────────────

    NetworkClient::NetworkClient(const std::string &base_url, uint32_t timeout_ms)
        : base_url_(base_url), timeout_ms_(timeout_ms)
    {
        if (base_url.empty()) {
            throw std::invalid_argument("Base URL cannot be empty");
        }
        midnight::g_logger->info("NetworkClient created: " + base_url);
    }

    NetworkClient::~NetworkClient() = default;

    // ─── Header Management ──────────────────────────────────────

    void NetworkClient::set_header(const std::string &header_name, const std::string &header_value)
    {
        {
            std::lock_guard<std::mutex> lock(headers_mutex_);
            headers_[header_name] = header_value;
        }
        // Logger call OUTSIDE the lock
        if (midnight::g_logger) {
            midnight::g_logger->debug("Set header: " + header_name);
        }
    }

    void NetworkClient::clear_headers()
    {
        std::lock_guard<std::mutex> lock(headers_mutex_);
        headers_.clear();
    }

    // ─── JSON Request Helpers ───────────────────────────────────

    // Helper to build httplib::Headers from stored headers map
    static httplib::Headers build_headers(const std::map<std::string, std::string> &hdrs) {
        httplib::Headers result;
        for (const auto &kv : hdrs) {
            result.insert({kv.first, kv.second});
        }
        return result;
    }

    static std::string join_request_path(const std::string &prefix, const std::string &path)
    {
        std::string normalized_prefix = prefix;
        while (normalized_prefix.size() > 1 && normalized_prefix.back() == '/') {
            normalized_prefix.pop_back();
        }

        if (normalized_prefix.empty()) {
            return path.empty() ? "/" : path;
        }
        if (path.empty() || path == "/") {
            return normalized_prefix;
        }
        if (normalized_prefix.back() == '/' && path.front() == '/') {
            return normalized_prefix + path.substr(1);
        }
        if (normalized_prefix.back() != '/' && path.front() != '/') {
            return normalized_prefix + "/" + path;
        }
        return normalized_prefix + path;
    }

    template<typename ClientT>
    std::string NetworkClient::do_post(ClientT &cli, const std::string &path,
                                      const std::string &body, const std::string &content_type)
    {
        httplib::Headers hdrs;
        {
            std::lock_guard<std::mutex> lock(headers_mutex_);
            hdrs = build_headers(headers_);
        }

        auto res = cli.Post(path, hdrs, body, content_type);

        if (!res) {
            std::ostringstream oss;
            oss << "HTTP request failed: ";
            if (res.error() != httplib::Error::Success) {
                oss << httplib::to_string(res.error());
            } else {
                oss << "unknown";
            }
            std::string error = oss.str();
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }

        if (res->status != 200 && res->status != 201) {
            std::ostringstream oss;
            oss << "HTTP " << res->status << ": " << res->body;
            midnight::g_logger->error(oss.str());
            throw std::runtime_error(oss.str());
        }

        return res->body;
    }

    template<typename ClientT>
    std::string NetworkClient::do_get(ClientT &cli, const std::string &path)
    {
        httplib::Headers hdrs;
        {
            std::lock_guard<std::mutex> lock(headers_mutex_);
            hdrs = build_headers(headers_);
        }

        auto res = cli.Get(path, hdrs);

        if (!res) {
            std::ostringstream oss;
            oss << "HTTP GET failed: ";
            if (res.error() != httplib::Error::Success) {
                oss << httplib::to_string(res.error());
            } else {
                oss << "unknown";
            }
            std::string error = oss.str();
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }

        if (res->status != 200) {
            std::ostringstream oss;
            oss << "HTTP GET " << res->status << ": " << res->body;
            midnight::g_logger->error(oss.str());
            throw std::runtime_error(oss.str());
        }

        return res->body;
    }

    json NetworkClient::post_json(const std::string &path, const json &payload)
    {
        std::string body = payload.dump();

        midnight::g_logger->debug("HTTP POST " + base_url_ + path +
                                  " (" + std::to_string(body.length()) + " bytes)");

        ParsedEndpoint ep = parse_url(base_url_);
        std::string request_path = join_request_path(ep.path_prefix, path);

        try {
            std::string response_body;
            if (ep.use_tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                httplib::SSLClient cli(ep.host, ep.port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                response_body = do_post(cli, request_path, body, "application/json");
#else
                throw std::runtime_error("HTTPS not supported — compile with OpenSSL");
#endif
            } else {
                httplib::Client cli(ep.host, ep.port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                response_body = do_post(cli, request_path, body, "application/json");
            }

            return json::parse(response_body);
        } catch (const json::parse_error &e) {
            std::string error = "JSON parse error: " + std::string(e.what());
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }
    }

    std::vector<uint8_t> NetworkClient::post_bytes(
        const std::string &path,
        const std::vector<uint8_t> &payload,
        const std::string &content_type)
    {
        const std::string body(reinterpret_cast<const char *>(payload.data()), payload.size());

        midnight::g_logger->debug("HTTP POST " + base_url_ + path +
                                  " (" + std::to_string(body.length()) + " raw bytes)");

        ParsedEndpoint ep = parse_url(base_url_);
        std::string request_path = join_request_path(ep.path_prefix, path);

        std::string response_body;
        if (ep.use_tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(ep.host, ep.port);
            cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
            response_body = do_post(cli, request_path, body, content_type);
#else
            throw std::runtime_error("HTTPS not supported - compile with OpenSSL");
#endif
        } else {
            httplib::Client cli(ep.host, ep.port);
            cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
            response_body = do_post(cli, request_path, body, content_type);
        }

        return std::vector<uint8_t>(response_body.begin(), response_body.end());
    }

    json NetworkClient::get_json(const std::string &path)
    {
        midnight::g_logger->debug("HTTP GET " + base_url_ + path);

        ParsedEndpoint ep = parse_url(base_url_);
        std::string request_path = join_request_path(ep.path_prefix, path);

        try {
            std::string response_body;
            if (ep.use_tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                httplib::SSLClient cli(ep.host, ep.port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                response_body = do_get(cli, request_path);
#else
                throw std::runtime_error("HTTPS not supported — compile with OpenSSL");
#endif
            } else {
                httplib::Client cli(ep.host, ep.port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                response_body = do_get(cli, request_path);
            }

            return json::parse(response_body);
        } catch (const json::parse_error &e) {
            std::string error = "JSON parse error: " + std::string(e.what());
            midnight::g_logger->error(error);
            throw std::runtime_error(error);
        }
    }

    std::string NetworkClient::get_text(const std::string &path)
    {
        midnight::g_logger->debug("HTTP GET " + base_url_ + path);

        ParsedEndpoint ep = parse_url(base_url_);
        std::string request_path = join_request_path(ep.path_prefix, path);

        if (ep.use_tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(ep.host, ep.port);
            cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
            return do_get(cli, request_path);
#else
            throw std::runtime_error("HTTPS not supported - compile with OpenSSL");
#endif
        }

        httplib::Client cli(ep.host, ep.port);
        cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
        return do_get(cli, request_path);
    }

    bool NetworkClient::is_connected() const
    {
        ParsedEndpoint ep = parse_url(base_url_);
        midnight::g_logger->debug("Connectivity probe: " + ep.host + ":" + std::to_string(ep.port));

        try {
            if (ep.use_tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                httplib::SSLClient cli(ep.host, ep.port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                auto res = cli.Get(ep.path_prefix.empty() ? "/" : ep.path_prefix.c_str());
                return static_cast<bool>(res);
#else
                midnight::g_logger->warn("HTTPS probe skipped — OpenSSL unavailable");
                return false;
#endif
            } else {
                httplib::Client cli(ep.host, ep.port);
                cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms_));
                auto res = cli.Get(ep.path_prefix.empty() ? "/" : ep.path_prefix.c_str());
                return static_cast<bool>(res);
            }
        } catch (const std::exception &e) {
            midnight::g_logger->warn("Connectivity probe failed: " + std::string(e.what()));
            return false;
        }
    }

} // namespace midnight::network
