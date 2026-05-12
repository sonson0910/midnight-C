#pragma once

#include "midnight/protocols/mqtt/mqtt_protocol.hpp"

#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace midnight::protocols::mqtt
{

using MessageCallback = std::function<void(const std::string& topic, const std::string& payload)>;
using ConnectCallback = std::function<void(bool success, const std::string& error)>;
using DisconnectCallback = std::function<void()>;

// Connection state machine
enum class ConnectionState
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    FAILED
};

class MqttConnection
{
public:
    MqttConnection(const std::string& broker, int port);
    ~MqttConnection();

    bool connect(const ConnectPayload& payload);
    void disconnect();
    bool is_connected() const;

    // Send QoS 0 publish
    bool publish(const std::string& topic, const std::string& payload, bool retain = false);
    // Send publish and wait for QoS ack
    bool publish_qos1(const std::string& topic, const std::string& payload, bool retain = false);

    // Subscribe (QoS 0 or 1)
    bool subscribe(const std::string& topic, QoS qos = QoS::AT_MOST_ONCE);
    bool unsubscribe(const std::string& topic);

    // Callbacks
    void set_message_callback(MessageCallback cb) { msg_cb_ = std::move(cb); }
    void set_connect_callback(ConnectCallback cb) { connect_cb_ = std::move(cb); }
    void set_disconnect_callback(DisconnectCallback cb) { disconnect_cb_ = std::move(cb); }

    // Last error
    std::string last_error() const { return last_error_; }

private:
    void reader_loop();
    void handle_packet(const ParsedPacket& pkt);
    void handle_connack(const ParsedPacket& pkt);
    void handle_publish(const ParsedPacket& pkt);
    void handle_puback(const ParsedPacket& pkt);
    void handle_pubrec(const ParsedPacket& pkt);
    void handle_pubrel(const ParsedPacket& pkt);
    void handle_pubcomp(const ParsedPacket& pkt);
    void handle_suback(const ParsedPacket& pkt);
    void handle_pingresp(const ParsedPacket& pkt);

    bool send_packet(const std::vector<uint8_t>& data);
    bool receive_packet(std::vector<uint8_t>& out);

    uint16_t next_packet_id();
    void cleanup();

    // Pending QoS 1 publishes waiting for PUBACK: packet_id -> future
    struct PendingPuback {
        std::condition_variable cv;
        bool completed = false;
        bool success = false;
    };
    std::map<uint16_t, std::shared_ptr<PendingPuback>> pending_pubacks_;
    std::mutex pending_mutex_;

    // Pending QoS 2: packet_id -> state
    enum class QoS2State {
        PUBREC_RECEIVED,
        PUBREL_SENT,
        PUBCOMP_RECEIVED
    };
    std::map<uint16_t, QoS2State> pending_qos2_;

    // Socket
    int sock_ = -1;
    std::atomic<bool> closing_{false};

    std::string broker_;
    int port_ = 1883;
    std::string last_error_;
    ConnectionState state_ = ConnectionState::DISCONNECTED;

    std::thread reader_thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex socket_mutex_;

    // Packet ID counter (atomic for thread safety)
    std::atomic<uint16_t> next_packet_id_{1};
    std::mutex pid_mutex_;

    MessageCallback msg_cb_;
    ConnectCallback connect_cb_;
    DisconnectCallback disconnect_cb_;

    std::chrono::steady_clock::time_point last_ping_;
    std::chrono::seconds keep_alive_{60};

    bool session_present_ = false;
    ConnackReturnCode last_return_code_ = ConnackReturnCode::ACCEPTED;

    // Condition variable for connect wait
    std::condition_variable connect_cv_;
    std::mutex connect_mutex_;
    bool connected_ = false;

#ifdef _WIN32
    inline static std::atomic<bool> winsock_initialized_{false};
#endif
};

} // namespace midnight::protocols::mqtt
