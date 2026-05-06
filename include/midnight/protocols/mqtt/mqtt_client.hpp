#pragma once

#include "midnight/protocols/mqtt/mqtt_connection.hpp"

#include <string>
#include <functional>
#include <memory>

namespace midnight::protocols::mqtt
{

/**
 * @brief Production MQTT 3.1.1 Client
 *
 * Full MQTT 3.1.1 protocol implementation over raw TCP sockets.
 * Supports:
 *   - QoS 0 (at most once, fire-and-forget)
 *   - QoS 1 (at least once, with acknowledgment)
 *   - QoS 2 (exactly once, 4-way handshake)
 *   - Clean/persistent sessions
 *   - Will messages
 *   - Keep-alive pings
 *   - TLS (wrap socket externally if needed)
 */
class MqttClient
{
public:
    MqttClient();
    ~MqttClient();

    /**
     * @brief Connect to MQTT broker
     * @param broker_url Hostname or IP of the broker
     * @param port TCP port (default 1883, use 8883 for TLS)
     * @param client_id Unique client identifier (auto-generated if empty)
     * @param clean_session Start fresh session (default true)
     * @param keep_alive Keep-alive interval in seconds (default 60)
     */
    bool connect(const std::string& broker_url, int port = 1883,
                 const std::string& client_id = "",
                 bool clean_session = true, int keep_alive = 60,
                 const std::string& username = "",
                 const std::string& password = "");

    void disconnect();

    /**
     * @brief Publish a message
     * @param topic MQTT topic (e.g., "sensors/temp")
     * @param payload Message body
     * @param qos QoS level (default QoS 0)
     * @param retain Retain message on broker (default false)
     */
    void publish(const std::string& topic, const std::string& payload,
                 QoS qos = QoS::AT_MOST_ONCE, bool retain = false);

    /**
     * @brief Subscribe to a topic
     * @param topic Topic pattern (supports + and # wildcards)
     * @param qos Maximum QoS for messages on this topic
     */
    void subscribe(const std::string& topic, QoS qos = QoS::AT_MOST_ONCE);

    /**
     * @brief Unsubscribe from a topic
     */
    void unsubscribe(const std::string& topic);

    bool is_connected() const;
    std::string get_last_error() const;

    void set_message_callback(MessageCallback cb);
    void set_connect_callback(ConnectCallback cb);
    void set_disconnect_callback(DisconnectCallback cb);

private:
    std::unique_ptr<MqttConnection> connection_;
    std::string client_id_;
    std::string username_;
    std::string password_;
    bool clean_session_ = true;
    int keep_alive_ = 60;
};

} // namespace midnight::protocols::mqtt
