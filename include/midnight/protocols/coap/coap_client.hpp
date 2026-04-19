#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>

namespace midnight::protocols::coap
{

    enum class MessageType
    {
        CON, // Confirmable
        NON, // Non-Confirmable
        ACK, // Acknowledgement
        RST  // Reset
    };

    enum class RequestMethod
    {
        GET = 1,
        POST = 2,
        PUT = 3,
        DELETE = 4
    };

    /**
     * @brief CoAP (Constrained Application Protocol) Client
     */
    class CoapClient
    {
    public:
        /**
         * @brief Send CoAP request
         */
        std::string send_request(const std::string &uri, RequestMethod method);

        /**
         * @brief Observe resource
         */
        void observe(const std::string &uri, std::function<void(const std::string &)> callback);

        /**
         * @brief Stop observing resource
         */
        void stop_observe(const std::string &uri);

    private:
        std::map<std::string, std::function<void(const std::string &)>> observers_;
    };

} // namespace midnight::protocols::coap
