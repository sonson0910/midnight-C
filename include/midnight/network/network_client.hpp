#ifndef MIDNIGHT_NETWORK_CLIENT_HPP
#define MIDNIGHT_NETWORK_CLIENT_HPP

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

namespace midnight::network
{

    using json = nlohmann::json;

    /**
     * @brief Simple HTTP client for making JSON-RPC requests to Midnight nodes
     *
     * Provides abstraction over HTTP communication with JSON serialization/deserialization.
     * Thread-safe for read operations, not recommended for concurrent writes.
     *
     * Example usage:
     * @code
     *   NetworkClient client("http://localhost:5678", 5000);
     *   auto response = client.post_json("/rpc", json::object({
     *       {"jsonrpc", "2.0"},
     *       {"method", "submitTransaction"},
     *       {"params", {{"tx", tx_hex}}},
     *       {"id", 1}
     *   }));
     * @endcode
     */
    class NetworkClient
    {
    public:
        /**
         * @brief Construct HTTP client
         * @param base_url Base URL (e.g., "http://localhost:5678")
         * @param timeout_ms Timeout in milliseconds
         */
        NetworkClient(const std::string &base_url, uint32_t timeout_ms = 5000);

        ~NetworkClient();

        /**
         * @brief Make HTTP POST request with JSON payload
         * @param path URL path (e.g., "/rpc")
         * @param payload JSON request body
         * @return JSON response body
         * @throws std::runtime_error on HTTP/network error
         */
        json post_json(const std::string &path, const json &payload);

        /**
         * @brief Make HTTP GET request
         * @param path URL path
         * @return JSON response body
         * @throws std::runtime_error on HTTP/network error
         */
        json get_json(const std::string &path);

        /**
         * @brief Set HTTP header (persisted for all subsequent requests)
         * @param header_name Name of header
         * @param header_value Value of header
         */
        void set_header(const std::string &header_name, const std::string &header_value);

        /**
         * @brief Clear all persisted HTTP headers
         */
        void clear_headers();

        /**
         * @brief Check connection status
         * @return true if endpoint is reachable
         */
        bool is_connected() const;

        /**
         * @brief Get base URL
         */
        std::string get_base_url() const { return base_url_; }

        /**
         * @brief Get timeout in milliseconds
         */
        uint32_t get_timeout_ms() const { return timeout_ms_; }

    private:
        std::string base_url_;
        uint32_t timeout_ms_;
        mutable std::map<std::string, std::string> headers_;
        mutable std::mutex headers_mutex_;

        struct ParsedEndpoint {
            std::string host;
            uint16_t port = 80;
            std::string path_prefix;
            bool use_tls = false;
        };

        ParsedEndpoint parse_url(const std::string &url) const;

        template<typename ClientT>
        std::string do_post(ClientT &cli, const std::string &path,
                           const std::string &body, const std::string &content_type);

        template<typename ClientT>
        std::string do_get(ClientT &cli, const std::string &path);
    };

} // namespace midnight::network

#endif // MIDNIGHT_NETWORK_CLIENT_HPP
