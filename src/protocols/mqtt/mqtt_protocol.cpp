#include "midnight/protocols/mqtt/mqtt_protocol.hpp"

namespace midnight::protocols::mqtt
{

// ============================================================================
// Variable Length Encoding (MQTT Spec Section 2.1.3)
// ============================================================================

uint32_t encode_remaining_length(size_t length)
{
    uint32_t encoded = 0;
    uint8_t byte;
    size_t remaining = length;

    do {
        byte = static_cast<uint8_t>(remaining % 128);
        remaining = remaining / 128;
        if (remaining > 0) {
            byte |= 0x80;  // Set continuation bit
        }
        encoded = (encoded << 8) | byte;
    } while (remaining > 0);

    return encoded;
}

size_t decode_remaining_length(const uint8_t* data, size_t max_len, uint32_t& out_len)
{
    out_len = 0;
    size_t bytes_used = 0;
    uint32_t multiplier = 1;

    for (size_t i = 0; i < max_len && i < 4; ++i) {
        uint8_t byte = data[i];
        out_len += (byte & 0x7F) * multiplier;
        bytes_used++;

        if ((byte & 0x80) == 0) {
            break;  // No continuation bit
        }

        multiplier *= 128;
    }

    return bytes_used;
}

// ============================================================================
// UTF-8 String Encoding/Decoding (MQTT Spec Section 1.5.4)
// ============================================================================

void encode_utf8_string(std::vector<uint8_t>& out, const std::string& str)
{
    // Encode 2-byte big-endian length
    uint16_t len = static_cast<uint16_t>(str.size());
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(len & 0xFF));

    // Encode UTF-8 bytes
    for (unsigned char c : str) {
        out.push_back(c);
    }
}

std::string decode_utf8_string(const uint8_t*& data, size_t& remaining)
{
    if (remaining < 2) {
        return "";
    }

    // Decode 2-byte big-endian length
    uint16_t len = static_cast<uint16_t>(data[0]) << 8 | data[1];
    data += 2;
    remaining -= 2;

    if (len > remaining) {
        return "";
    }

    std::string result(reinterpret_cast<const char*>(data), len);
    data += len;
    remaining -= len;

    return result;
}

// ============================================================================
// 2-Byte Big-Endian Encoding
// ============================================================================

uint16_t decode_be16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) << 8 | data[1];
}

void encode_be16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

// ============================================================================
// Build MQTT Packets
// ============================================================================

std::vector<uint8_t> build_connect_packet(const ConnectPayload& payload)
{
    std::vector<uint8_t> packet;

    // Calculate variable header and payload size first
    // Variable header:
    //   Protocol name: "MQTT" (4 bytes) + length (2 bytes) = 6
    //   Protocol level: 1 byte
    //   Connect flags: 1 byte
    //   Keep alive: 2 bytes
    // Total variable header = 10 bytes

    // Payload:
    //   Client ID: 2-byte length + bytes
    //   Will topic: 2-byte length + bytes (if will flag set)
    //   Will message: 2-byte length + bytes (if will flag set)
    //   Username: 2-byte length + bytes (if username flag set)
    //   Password: 2-byte length + bytes (if password flag set)

    size_t payload_size = 0;
    size_t client_id_len = payload.client_id.size();
    payload_size += 2 + client_id_len;

    bool has_will = !payload.will_topic.empty();
    if (has_will) {
        payload_size += 2 + payload.will_topic.size();
        payload_size += 2 + payload.will_message.size();
    }

    bool has_username = !payload.username.empty();
    bool has_password = !payload.password.empty();

    if (has_username) {
        payload_size += 2 + payload.username.size();
    }
    if (has_password) {
        payload_size += 2 + payload.password.size();
    }

    size_t variable_header_size = 10;  // Protocol name + level + flags + keepalive
    size_t remaining_length = variable_header_size + payload_size;

    // Encode remaining length (1-4 bytes)
    uint32_t rl_encoded = encode_remaining_length(remaining_length);
    size_t rl_bytes = 0;
    if (rl_encoded > 0xFFFFFF) rl_bytes = 4;
    else if (rl_encoded > 0xFFFF) rl_bytes = 3;
    else if (rl_encoded > 0x7F) rl_bytes = 2;
    else rl_bytes = 1;

    // Build fixed header: packet type (1 byte) + remaining length (1-4 bytes)
    packet.push_back(static_cast<uint8_t>(PacketType::CONNECT) << 4);

    // Encode remaining length bytes
    uint32_t rl = remaining_length;
    do {
        uint8_t byte = static_cast<uint8_t>(rl % 128);
        rl = rl / 128;
        if (rl > 0) {
            byte |= 0x80;
        }
        packet.push_back(byte);
    } while (rl > 0);

    // Variable header
    // Protocol name "MQTT"
    packet.push_back(0x00);  // Length MSB
    packet.push_back(0x04);  // Length LSB
    packet.push_back('M');
    packet.push_back('Q');
    packet.push_back('T');
    packet.push_back('T');

    // Protocol level 4 (MQTT 3.1.1)
    packet.push_back(0x04);

    // Connect flags
    uint8_t connect_flags = 0;
    if (payload.clean_session) {
        connect_flags |= static_cast<uint8_t>(ConnectFlags::CLEAN_SESSION);
    }
    if (has_will) {
        connect_flags |= static_cast<uint8_t>(ConnectFlags::WILL_FLAG);
        connect_flags |= static_cast<uint8_t>(payload.will_qos) << 3;
        if (payload.will_retain) {
            connect_flags |= static_cast<uint8_t>(ConnectFlags::WILL_RETAIN);
        }
    }
    if (has_username) {
        connect_flags |= static_cast<uint8_t>(ConnectFlags::USERNAME);
    }
    if (has_password) {
        connect_flags |= static_cast<uint8_t>(ConnectFlags::PASSWORD);
    }
    packet.push_back(connect_flags);

    // Keep alive (2 bytes, big-endian)
    packet.push_back(static_cast<uint8_t>(payload.keep_alive >> 8));
    packet.push_back(static_cast<uint8_t>(payload.keep_alive & 0xFF));

    // Payload
    // Client ID
    encode_utf8_string(packet, payload.client_id);

    // Will topic and message
    if (has_will) {
        encode_utf8_string(packet, payload.will_topic);
        encode_utf8_string(packet, payload.will_message);
    }

    // Username
    if (has_username) {
        encode_utf8_string(packet, payload.username);
    }

    // Password
    if (has_password) {
        encode_utf8_string(packet, payload.password);
    }

    return packet;
}

std::vector<uint8_t> build_publish_packet(const PublishPacket& pub)
{
    std::vector<uint8_t> packet;

    // Calculate remaining length
    // Variable header: topic (2+len) + packet_id (2, if QoS > 0)
    // Payload: message bytes

    size_t variable_header_size = 2 + pub.topic.size();
    if (pub.qos != QoS::AT_MOST_ONCE) {
        variable_header_size += 2;  // Packet ID
    }

    size_t remaining_length = variable_header_size + pub.payload.size();

    // Fixed header: packet type + flags + remaining length
    uint8_t fixed_byte = static_cast<uint8_t>(PacketType::PUBLISH) << 4;
    if (pub.dup) fixed_byte |= 0x08;
    if (pub.retain) fixed_byte |= 0x01;
    fixed_byte |= (static_cast<uint8_t>(pub.qos) << 1);
    packet.push_back(fixed_byte);

    // Encode remaining length
    uint32_t rl = remaining_length;
    do {
        uint8_t byte = static_cast<uint8_t>(rl % 128);
        rl = rl / 128;
        if (rl > 0) {
            byte |= 0x80;
        }
        packet.push_back(byte);
    } while (rl > 0);

    // Variable header: topic + packet ID
    encode_utf8_string(packet, pub.topic);

    if (pub.qos != QoS::AT_MOST_ONCE) {
        encode_be16(packet, pub.packet_id);
    }

    // Payload
    for (unsigned char c : pub.payload) {
        packet.push_back(c);
    }

    return packet;
}

std::vector<uint8_t> build_puback_packet(uint16_t packet_id)
{
    std::vector<uint8_t> packet;

    // Fixed header
    packet.push_back(static_cast<uint8_t>(PacketType::PUBACK) << 4);
    packet.push_back(0x02);  // Remaining length = 2 (just packet ID)

    // Variable header: packet ID
    encode_be16(packet, packet_id);

    return packet;
}

std::vector<uint8_t> build_pubrec_packet(uint16_t packet_id)
{
    std::vector<uint8_t> packet;

    // Fixed header
    packet.push_back(static_cast<uint8_t>(PacketType::PUBREC) << 4);
    packet.push_back(0x02);  // Remaining length = 2 (just packet ID)

    // Variable header: packet ID
    encode_be16(packet, packet_id);

    return packet;
}

std::vector<uint8_t> build_pubrel_packet(uint16_t packet_id)
{
    std::vector<uint8_t> packet;

    // Fixed header (PUBREL uses QoS 1 flags)
    packet.push_back((static_cast<uint8_t>(PacketType::PUBREL) << 4) | 0x02);
    packet.push_back(0x02);  // Remaining length = 2

    // Variable header: packet ID
    encode_be16(packet, packet_id);

    return packet;
}

std::vector<uint8_t> build_pubcomp_packet(uint16_t packet_id)
{
    std::vector<uint8_t> packet;

    // Fixed header
    packet.push_back(static_cast<uint8_t>(PacketType::PUBCOMP) << 4);
    packet.push_back(0x02);  // Remaining length = 2

    // Variable header: packet ID
    encode_be16(packet, packet_id);

    return packet;
}

std::vector<uint8_t> build_subscribe_packet(uint16_t packet_id, const std::vector<SubscribeTopic>& topics)
{
    std::vector<uint8_t> packet;

    // Calculate remaining length
    // Variable header: packet ID (2 bytes)
    // Payload: topic (2+len) + QoS (1 byte) for each topic
    size_t payload_size = 0;
    for (const auto& t : topics) {
        payload_size += 2 + t.topic.size() + 1;
    }

    size_t remaining_length = 2 + payload_size;

    // Fixed header
    packet.push_back((static_cast<uint8_t>(PacketType::SUBSCRIBE) << 4) | 0x02);  // QoS 1 required for SUBSCRIBE

    // Encode remaining length
    uint32_t rl = remaining_length;
    do {
        uint8_t byte = static_cast<uint8_t>(rl % 128);
        rl = rl / 128;
        if (rl > 0) {
            byte |= 0x80;
        }
        packet.push_back(byte);
    } while (rl > 0);

    // Variable header: packet ID
    encode_be16(packet, packet_id);

    // Payload: topics with QoS
    for (const auto& t : topics) {
        encode_utf8_string(packet, t.topic);
        packet.push_back(static_cast<uint8_t>(t.qos));
    }

    return packet;
}

std::vector<uint8_t> build_unsubscribe_packet(uint16_t packet_id, const std::vector<std::string>& topics)
{
    std::vector<uint8_t> packet;

    // Calculate remaining length
    size_t payload_size = 0;
    for (const auto& t : topics) {
        payload_size += 2 + t.size();
    }

    size_t remaining_length = 2 + payload_size;

    // Fixed header
    packet.push_back((static_cast<uint8_t>(PacketType::UNSUBSCRIBE) << 4) | 0x02);  // QoS 1 required

    // Encode remaining length
    uint32_t rl = remaining_length;
    do {
        uint8_t byte = static_cast<uint8_t>(rl % 128);
        rl = rl / 128;
        if (rl > 0) {
            byte |= 0x80;
        }
        packet.push_back(byte);
    } while (rl > 0);

    // Variable header: packet ID
    encode_be16(packet, packet_id);

    // Payload: topics
    for (const auto& t : topics) {
        encode_utf8_string(packet, t);
    }

    return packet;
}

std::vector<uint8_t> build_pingreq_packet()
{
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(PacketType::PINGREQ) << 4);
    packet.push_back(0x00);  // No remaining length
    return packet;
}

std::vector<uint8_t> build_disconnect_packet()
{
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(PacketType::DISCONNECT) << 4);
    packet.push_back(0x00);  // No remaining length
    return packet;
}

// ============================================================================
// Parse MQTT Packets
// ============================================================================

ParsedPacket parse_packet(const uint8_t* data, size_t size)
{
    ParsedPacket result;

    if (size < 2) {
        result.error = "Packet too small";
        return result;
    }

    // Fixed header byte 1: packet type in upper nibble, flags in lower nibble
    result.type = static_cast<PacketType>(data[0] >> 4);
    result.flags = data[0] & 0x0F;

    // Decode remaining length (variable, 1-4 bytes)
    uint32_t remaining_length = 0;
    size_t rl_bytes = decode_remaining_length(data + 1, size - 1, remaining_length);

    if (rl_bytes == 0 || (1 + rl_bytes + remaining_length) > size) {
        result.error = "Invalid remaining length or incomplete packet";
        return result;
    }

    // Extract payload
    size_t header_size = 1 + rl_bytes;
    result.payload.assign(data + header_size, data + header_size + remaining_length);

    return result;
}

ConnackData parse_connack(const uint8_t* payload, size_t size)
{
    ConnackData result = {false, ConnackReturnCode::ACCEPTED};

    if (size < 2) {
        return result;
    }

    result.session_present = (payload[0] & 0x01) != 0;
    result.code = static_cast<ConnackReturnCode>(payload[1]);

    return result;
}

PubackData parse_puback(const uint8_t* payload, size_t size)
{
    PubackData result = {0};

    if (size < 2) {
        return result;
    }

    result.packet_id = decode_be16(payload);
    return result;
}

SubackData parse_suback(const uint8_t* payload, size_t size)
{
    SubackData result = {0, {}};

    if (size < 3) {
        return result;
    }

    result.packet_id = decode_be16(payload);

    // Return codes start at offset 2
    for (size_t i = 2; i < size; ++i) {
        result.return_codes.push_back(payload[i]);
    }

    return result;
}

PublishData parse_publish(const uint8_t* header, const uint8_t* payload, size_t size)
{
    PublishData result = {"", "", 0, QoS::AT_MOST_ONCE, false, false};

    // Header byte contains packet type and flags
    uint8_t fixed = header[0];
    result.qos = static_cast<QoS>((fixed >> 1) & 0x03);
    result.dup = ((fixed & 0x08) != 0);
    result.retain = ((fixed & 0x01) != 0);

    // Need at least 2 bytes for topic length
    if (size < 2) {
        return result;
    }

    // Decode topic
    size_t remaining = size;
    result.topic = decode_utf8_string(payload, remaining);

    // Decode packet ID if QoS > 0
    if (result.qos != QoS::AT_MOST_ONCE && remaining >= 2) {
        result.packet_id = decode_be16(payload);
        payload += 2;
        remaining -= 2;
    }

    // Rest is payload
    if (remaining > 0) {
        result.payload.assign(reinterpret_cast<const char*>(payload), remaining);
    }

    return result;
}

} // namespace midnight::protocols::mqtt
