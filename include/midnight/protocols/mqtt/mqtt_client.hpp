#pragma once

#include <string>
#include <functional>
#include <memory>

namespace midnight::protocols::mqtt
{

    using MessageCallback = std::function<void(const std::string &topic, const std::string &payload)>;
    using ConnectCallback = std::function<void(bool success)>;

    /**
     * @brief MQTT Client for IoT communication
     */
    class MqttClient
    {
    public:
        /**
         * @brief Connect to MQTT broker
         */
        void connect(const std::string &broker_url, int port = 1883);

        /**
         * @brief Disconnect from broker
         */
        void disconnect();

        /**
         * @brief Publish message to topic
         */
        void publish(const std::string &topic, const std::string &payload, int qos = 0);

        /**
         * @brief Subscribe to topic
         */
        void subscribe(const std::string &topic, MessageCallback callback);

        /**
         * @brief Check if connected
         */
        bool is_connected() const;

        /**
         * @brief Set connection callback
         */
        void set_connect_callback(ConnectCallback callback);

    private:
        bool connected_ = false;
        std::string broker_url_;
        int port_;
    };

} // namespace midnight::protocols::mqtt
