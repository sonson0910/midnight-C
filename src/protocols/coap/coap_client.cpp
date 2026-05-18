#include "midnight/protocols/coap/coap_client.hpp"
#include "midnight/core/logger.hpp"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    typedef int socklen_t;
    typedef SSIZE_T ssize_t;
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <cstring>
#endif

#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>

namespace midnight::protocols::coap
{

    namespace
    {
        // CoAP constants
        constexpr uint8_t COAP_VERSION = 1;
        constexpr uint8_t COAP_HEADER_SIZE = 4;
        constexpr uint8_t PAYLOAD_MARKER = 0xFF;
        constexpr uint16_t DEFAULT_COAP_PORT = 5683;
        constexpr uint16_t DEFAULT_COAPS_PORT = 5684;
        constexpr size_t MAX_UDP_PACKET_SIZE = 1152;

        uint8_t generate_token()
        {
            static std::mt19937 rng(static_cast<uint32_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()));
            return static_cast<uint8_t>(rng() & 0xFF);
        }
    }

    CoapClient::CoapClient()
    {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            last_error_ = "Winsock initialization failed";
            midnight::g_logger->error(last_error_);
        }
#endif
    }

    CoapClient::~CoapClient()
    {
        stop_observe("");
#ifdef _WIN32
        WSACleanup();
#endif
    }

    std::string CoapClient::send_request(const std::string &uri, RequestMethod method,
                                         const std::string &payload)
    {
        // Parse URI
        std::string host;
        uint16_t port;
        std::string path;
        if (!parse_uri(uri, host, port, path)) {
            return "";
        }

        midnight::g_logger->debug("CoAP request: method=" + std::to_string(static_cast<int>(method)) +
                                 " uri=" + uri);

        // Encode CoAP request
        std::vector<uint8_t> request = encode_request(method, path, port, payload);
        if (request.empty()) {
            last_error_ = "Failed to encode CoAP request";
            return "";
        }

        // Send UDP packet
        if (!send_udp_packet(host, port, request)) {
            return "";
        }

        // Receive response
        std::vector<uint8_t> response_buffer;
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            last_error_ = "Failed to create receive socket";
            midnight::g_logger->error(last_error_);
            return "";
        }

        // Set receive timeout
#ifdef _WIN32
        DWORD tv = timeout_secs_ * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = timeout_secs_;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        bool got_response = receive_udp_packet(sock, response_buffer, timeout_secs_);

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif

        if (!got_response || response_buffer.empty()) {
            last_error_ = "No response from server";
            midnight::g_logger->warn(last_error_);
            return "";
        }

        // Decode response
        std::string response_payload;
        if (!decode_response(response_buffer.data(), response_buffer.size(), response_payload)) {
            last_error_ = "Failed to decode CoAP response";
            return "";
        }

        midnight::g_logger->debug("CoAP response received: " + std::to_string(response_payload.size()) + " bytes");
        return response_payload;
    }

    bool CoapClient::observe(const std::string &uri, std::function<void(const std::string &)> callback)
    {
        if (!callback) {
            last_error_ = "Invalid callback";
            return false;
        }

        // Parse URI
        std::string host;
        uint16_t port;
        std::string path;
        if (!parse_uri(uri, host, port, path)) {
            return false;
        }

        // Stop existing observer for this URI
        stop_observe(uri);

        // Create UDP socket for observe
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            last_error_ = "Failed to create observe socket";
            midnight::g_logger->error(last_error_);
            return false;
        }

        // Set socket to non-blocking for async receive
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

        // Store observer
        ObserverEntry entry;
        entry.callback = std::move(callback);
        entry.sock = sock;
        entry.uri = uri;
        observers_[uri] = std::move(entry);

        midnight::g_logger->info("CoAP observe started: " + uri);
        return true;
    }

    void CoapClient::stop_observe(const std::string &uri)
    {
        if (uri.empty()) {
            // Stop all observers
            for (auto &[key, entry] : observers_) {
#ifdef _WIN32
                closesocket(entry.sock);
#else
                close(entry.sock);
#endif
                midnight::g_logger->info("CoAP observe stopped: " + entry.uri);
            }
            observers_.clear();
            return;
        }

        auto it = observers_.find(uri);
        if (it != observers_.end()) {
#ifdef _WIN32
            closesocket(it->second.sock);
#else
            close(it->second.sock);
#endif
            observers_.erase(it);
            midnight::g_logger->info("CoAP observe stopped: " + uri);
        }
    }

    bool CoapClient::is_observing(const std::string &uri) const
    {
        return observers_.find(uri) != observers_.end();
    }

    void CoapClient::set_timeout(int seconds)
    {
        timeout_secs_ = std::max(1, std::min(seconds, 60));
    }

    // === Private methods ===

    bool CoapClient::parse_uri(const std::string &uri, std::string &host, uint16_t &port,
                               std::string &path)
    {
        host.clear();
        path.clear();
        port = DEFAULT_COAP_PORT;

        // Check scheme
        std::string working_uri = uri;
        if (working_uri.rfind("coap://", 0) == 0) {
            working_uri = working_uri.substr(7);
        } else if (working_uri.rfind("coaps://", 0) == 0) {
            port = DEFAULT_COAPS_PORT;
            last_error_ =
                "CoAPS (DTLS) requires a DTLS transport backend; this build supports plain CoAP/UDP only";
            midnight::g_logger->error(last_error_);
            return false;
        } else {
            // Assume host:port/path format if no scheme
            if (working_uri.find("://") != std::string::npos) {
                last_error_ = "Unsupported URI scheme";
                midnight::g_logger->error(last_error_);
                return false;
            }
        }

        // Find path separator
        size_t path_pos = working_uri.find('/');
        std::string host_port;
        if (path_pos != std::string::npos) {
            host_port = working_uri.substr(0, path_pos);
            path = working_uri.substr(path_pos);
        } else {
            host_port = working_uri;
            path = "/";
        }

        // Parse host:port
        size_t colon_pos = host_port.find(':');
        if (colon_pos != std::string::npos) {
            host = host_port.substr(0, colon_pos);
            try {
                port = static_cast<uint16_t>(std::stoi(host_port.substr(colon_pos + 1)));
            } catch (...) {
                port = DEFAULT_COAP_PORT;
            }
        } else {
            host = host_port;
        }

        if (host.empty()) {
            last_error_ = "Empty host in URI";
            midnight::g_logger->error(last_error_);
            return false;
        }

        // Ensure path starts with /
        if (path.empty() || path[0] != '/') {
            path = "/" + path;
        }

        midnight::g_logger->trace("Parsed URI: host=" + host + " port=" + std::to_string(port) +
                                  " path=" + path);
        return true;
    }

    std::vector<uint8_t> CoapClient::encode_request(RequestMethod method, const std::string &path,
                                                    uint16_t, const std::string &payload,
                                                    bool observe)
    {
        std::vector<uint8_t> message;

        // Generate message components
        uint16_t msg_id = ++next_message_id_;
        if (next_message_id_ == 0) next_message_id_ = 1;
        uint8_t token = generate_token();

        // Calculate header byte 0: version (2 bits) + type (2 bits) + token length (4 bits)
        uint8_t header0 = (COAP_VERSION << 6) | (static_cast<uint8_t>(MessageType::CON) << 4) | 1;

        // Calculate header byte 1: code (class + detail)
        // Code format: [class] [detail] where class 0 = request
        uint8_t code = static_cast<uint8_t>(method);

        // Build message header (4 bytes)
        message.push_back(header0);                         // Ver, Type, Token Length
        message.push_back(code);                             // Code
        message.push_back(static_cast<uint8_t>(msg_id >> 8)); // Message ID high
        message.push_back(static_cast<uint8_t>(msg_id & 0xFF)); // Message ID low

        // Token (1 byte for now)
        message.push_back(token);

        // Options
        std::vector<uint8_t> options;

        // Uri-Path options (split path into segments)
        std::stringstream ss(path);
        std::string segment;
        std::string clean_path = path;
        if (!clean_path.empty() && clean_path[0] == '/') {
            clean_path = clean_path.substr(1);
        }

        ss.str(clean_path);
        while (std::getline(ss, segment, '/')) {
            if (segment.empty()) continue;
            auto opt = encode_option(static_cast<uint16_t>(OptionCode::uri_path), segment);
            options.insert(options.end(), opt.begin(), opt.end());
        }

        // Observe option (if requested)
        if (observe) {
            auto opt = encode_option(static_cast<uint16_t>(OptionCode::observe), "");
            options.insert(options.end(), opt.begin(), opt.end());
        }

        // Content-Format option (if payload present)
        if (!payload.empty()) {
            auto opt = encode_option(static_cast<uint16_t>(OptionCode::content_format),
                                     std::to_string(static_cast<int>(ContentFormat::text_plain)));
            options.insert(options.end(), opt.begin(), opt.end());
        }

        // Insert options
        message.insert(message.end(), options.begin(), options.end());

        // Payload
        if (!payload.empty()) {
            message.push_back(PAYLOAD_MARKER);
            message.insert(message.end(), payload.begin(), payload.end());
        }

        return message;
    }

    std::vector<uint8_t> CoapClient::encode_option(uint16_t option_number, const std::string &value)
    {
        std::vector<uint8_t> opt;

        // Delta encoding for options
        uint16_t prev_opt = 0;
        uint16_t delta = option_number - prev_opt;

        // Calculate option delta nibble (0-15, extended if delta >= 13)
        if (delta < 13) {
            opt.push_back(static_cast<uint8_t>(delta << 4));
        } else if (delta < 269) {
            opt.push_back(static_cast<uint8_t>(13 << 4));
            opt.push_back(static_cast<uint8_t>(delta - 13));
        } else {
            opt.push_back(static_cast<uint8_t>(14 << 4));
            opt.push_back(static_cast<uint8_t>((delta - 269) >> 8));
            opt.push_back(static_cast<uint8_t>((delta - 269) & 0xFF));
        }

        // Length encoding
        size_t len = value.length();
        if (len < 13) {
            opt[0] |= static_cast<uint8_t>(len);
        } else if (len < 269) {
            opt[0] |= 13;
            opt.push_back(static_cast<uint8_t>(len - 13));
        } else {
            opt[0] |= 14;
            opt.push_back(static_cast<uint8_t>((len - 269) >> 8));
            opt.push_back(static_cast<uint8_t>((len - 269) & 0xFF));
        }

        // Option value (for string options)
        if (!value.empty()) {
            opt.insert(opt.end(), value.begin(), value.end());
        }

        return opt;
    }

    bool CoapClient::decode_response(const uint8_t *data, size_t len, std::string &out_payload)
    {
        out_payload.clear();

        if (len < COAP_HEADER_SIZE) {
            last_error_ = "Response too short";
            return false;
        }

        // Parse header
        uint8_t header0 = data[0];
        uint8_t version = (header0 >> 6) & 0x03;
        uint8_t token_len = header0 & 0x0F;
        uint8_t code = data[1];
        uint16_t msg_id = (static_cast<uint16_t>(data[2]) << 8) | data[3];

        if (version != COAP_VERSION) {
            last_error_ = "Unsupported CoAP version: " + std::to_string(version);
            midnight::g_logger->error(last_error_);
            return false;
        }

        midnight::g_logger->trace("CoAP response: code=" + std::to_string(code) +
                                  " msg_id=" + std::to_string(msg_id) +
                                  " token_len=" + std::to_string(token_len));

        // Check response code
        uint8_t response_class = code >> 5;

        if (response_class != 2) { // Not 2.x Success
            if (code == 0) {
                // Empty response (ACK) - valid for confirmable requests
                midnight::g_logger->trace("CoAP empty response (ACK)");
                return true;
            }
            last_error_ = "CoAP error response: " + std::to_string(code);
            midnight::g_logger->warn(last_error_);
            return false;
        }

        // Skip token
        size_t pos = COAP_HEADER_SIZE;
        if (token_len > 0 && pos + token_len <= len) {
            pos += token_len;
        }

        // Parse options until payload marker
        while (pos < len && data[pos] != PAYLOAD_MARKER) {
            uint8_t first_byte = data[pos++];

            // Extract delta and length
            uint8_t delta_nibble = (first_byte >> 4) & 0x0F;
            uint8_t length_nibble = first_byte & 0x0F;

            // Extended delta bytes are consumed here; this response parser does
            // not need the reconstructed option number.
            if (delta_nibble == 13) {
                if (pos >= len) break;
                ++pos;
            } else if (delta_nibble == 14) {
                if (pos + 1 >= len) break;
                pos += 2;
            }

            // Extended length
            size_t opt_len = length_nibble;
            if (length_nibble == 13) {
                if (pos >= len) break;
                opt_len = 13 + data[pos++];
            } else if (length_nibble == 14) {
                if (pos + 1 >= len) break;
                opt_len = 269 + ((static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1]);
                pos += 2;
            }

            // Skip option value
            if (pos + opt_len > len) break;
            pos += opt_len;
        }

        // Skip payload marker
        if (pos < len && data[pos] == PAYLOAD_MARKER) {
            pos++;
        }

        // Extract payload
        if (pos < len) {
            out_payload.assign(reinterpret_cast<const char*>(&data[pos]), len - pos);
        }

        return true;
    }

    bool CoapClient::send_udp_packet(const std::string &host, uint16_t port,
                                     const std::vector<uint8_t> &data)
    {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            last_error_ = "Failed to create UDP socket";
            midnight::g_logger->error(last_error_);
            return false;
        }

        // Resolve hostname
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        // Try to convert address directly first
        bool address_resolved = (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) == 1);

        // If not an IP address, try to resolve hostname
        if (!address_resolved) {
#ifdef _WIN32
            struct hostent *host_entry = gethostbyname(host.c_str());
#else
            struct hostent *host_entry = gethostbyname(host.c_str());
#endif
            if (host_entry == nullptr) {
                last_error_ = "Failed to resolve hostname: " + host;
                midnight::g_logger->error(last_error_);
#ifdef _WIN32
                closesocket(sock);
#else
                close(sock);
#endif
                return false;
            }
            memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
        }

        // Send data
        ssize_t sent = sendto(sock, reinterpret_cast<const char*>(data.data()),
                              static_cast<int>(data.size()), 0,
                              (struct sockaddr *)&server_addr, sizeof(server_addr));

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif

        if (sent < 0 || static_cast<size_t>(sent) != data.size()) {
            last_error_ = "Failed to send UDP packet";
            midnight::g_logger->error(last_error_);
            return false;
        }

        midnight::g_logger->trace("UDP packet sent: " + std::to_string(sent) + " bytes to " +
                                  host + ":" + std::to_string(port));
        return true;
    }

    bool CoapClient::receive_udp_packet(int sock, std::vector<uint8_t> &buffer, int timeout_secs)
    {
        buffer.resize(MAX_UDP_PACKET_SIZE);
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

#ifdef _WIN32
        DWORD tv = timeout_secs * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = timeout_secs;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        ssize_t received = recvfrom(sock, reinterpret_cast<char*>(buffer.data()),
                                     static_cast<int>(buffer.size()), 0,
                                     (struct sockaddr *)&from_addr, &from_len);

        if (received < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                last_error_ = "Receive timeout";
            } else {
                last_error_ = "Receive failed: " + std::to_string(err);
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                last_error_ = "Receive timeout";
            } else {
                last_error_ = "Receive failed: " + std::string(strerror(errno));
            }
#endif
            buffer.clear();
            return false;
        }

        buffer.resize(static_cast<size_t>(received));

        char from_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from_addr.sin_addr, from_ip, INET_ADDRSTRLEN);
        midnight::g_logger->trace("UDP packet received: " + std::to_string(received) +
                                  " bytes from " + std::string(from_ip));

        return true;
    }

} // namespace midnight::protocols::coap
