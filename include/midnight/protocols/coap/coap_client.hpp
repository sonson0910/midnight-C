#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include <cstdint>

namespace midnight::protocols::coap
{

    enum class MessageType
    {
        CON = 0, // Confirmable
        NON = 1, // Non-Confirmable
        ACK = 2, // Acknowledgement
        RST = 3  // Reset
    };

    enum class RequestMethod
    {
        GET = 1,
        POST = 2,
        PUT = 3,
        DELETE = 4
    };

    enum class ContentFormat : uint8_t
    {
        text_plain = 0,
        application_json = 50,
        application_octet_stream = 21
    };

    enum class OptionCode : uint16_t
    {
        uri_host = 3,
        uri_port = 7,
        uri_path = 11,
        content_format = 12,
        observe = 6
    };

    /**
     * @brief CoAP (Constrained Application Protocol) Client
     *
     * Implements basic CoAP client functionality using UDP sockets.
     * Supports GET, POST, PUT, DELETE methods and observe pattern.
     */
    class CoapClient
    {
    public:
        CoapClient();
        ~CoapClient();

        /**
         * @brief Send CoAP request
         * @param uri Target URI (e.g., "coap://localhost:5683/sensor/temp")
         * @param method HTTP-like method (GET, POST, PUT, DELETE)
         * @param payload Optional request payload
         * @return Response payload as string, empty on error
         */
        std::string send_request(const std::string &uri, RequestMethod method,
                                  const std::string &payload = "");

        /**
         * @brief Observe resource (request notifications)
         * @param uri Resource URI to observe
         * @param callback Function called when notifications arrive
         * @return true if observe request sent successfully
         */
        bool observe(const std::string &uri, std::function<void(const std::string &)> callback);

        /**
         * @brief Stop observing resource
         * @param uri Resource URI to stop observing
         */
        void stop_observe(const std::string &uri);

        /**
         * @brief Check if client is actively observing a resource
         */
        bool is_observing(const std::string &uri) const;

        /**
         * @brief Set receive timeout for responses
         */
        void set_timeout(int seconds);

        /**
         * @brief Get last error message
         */
        const std::string& get_last_error() const { return last_error_; }

    private:
        struct ObserverEntry
        {
            std::function<void(const std::string &)> callback;
            int sock;
            std::string uri;
        };

        // Message encoding
        std::vector<uint8_t> encode_request(RequestMethod method, const std::string &path,
                                           uint16_t port, const std::string &payload,
                                           bool observe = false);
        std::vector<uint8_t> encode_option(uint16_t option_number, const std::string &value);

        // Message decoding
        bool decode_response(const uint8_t *data, size_t len, std::string &out_payload);

        // Network operations
        bool send_udp_packet(const std::string &host, uint16_t port, const std::vector<uint8_t> &data);
        bool receive_udp_packet(int sock, std::vector<uint8_t> &buffer, int timeout_secs);

        // URI parsing
        bool parse_uri(const std::string &uri, std::string &host, uint16_t &port, std::string &path);

        // Internal state
        int timeout_secs_ = 5;
        std::string last_error_;
        uint16_t next_message_id_ = 0;
        std::map<std::string, ObserverEntry> observers_;
    };

} // namespace midnight::protocols::coap
