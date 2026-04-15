#pragma once

#include <string>
#include <map>
#include <memory>

namespace midnight::protocols::http
{

    enum class HttpMethod
    {
        GET,
        POST,
        PUT,
        DELETE,
        PATCH,
        HEAD,
        OPTIONS
    };

    struct HttpResponse
    {
        int status_code;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    /**
     * @brief HTTP/HTTPS Client
     */
    class HttpClient
    {
    public:
        /**
         * @brief Send HTTP request
         */
        HttpResponse send_request(const std::string &url, HttpMethod method,
                                  const std::string &body = "",
                                  const std::map<std::string, std::string> &headers = {});

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

    private:
        bool ssl_verify_ = true;
    };

} // namespace midnight::protocols::http
