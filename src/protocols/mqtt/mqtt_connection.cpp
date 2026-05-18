#include "midnight/protocols/mqtt/mqtt_connection.hpp"
#include "midnight/core/logger.hpp"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    #define CLOSE_SOCKET closesocket
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
    #define CLOSE_SOCKET close
#endif

#include <cstring>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
using midnight_socket_fd = SOCKET;
#else
using midnight_socket_fd = int;
#endif

namespace midnight::protocols::mqtt
{

MqttConnection::MqttConnection(const std::string& broker, int port)
    : broker_(broker)
    , port_(port)
    , state_(ConnectionState::DISCONNECTED)
{
#ifdef _WIN32
    // Initialize Winsock once globally
    if (!winsock_initialized_.load()) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
            winsock_initialized_.store(true);
        }
    }
#endif
    midnight::g_logger->debug("MqttConnection created for " + broker + ":" + std::to_string(port));
}

MqttConnection::~MqttConnection()
{
    disconnect();
}

bool MqttConnection::connect(const ConnectPayload& payload)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (state_ == ConnectionState::CONNECTED || state_ == ConnectionState::CONNECTING) {
        last_error_ = "Already connected or connecting";
        return false;
    }

    state_ = ConnectionState::CONNECTING;
    last_error_.clear();
    connected_ = false;
    keep_alive_ = std::chrono::seconds(payload.keep_alive);

    // Create socket
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) {
        last_error_ = "Failed to create socket";
        state_ = ConnectionState::FAILED;
        midnight::g_logger->error("MQTT: " + last_error_);
        return false;
    }

    // Set socket to non-blocking for timeout handling
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock_, FIONBIO, &mode);
#else
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
#endif

    // Resolve hostname using getaddrinfo
    struct addrinfo hints, *result = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port_);
    int gai_result = getaddrinfo(broker_.c_str(), port_str.c_str(), &hints, &result);
    if (gai_result != 0) {
        last_error_ = "Failed to resolve hostname: " + broker_;
        state_ = ConnectionState::FAILED;
        CLOSE_SOCKET(sock_);
        sock_ = -1;
#ifdef _WIN32
        freeaddrinfo(result);
#endif
        midnight::g_logger->error("MQTT: " + last_error_);
        return false;
    }

    // Attempt connection
    int conn_result = ::connect(sock_, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    if (conn_result < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            last_error_ = "Connection failed: " + std::to_string(err);
            state_ = ConnectionState::FAILED;
            CLOSE_SOCKET(sock_);
            sock_ = -1;
            midnight::g_logger->error("MQTT: " + last_error_);
            return false;
        }
#else
        if (errno != EINPROGRESS) {
            last_error_ = "Connection failed: " + std::string(strerror(errno));
            state_ = ConnectionState::FAILED;
            CLOSE_SOCKET(sock_);
            sock_ = -1;
            midnight::g_logger->error("MQTT: " + last_error_);
            return false;
        }
#endif

        // Wait for connection with timeout
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(static_cast<midnight_socket_fd>(sock_), &write_fds);
        struct timeval timeout = {5, 0};  // 5 second timeout

        conn_result = select(static_cast<int>(sock_) + 1, nullptr, &write_fds, nullptr, &timeout);
        if (conn_result <= 0) {
            last_error_ = "Connection timeout";
            state_ = ConnectionState::FAILED;
            CLOSE_SOCKET(sock_);
            sock_ = -1;
            midnight::g_logger->error("MQTT: " + last_error_);
            return false;
        }

        // Check actual connection success
#ifdef _WIN32
        int so_error = 0;
#else
        int so_error = 0;
#endif
        socklen_t len = sizeof(so_error);
        getsockopt(sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
        if (so_error != 0) {
            last_error_ = "Connection failed (SO_ERROR): " + std::to_string(so_error);
            state_ = ConnectionState::FAILED;
            CLOSE_SOCKET(sock_);
            sock_ = -1;
            midnight::g_logger->error("MQTT: " + last_error_);
            return false;
        }
    }

    midnight::g_logger->info("MQTT: TCP connected to " + broker_ + ":" + std::to_string(port_));

    // Build and send CONNECT packet
    auto connect_packet = build_connect_packet(payload);
    if (!send_packet(connect_packet)) {
        last_error_ = "Failed to send CONNECT packet";
        state_ = ConnectionState::FAILED;
        CLOSE_SOCKET(sock_);
        sock_ = -1;
        midnight::g_logger->error("MQTT: " + last_error_);
        return false;
    }

    midnight::g_logger->debug("MQTT: CONNECT packet sent");

    // Start reader thread
    running_.store(true);
    closing_.store(false);
    reader_thread_ = std::thread(&MqttConnection::reader_loop, this);

    // Wait for CONNACK (with timeout)
    {
        std::unique_lock<std::mutex> cv_lock(connect_mutex_);
        if (!connect_cv_.wait_for(cv_lock, std::chrono::seconds(10), [this] { return connected_; })) {
            last_error_ = "Connection timeout waiting for CONNACK";
            state_ = ConnectionState::FAILED;
            disconnect();
            midnight::g_logger->error("MQTT: " + last_error_);
            return false;
        }
    }

    if (!connected_) {
        last_error_ = "Connection rejected by broker";
        state_ = ConnectionState::FAILED;
        midnight::g_logger->error("MQTT: " + last_error_);
        return false;
    }

    state_ = ConnectionState::CONNECTED;
    midnight::g_logger->info("MQTT: Connected successfully");
    return true;
}

void MqttConnection::disconnect()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (closing_.load()) {
        return;
    }
    closing_.store(true);

    if (state_ == ConnectionState::CONNECTED) {
        // Send DISCONNECT packet
        auto disc_packet = build_disconnect_packet();
        send_packet(disc_packet);
    }

    running_.store(false);

    // Close socket
    if (sock_ >= 0) {
        CLOSE_SOCKET(sock_);
        sock_ = -1;
    }

    // Join reader thread
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    // Cleanup pending operations
    cleanup();

    state_ = ConnectionState::DISCONNECTED;
    midnight::g_logger->info("MQTT: Disconnected");
}

bool MqttConnection::is_connected() const
{
    return state_ == ConnectionState::CONNECTED;
}

bool MqttConnection::publish(const std::string& topic, const std::string& payload, bool retain)
{
    PublishPacket pub;
    pub.topic = topic;
    pub.payload = payload;
    pub.qos = QoS::AT_MOST_ONCE;
    pub.retain = retain;

    auto packet = build_publish_packet(pub);
    return send_packet(packet);
}

bool MqttConnection::publish_qos1(const std::string& topic, const std::string& payload, bool retain)
{
    uint16_t packet_id = next_packet_id();

    PublishPacket pub;
    pub.topic = topic;
    pub.payload = payload;
    pub.packet_id = packet_id;
    pub.qos = QoS::AT_LEAST_ONCE;
    pub.retain = retain;

    auto packet = build_publish_packet(pub);
    if (!send_packet(packet)) {
        return false;
    }

    // Wait for PUBACK with timeout
    auto pending = std::make_shared<PendingPuback>();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_pubacks_[packet_id] = pending;
    }

    bool success = false;
    {
        std::unique_lock<std::mutex> lock(pending_mutex_);
        success = pending->cv.wait_for(lock, std::chrono::seconds(30), [&pending] {
            return pending->completed;
        });
    }

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_pubacks_.erase(packet_id);
    }

    return success && pending->success;
}

bool MqttConnection::subscribe(const std::string& topic, QoS qos)
{
    uint16_t packet_id = next_packet_id();

    SubscribeTopic t;
    t.topic = topic;
    t.qos = qos;

    auto packet = build_subscribe_packet(packet_id, {t});
    return send_packet(packet);
}

bool MqttConnection::unsubscribe(const std::string& topic)
{
    uint16_t packet_id = next_packet_id();

    auto packet = build_unsubscribe_packet(packet_id, {topic});
    return send_packet(packet);
}

void MqttConnection::reader_loop()
{
    midnight::g_logger->debug("MQTT: Reader loop started");

    while (running_.load() && !closing_.load()) {
        std::vector<uint8_t> data;
        if (!receive_packet(data)) {
            if (running_.load() && !closing_.load()) {
                midnight::g_logger->warn("MQTT: Socket read failed");
            }
            break;
        }

        if (data.empty()) {
            continue;
        }

        auto pkt = parse_packet(data.data(), data.size());
        if (!pkt.error.empty()) {
            midnight::g_logger->error("MQTT: Parse error: " + pkt.error);
            continue;
        }

        handle_packet(pkt);

        // Update last ping time on any received packet
        last_ping_ = std::chrono::steady_clock::now();

        // Check keep-alive
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_).count();
        if (elapsed > keep_alive_.count() * 2) {
            midnight::g_logger->warn("MQTT: Keep-alive timeout, sending PINGREQ");
            auto ping = build_pingreq_packet();
            send_packet(ping);
            last_ping_ = now;
        }
    }

    midnight::g_logger->debug("MQTT: Reader loop ended");

    // Connection lost - cleanup
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (sock_ >= 0) {
            CLOSE_SOCKET(sock_);
            sock_ = -1;
        }
    }

    cleanup();

    if (state_ == ConnectionState::CONNECTED) {
        state_ = ConnectionState::DISCONNECTED;
        if (disconnect_cb_) {
            disconnect_cb_();
        }
    }
}

void MqttConnection::handle_packet(const ParsedPacket& pkt)
{
    switch (pkt.type) {
        case PacketType::CONNACK:
            handle_connack(pkt);
            break;
        case PacketType::PUBLISH:
            handle_publish(pkt);
            break;
        case PacketType::PUBACK:
            handle_puback(pkt);
            break;
        case PacketType::PUBREC:
            handle_pubrec(pkt);
            break;
        case PacketType::PUBREL:
            handle_pubrel(pkt);
            break;
        case PacketType::PUBCOMP:
            handle_pubcomp(pkt);
            break;
        case PacketType::SUBACK:
            handle_suback(pkt);
            break;
        case PacketType::PINGRESP:
            handle_pingresp(pkt);
            break;
        default:
            midnight::g_logger->warn("MQTT: Unhandled packet type: " + std::to_string(static_cast<int>(pkt.type)));
            break;
    }
}

void MqttConnection::handle_connack(const ParsedPacket& pkt)
{
    auto data = parse_connack(pkt.payload.data(), pkt.payload.size());
    session_present_ = data.session_present;
    last_return_code_ = data.code;

    if (data.code == ConnackReturnCode::ACCEPTED) {
        midnight::g_logger->debug("MQTT: CONNACK received - connection accepted");
        {
            std::lock_guard<std::mutex> lock(connect_mutex_);
            connected_ = true;
        }
        connect_cv_.notify_one();
        if (connect_cb_) {
            connect_cb_(true, "");
        }
    } else {
        std::string error = "Connection rejected: return code " + std::to_string(static_cast<int>(data.code));
        midnight::g_logger->error("MQTT: " + error);
        {
            std::lock_guard<std::mutex> lock(connect_mutex_);
            connected_ = false;
        }
        connect_cv_.notify_one();
        if (connect_cb_) {
            connect_cb_(false, error);
        }
    }
}

void MqttConnection::handle_publish(const ParsedPacket& pkt)
{
    // First byte of payload contains fixed header info for PUBLISH
    // But in our parse_publish, we need the original header byte
    // Since pkt only has payload, we reconstruct from parsed data

    auto data = parse_publish(&pkt.flags, pkt.payload.data(), pkt.payload.size());

    midnight::g_logger->debug("MQTT: PUBLISH received - topic='" + data.topic +
                              "' qos=" + std::to_string(static_cast<int>(data.qos)) +
                              " size=" + std::to_string(data.payload.size()));

    // Deliver to callback
    if (msg_cb_) {
        msg_cb_(data.topic, data.payload);
    }

    // Respond based on QoS
    if (data.qos == QoS::AT_LEAST_ONCE) {
        // Send PUBACK
        auto puback = build_puback_packet(data.packet_id);
        send_packet(puback);
    } else if (data.qos == QoS::EXACTLY_ONCE) {
        // Send PUBREC, store state
        auto pubrec = build_pubrec_packet(data.packet_id);
        send_packet(pubrec);

        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_qos2_[data.packet_id] = QoS2State::PUBREC_RECEIVED;
    }
}

void MqttConnection::handle_puback(const ParsedPacket& pkt)
{
    auto data = parse_puback(pkt.payload.data(), pkt.payload.size());
    midnight::g_logger->debug("MQTT: PUBACK received - packet_id=" + std::to_string(data.packet_id));

    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_pubacks_.find(data.packet_id);
    if (it != pending_pubacks_.end()) {
        it->second->success = true;
        it->second->completed = true;
        it->second->cv.notify_one();
    }
}

void MqttConnection::handle_pubrec(const ParsedPacket& pkt)
{
    auto data = parse_puback(pkt.payload.data(), pkt.payload.size());
    midnight::g_logger->debug("MQTT: PUBREC received - packet_id=" + std::to_string(data.packet_id));

    // Send PUBREL
    auto pubrel = build_pubrel_packet(data.packet_id);
    send_packet(pubrel);

    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_qos2_[data.packet_id] = QoS2State::PUBREL_SENT;
}

void MqttConnection::handle_pubrel(const ParsedPacket& pkt)
{
    auto data = parse_puback(pkt.payload.data(), pkt.payload.size());
    midnight::g_logger->debug("MQTT: PUBREL received - packet_id=" + std::to_string(data.packet_id));

    // Send PUBCOMP
    auto pubcomp = build_pubcomp_packet(data.packet_id);
    send_packet(pubcomp);

    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_qos2_.erase(data.packet_id);
}

void MqttConnection::handle_pubcomp(const ParsedPacket& pkt)
{
    auto data = parse_puback(pkt.payload.data(), pkt.payload.size());
    midnight::g_logger->debug("MQTT: PUBCOMP received - packet_id=" + std::to_string(data.packet_id));

    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_qos2_.erase(data.packet_id);

    auto it = pending_pubacks_.find(data.packet_id);
    if (it != pending_pubacks_.end()) {
        it->second->success = true;
        it->second->completed = true;
        it->second->cv.notify_one();
    }
}

void MqttConnection::handle_suback(const ParsedPacket& pkt)
{
    auto data = parse_suback(pkt.payload.data(), pkt.payload.size());
    midnight::g_logger->debug("MQTT: SUBACK received - packet_id=" + std::to_string(data.packet_id) +
                              " return_codes=" + std::to_string(data.return_codes.size()));
}

void MqttConnection::handle_pingresp(const ParsedPacket&)
{
    midnight::g_logger->debug("MQTT: PINGRESP received");
    last_ping_ = std::chrono::steady_clock::now();
}

bool MqttConnection::send_packet(const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (sock_ < 0) {
        last_error_ = "Socket not connected";
        return false;
    }

    ssize_t sent = ::send(sock_, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
    if (sent < 0) {
#ifdef _WIN32
        last_error_ = "Send failed: " + std::to_string(WSAGetLastError());
#else
        last_error_ = "Send failed: " + std::string(strerror(errno));
#endif
        midnight::g_logger->error("MQTT: " + last_error_);
        return false;
    }

    return true;
}

bool MqttConnection::receive_packet(std::vector<uint8_t>& out)
{
    out.clear();

    // Read fixed header (at least 2 bytes)
    uint8_t header[4];
    ssize_t n = recv(sock_, reinterpret_cast<char*>(header), 1, 0);
    if (n <= 0) {
        return false;
    }

    // Decode remaining length
    uint32_t remaining_len = 0;
    uint8_t multiplier = 1;
    uint8_t byte;
    size_t rl_bytes = 0;

    do {
        n = recv(sock_, reinterpret_cast<char*>(&byte), 1, 0);
        if (n <= 0) return false;

        header[1 + rl_bytes] = byte;
        rl_bytes++;

        remaining_len += (byte & 0x7F) * multiplier;
        multiplier *= 128;

        if (rl_bytes > 4) return false;  // Invalid encoding
    } while ((byte & 0x80) != 0);

    // Read payload
    out.assign(header, header + 1 + rl_bytes);

    if (remaining_len > 0) {
        std::vector<uint8_t> payload(remaining_len);
        size_t total_read = 0;

        while (total_read < remaining_len) {
            n = recv(sock_, reinterpret_cast<char*>(payload.data() + total_read),
                     static_cast<int>(remaining_len - total_read), 0);
            if (n <= 0) {
                return false;
            }
            total_read += static_cast<size_t>(n);
        }

        out.insert(out.end(), payload.begin(), payload.end());
    }

    return true;
}

uint16_t MqttConnection::next_packet_id()
{
    std::lock_guard<std::mutex> lock(pid_mutex_);
    uint16_t id = next_packet_id_.fetch_add(1);
    if (id == 0) {  // Wrap-around protection
        id = next_packet_id_.fetch_add(1);
    }
    return id;
}

void MqttConnection::cleanup()
{
    std::lock_guard<std::mutex> lock(pending_mutex_);

    // Complete all pending QoS 1 publishes with failure
    for (auto& pair : pending_pubacks_) {
        pair.second->success = false;
        pair.second->completed = true;
        pair.second->cv.notify_one();
    }
    pending_pubacks_.clear();
    pending_qos2_.clear();
}

} // namespace midnight::protocols::mqtt
