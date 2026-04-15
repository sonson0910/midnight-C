#include "midnight/protocols/http/http_client.hpp"

namespace midnight::protocols::http
{

    HttpResponse HttpClient::send_request(const std::string &url, HttpMethod method,
                                          const std::string &body,
                                          const std::map<std::string, std::string> &headers)
    {
        // Implementation for HTTP request
        return HttpResponse{200, "", {}};
    }

    HttpResponse HttpClient::get(const std::string &url)
    {
        return send_request(url, HttpMethod::GET);
    }

    HttpResponse HttpClient::post(const std::string &url, const std::string &body)
    {
        return send_request(url, HttpMethod::POST, body);
    }

    HttpResponse HttpClient::put(const std::string &url, const std::string &body)
    {
        return send_request(url, HttpMethod::PUT, body);
    }

    HttpResponse HttpClient::delete_request(const std::string &url)
    {
        return send_request(url, HttpMethod::DELETE);
    }

    void HttpClient::set_ssl_verify(bool verify)
    {
        ssl_verify_ = verify;
    }

} // namespace midnight::protocols::http
