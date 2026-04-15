#include "midnight/protocols/mqtt/mqtt_client.hpp"
#include "midnight/core/logger.hpp"
#include <iostream>

int main()
{
    midnight::g_logger->info("MQTT Example Started");

    midnight::protocols::mqtt::MqttClient mqtt_client;

    // Connect to broker
    mqtt_client.connect("mqtt.example.com", 1883);

    if (mqtt_client.is_connected())
    {
        midnight::g_logger->info("Connected to MQTT broker");

        // Publish message
        mqtt_client.publish("midnight/sensor/temperature", "25.5", 1);

        // Subscribe to topic
        mqtt_client.subscribe("midnight/control/commands",
                              [](const std::string &topic, const std::string &payload)
                              {
                                  std::cout << "Received on " << topic << ": " << payload << std::endl;
                              });
    }
    else
    {
        midnight::g_logger->error("Failed to connect to MQTT broker");
    }

    midnight::g_logger->info("MQTT Example Finished");
    return 0;
}
