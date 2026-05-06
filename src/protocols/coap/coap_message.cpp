#include "midnight/protocols/coap/coap_message.hpp"
#include "midnight/core/logger.hpp"

#include <vector>
#include <cstring>

namespace midnight::protocols::coap
{

    namespace
    {
        constexpr uint8_t PAYLOAD_MARKER = 0xFF;
        constexpr uint8_t COAP_VERSION = 1;
        constexpr uint8_t COAP_HEADER_SIZE = 4;
    }

    std::vector<uint8_t> encode_option(uint16_t option_number, const std::string &value)
    {
        std::vector<uint8_t> opt;

        uint16_t delta = option_number;

        // Delta encoding
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

        // Option value
        if (!value.empty()) {
            opt.insert(opt.end(), value.begin(), value.end());
        }

        return opt;
    }

    std::string decode_option_value(const uint8_t *data, size_t len, size_t &consumed)
    {
        consumed = 0;
        if (len == 0) return "";

        uint8_t first_byte = data[0];
        uint8_t delta_nibble = (first_byte >> 4) & 0x0F;
        uint8_t length_nibble = first_byte & 0x0F;

        size_t pos = 1;

        // Extended delta
        uint16_t delta = delta_nibble;
        if (delta_nibble == 13) {
            if (pos >= len) return "";
            delta = 13 + data[pos++];
        } else if (delta_nibble == 14) {
            if (pos + 1 >= len) return "";
            delta = 269 + ((static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1]);
            pos += 2;
        }

        // Extended length
        size_t opt_len = length_nibble;
        if (length_nibble == 13) {
            if (pos >= len) return "";
            opt_len = 13 + data[pos++];
        } else if (length_nibble == 14) {
            if (pos + 1 >= len) return "";
            opt_len = 269 + ((static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1]);
            pos += 2;
        }

        // Extract value
        if (pos + opt_len > len) return "";
        std::string value(reinterpret_cast<const char*>(&data[pos]), opt_len);
        consumed = pos + opt_len;

        return value;
    }

    std::vector<CoapOption> parse_options(const uint8_t *data, size_t len, size_t &header_size)
    {
        std::vector<CoapOption> options;
        header_size = COAP_HEADER_SIZE;

        if (len < COAP_HEADER_SIZE) return options;

        // Parse token length
        uint8_t token_len = data[0] & 0x0F;
        header_size += token_len;

        // Parse options
        uint16_t prev_option = 0;
        size_t pos = header_size;

        while (pos < len && data[pos] != PAYLOAD_MARKER) {
            size_t consumed = 0;
            std::string value = decode_option_value(&data[pos], len - pos, consumed);
            if (consumed == 0) break;

            uint8_t first_byte = data[pos];
            uint8_t delta_nibble = (first_byte >> 4) & 0x0F;

            uint16_t delta = delta_nibble;
            if (delta_nibble == 13) {
                if (pos + 1 < len) delta = 13 + data[pos + 1];
            } else if (delta_nibble == 14) {
                delta = 269 + ((static_cast<uint16_t>(data[pos + 1]) << 8) | data[pos + 2]);
            }

            uint16_t option_number = prev_option + delta;
            options.push_back({option_number, value});
            prev_option = option_number;

            pos += consumed;
        }

        return options;
    }

    bool parse_message(const uint8_t *data, size_t len, CoapMessage &out_msg)
    {
        if (len < COAP_HEADER_SIZE) return false;

        // Header
        out_msg.version = (data[0] >> 6) & 0x03;
        out_msg.type = static_cast<MessageType>((data[0] >> 4) & 0x03);
        uint8_t token_len = data[0] & 0x0F;
        out_msg.code = data[1];
        out_msg.message_id = (static_cast<uint16_t>(data[2]) << 8) | data[3];

        if (out_msg.version != COAP_VERSION) {
            midnight::g_logger->error("Invalid CoAP version");
            return false;
        }

        // Token
        size_t pos = COAP_HEADER_SIZE;
        if (token_len > 0 && pos + token_len <= len) {
            out_msg.token.assign(reinterpret_cast<const char*>(&data[pos]), token_len);
            pos += token_len;
        }

        // Options
        size_t header_end = 0;
        out_msg.options = parse_options(&data[pos], len - pos, header_end);
        pos += header_end;

        // Skip payload marker
        if (pos < len && data[pos] == PAYLOAD_MARKER) {
            pos++;
        }

        // Payload
        if (pos < len) {
            out_msg.payload.assign(reinterpret_cast<const char*>(&data[pos]), len - pos);
        }

        return true;
    }

    bool validate_response(const CoapMessage &msg)
    {
        // Check version
        if (msg.version != COAP_VERSION) {
            midnight::g_logger->error("Invalid CoAP version in response");
            return false;
        }

        // Check code class (2.x = success, 4.x = client error, 5.x = server error)
        uint8_t code_class = msg.code >> 5;

        if (code_class == 2) {
            return true; // Success
        } else if (code_class == 4) {
            midnight::g_logger->warn("CoAP client error: code " + std::to_string(msg.code));
            return false;
        } else if (code_class == 5) {
            midnight::g_logger->error("CoAP server error: code " + std::to_string(msg.code));
            return false;
        } else if (msg.code == 0) {
            return true; // Empty ACK
        }

        return false;
    }

} // namespace midnight::protocols::coap
