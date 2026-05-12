#include "midnight/protocols/http/http_client.hpp"
#include "midnight/core/logger.hpp"
#include <httplib.h>
#include <sstream>

namespace midnight::protocols::http
{

    class HttpClientImpl
    {
    public:
        explicit HttpClientImpl(bool ssl_verify, std::chrono::seconds default_timeout)
            : ssl_verify_(ssl_verify), default_timeout_(default_timeout) {}

        template<typename ClientT>
        HttpResponse do_request(ClientT& cli, const std::string& path, HttpMethod method,
                              const std::string& body,
                              const httplib::Headers& req_headers,
                              std::chrono::seconds timeout)
        {
            // Apply per-request timeout if provided
            if (timeout.count() > 0) {
                cli.set_read_timeout(timeout.count(), 0);
                cli.set_connection_timeout(timeout.count(), 0);
            }

            auto make_response = [&](const httplib::Result& res) -> HttpResponse {
                if (!res) {
                    if (midnight::g_logger) midnight::g_logger->error("HTTP request failed");
                    return HttpResponse{0, "", {{"error", "HTTP request failed"}}};
                }
                // Populate response headers from httplib response
                std::map<std::string, std::string> resp_headers;
                for (const auto& hdr : res.value().headers) {
                    resp_headers[hdr.first] = hdr.second;
                }
                if (midnight::g_logger) midnight::g_logger->debug(
                    "HTTP " + std::to_string(res->status) + " " + path);
                return HttpResponse{res->status, res->body, std::move(resp_headers)};
            };

            if (method == HttpMethod::Get) {
                return make_response(cli.Get(path.c_str(), req_headers));
            } else if (method == HttpMethod::Post) {
                return make_response(cli.Post(path.c_str(), req_headers, body, "application/json"));
            } else if (method == HttpMethod::Put) {
                return make_response(cli.Put(path.c_str(), req_headers, body, "application/json"));
            } else if (method == HttpMethod::Delete) {
                return make_response(cli.Delete(path.c_str(), req_headers));
            } else if (method == HttpMethod::Patch) {
                return make_response(cli.Patch(path.c_str(), req_headers, body, "application/json"));
            } else if (method == HttpMethod::Head) {
                return make_response(cli.Head(path.c_str(), req_headers));
            }
            return make_response(cli.Get(path.c_str(), req_headers));
        }

        HttpResponse send_request(const std::string& url, HttpMethod method,
                                const std::string& body,
                                const std::map<std::string, std::string>& headers,
                                std::chrono::seconds timeout = std::chrono::seconds(0));

    private:
        bool ssl_verify_;
        std::chrono::seconds default_timeout_;
    };

    HttpResponse HttpClientImpl::send_request(const std::string& url, HttpMethod method,
                                            const std::string& body,
                                            const std::map<std::string, std::string>& headers,
                                            std::chrono::seconds timeout)
    {
        // Use per-request timeout if provided, otherwise fall back to default
        if (timeout.count() == 0) timeout = default_timeout_;

        std::string scheme_host;
        int port = 80;
        std::string path;
        bool use_tls = false;

        std::string rest = url;
        if (rest.rfind("https://", 0) == 0) {
            use_tls = true;
            port = 443;
            rest = rest.substr(8);
        } else if (rest.rfind("http://", 0) == 0) {
            use_tls = false;
            port = 80;
            rest = rest.substr(7);
        }

        size_t slash_pos = rest.find('/');
        size_t colon_pos = rest.find(':');

        if (colon_pos != std::string::npos && (slash_pos == std::string::npos || colon_pos < slash_pos)) {
            scheme_host = rest.substr(0, colon_pos);
            if (slash_pos != std::string::npos) {
                port = std::stoi(rest.substr(colon_pos + 1, slash_pos - colon_pos - 1));
                path = rest.substr(slash_pos);
            } else {
                path = "/";
            }
        } else if (slash_pos != std::string::npos) {
            scheme_host = rest.substr(0, slash_pos);
            path = rest.substr(slash_pos);
        } else {
            scheme_host = rest;
            path = "/";
        }

        httplib::Headers req_headers;
        for (const auto& hdr : headers) {
            req_headers.insert({hdr.first, hdr.second});
        }

        if (use_tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(scheme_host, port);
            cli.set_connection_timeout(timeout.count(), 0);
            cli.set_read_timeout(timeout.count(), 0);

            if (!ssl_verify_) {
                cli.enable_server_certificate_verification(false);
            }

            return do_request(cli, path, method, body, req_headers, timeout);
#else
            return HttpResponse{0, "", {{"error", "HTTPS not supported — compile with OpenSSL"}}};
#endif
        } else {
            httplib::Client cli(scheme_host, port);
            cli.set_connection_timeout(timeout.count(), 0);
            cli.set_read_timeout(timeout.count(), 0);
            return do_request(cli, path, method, body, req_headers, timeout);
        }
    }

    HttpClient::HttpClient()
        : pImpl_(std::make_unique<HttpClientImpl>(true, std::chrono::seconds(60)))
        , ssl_verify_(true)
        , default_timeout_(std::chrono::seconds(60))
    {
    }

    HttpClient::~HttpClient() = default;

    HttpResponse HttpClient::send_request(const std::string &url, HttpMethod method,
                                         const std::string &body,
                                         const std::map<std::string, std::string> &headers,
                                         std::chrono::seconds timeout)
    {
        return pImpl_->send_request(url, method, body, headers, timeout);
    }

    HttpResponse HttpClient::get(const std::string &url)
    {
        return send_request(url, HttpMethod::Get);
    }

    HttpResponse HttpClient::post(const std::string &url, const std::string &body)
    {
        return send_request(url, HttpMethod::Post, body);
    }

    HttpResponse HttpClient::put(const std::string &url, const std::string &body)
    {
        return send_request(url, HttpMethod::Put, body);
    }

    HttpResponse HttpClient::delete_request(const std::string &url)
    {
        return send_request(url, HttpMethod::Delete);
    }

    void HttpClient::set_ssl_verify(bool verify)
    {
        ssl_verify_ = verify;
        pImpl_ = std::make_unique<HttpClientImpl>(verify, default_timeout_);
    }

    void HttpClient::set_default_timeout(std::chrono::seconds timeout)
    {
        default_timeout_ = timeout;
        pImpl_ = std::make_unique<HttpClientImpl>(ssl_verify_, timeout);
    }

} // namespace midnight::protocols::http
