#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace midnight::protocols::mqtt
{

// MQTT Control Packet Types (byte 1, upper nibble)
enum class PacketType : uint8_t
{
    CONNECT     = 1,
    CONNACK     = 2,
    PUBLISH     = 3,
    PUBACK      = 4,
    PUBREC      = 5,
    PUBREL      = 6,
    PUBCOMP     = 7,
    SUBSCRIBE   = 8,
    SUBACK      = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK    = 11,
    PINGREQ     = 12,
    PINGRESP    = 13,
    DISCONNECT  = 14,
    AUTH        = 15
};

// MQTT 3.1.1 Connect flags
enum class ConnectFlags : uint8_t
{
    CLEAN_SESSION  = 0x02,
    WILL_FLAG      = 0x04,
    WILL_QOS_0    = 0x00,
    WILL_QOS_1    = 0x08,
    WILL_QOS_2    = 0x10,
    WILL_RETAIN    = 0x20,
    PASSWORD       = 0x40,
    USERNAME       = 0x80
};

// MQTT 3.1.1 Connect Return Codes
enum class ConnackReturnCode : uint8_t
{
    ACCEPTED              = 0,
    UNACCEPTABLE_PROTOCOL = 1,
    IDENTIFIER_REJECTED   = 2,
    SERVER_UNAVAILABLE    = 3,
    BAD_CREDENTIALS       = 4,
    NOT_AUTHORIZED        = 5
};

// QoS levels
enum class QoS : uint8_t
{
    AT_MOST_ONCE  = 0,  // QoS 0
    AT_LEAST_ONCE = 1,  // QoS 1
    EXACTLY_ONCE  = 2   // QoS 2
};

// Connect packet payload
struct ConnectPayload
{
    std::string client_id;
    std::string will_topic;
    std::string will_message;
    std::string username;
    std::string password;
    uint16_t keep_alive = 60;
    bool clean_session = true;
    bool will_retain = false;
    QoS will_qos = QoS::AT_MOST_ONCE;
};

// Publish packet info
struct PublishPacket
{
    std::string topic;
    std::string payload;
    uint16_t packet_id = 0;
    QoS qos = QoS::AT_MOST_ONCE;
    bool dup = false;
    bool retain = false;
};

// Subscribe topic entry
struct SubscribeTopic
{
    std::string topic;
    QoS qos = QoS::AT_MOST_ONCE;
};

// Helper: encode/decode MQTT variable header integers
uint32_t encode_remaining_length(size_t length);
size_t decode_remaining_length(const uint8_t* data, size_t max_len, uint32_t& out_len);

// Helper: encode/decode UTF-8 strings
void encode_utf8_string(std::vector<uint8_t>& out, const std::string& str);
std::string decode_utf8_string(const uint8_t*& data, size_t& remaining);

// Helper: encode/decode 2-byte big-endian
uint16_t decode_be16(const uint8_t* data);
void encode_be16(std::vector<uint8_t>& out, uint16_t value);

// Build MQTT packets
std::vector<uint8_t> build_connect_packet(const ConnectPayload& payload);
std::vector<uint8_t> build_publish_packet(const PublishPacket& pub);
std::vector<uint8_t> build_puback_packet(uint16_t packet_id);
std::vector<uint8_t> build_pubrec_packet(uint16_t packet_id);
std::vector<uint8_t> build_pubrel_packet(uint16_t packet_id);
std::vector<uint8_t> build_pubcomp_packet(uint16_t packet_id);
std::vector<uint8_t> build_subscribe_packet(uint16_t packet_id, const std::vector<SubscribeTopic>& topics);
std::vector<uint8_t> build_unsubscribe_packet(uint16_t packet_id, const std::vector<std::string>& topics);
std::vector<uint8_t> build_pingreq_packet();
std::vector<uint8_t> build_disconnect_packet();

// Parse MQTT packets
struct ParsedPacket
{
    PacketType type;
    uint8_t flags;
    std::vector<uint8_t> payload;
    std::string error;
};
ParsedPacket parse_packet(const uint8_t* data, size_t size);

// Parse specific packet types
struct ConnackData { bool session_present; ConnackReturnCode code; };
ConnackData parse_connack(const uint8_t* payload, size_t size);

struct PubackData { uint16_t packet_id; };
PubackData parse_puback(const uint8_t* payload, size_t size);

struct SubackData { uint16_t packet_id; std::vector<uint8_t> return_codes; };
SubackData parse_suback(const uint8_t* payload, size_t size);

struct PublishData { std::string topic; std::string payload; uint16_t packet_id; QoS qos; bool dup; bool retain; };
PublishData parse_publish(const uint8_t* header, const uint8_t* payload, size_t size);

} // namespace midnight::protocols::mqtt
