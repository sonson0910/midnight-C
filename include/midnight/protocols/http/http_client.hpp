#pragma once

#include <string>
#include <map>
#include <memory>
#include <chrono>

namespace midnight::protocols::http
{

    enum class HttpMethod
    {
        Get,
        Post,
        Put,
        Delete,
        Patch,
        Head,
        Options
    };

    struct HttpResponse
    {
        int status_code;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    class HttpClientImpl;

    /**
     * @brief HTTP/HTTPS Client
     */
    class HttpClient
    {
    public:
        HttpClient();
        ~HttpClient();

        /**
         * @brief Send HTTP request
         * @param url Full URL including scheme (http/https)
         * @param method HTTP method
         * @param body Request body (for POST/PUT/PATCH)
         * @param headers Request headers
         * @param timeout Optional per-request timeout in seconds (0 = use default)
         */
        HttpResponse send_request(const std::string &url, HttpMethod method,
                                  const std::string &body = "",
                                  const std::map<std::string, std::string> &headers = {},
                                  std::chrono::seconds timeout = std::chrono::seconds(0));

        /**
         * @brief GET request
         */
        HttpResponse get(const std::string &url);

        /**
         * @brief POST request
         */
        HttpResponse post(const std::string &url, const std::string &body);

        /**
         * @brief PUT request
         */
        HttpResponse put(const std::string &url, const std::string &body);

        /**
         * @brief DELETE request
         */
        HttpResponse delete_request(const std::string &url);

        /**
         * @brief Set SSL verification
         */
        void set_ssl_verify(bool verify);

        /**
         * @brief Set default request timeout
         * @param timeout Timeout in seconds (default: 60)
         */
        void set_default_timeout(std::chrono::seconds timeout);

    private:
        std::unique_ptr<HttpClientImpl> pImpl_;
        bool ssl_verify_ = true;
        std::chrono::seconds default_timeout_ = std::chrono::seconds(60);
    };

} // namespace midnight::protocols::http
