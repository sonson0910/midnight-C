#include "midnight/protocols/coap/coap_client.hpp"

namespace midnight::protocols::coap
{

    std::string CoapClient::send_request(const std::string &uri, RequestMethod method)
    {
        // Implementation for CoAP request
        return "";
    }

    void CoapClient::observe(const std::string &uri, std::function<void(const std::string &)> callback)
    {
        observers_[uri] = callback;
        // Implementation for CoAP observe
    }

    void CoapClient::stop_observe(const std::string &uri)
    {
        observers_.erase(uri);
    }

} // namespace midnight::protocols::coap
