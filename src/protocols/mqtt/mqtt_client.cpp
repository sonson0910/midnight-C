#include "midnight/protocols/mqtt/mqtt_client.hpp"
#include "midnight/core/logger.hpp"

#include <chrono>

namespace midnight::protocols::mqtt
{

MqttClient::MqttClient() = default;

MqttClient::~MqttClient() = default;

bool MqttClient::connect(const std::string& broker_url, int port,
                         const std::string& client_id,
                         bool clean_session, int keep_alive,
                         const std::string& username,
                         const std::string& password)
{
    if (is_connected())
        disconnect();

    connection_ = std::make_unique<MqttConnection>(broker_url, port);

    ConnectPayload payload;
    payload.client_id = client_id.empty()
        ? "midnight-mqtt-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
        : client_id;
    payload.clean_session = clean_session;
    payload.keep_alive = static_cast<uint16_t>(keep_alive);
    payload.username = username;
    payload.password = password;
    client_id_ = payload.client_id;
    clean_session_ = clean_session;
    keep_alive_ = keep_alive;
    username_ = username;
    password_ = password;

    bool result = connection_->connect(payload);
    if (!result) {
        midnight::g_logger->error("MQTT connect failed: " + connection_->last_error());
    }
    return result;
}

void MqttClient::disconnect()
{
    if (connection_) {
        connection_->disconnect();
        connection_.reset();
    }
}

bool MqttClient::is_connected() const
{
    return connection_ && connection_->is_connected();
}

std::string MqttClient::get_last_error() const
{
    return connection_ ? connection_->last_error() : "";
}

void MqttClient::publish(const std::string& topic, const std::string& payload, QoS qos, bool retain)
{
    if (!connection_ || !connection_->is_connected()) {
        midnight::g_logger->warn("MQTT publish: not connected");
        return;
    }
    if (qos == QoS::AT_MOST_ONCE)
        connection_->publish(topic, payload, retain);
    else
        connection_->publish_qos1(topic, payload, retain);
}

void MqttClient::subscribe(const std::string& topic, QoS qos)
{
    if (connection_) connection_->subscribe(topic, qos);
}

void MqttClient::unsubscribe(const std::string& topic)
{
    if (connection_) connection_->unsubscribe(topic);
}

void MqttClient::set_message_callback(MessageCallback cb)
{
    if (connection_) connection_->set_message_callback(std::move(cb));
}

void MqttClient::set_connect_callback(ConnectCallback cb)
{
    if (connection_) connection_->set_connect_callback(std::move(cb));
}

void MqttClient::set_disconnect_callback(DisconnectCallback cb)
{
    if (connection_) connection_->set_disconnect_callback(std::move(cb));
}

} // namespace midnight::protocols::mqtt
