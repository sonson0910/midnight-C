#pragma once

#include "midnight/protocols/coap/coap_client.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace midnight::protocols::coap
{

    struct CoapOption
    {
        uint16_t number;
        std::string value;
    };

    struct CoapMessage
    {
        uint8_t version = 0;
        MessageType type = MessageType::NON;
        uint8_t code = 0;
        uint16_t message_id = 0;
        std::string token;
        std::vector<CoapOption> options;
        std::string payload;
    };

    /**
     * @brief Encode a CoAP option with delta encoding
     * @param option_number Option number (e.g., 11 for Uri-Path)
     * @param value Option value as string
     * @return Encoded option bytes
     */
    std::vector<uint8_t> encode_option(uint16_t option_number, const std::string &value);

    /**
     * @brief Decode option value from CoAP message bytes
     * @param data Pointer to option data
     * @param len Length of data
     * @param consumed Output: number of bytes consumed
     * @return Decoded option value
     */
    std::string decode_option_value(const uint8_t *data, size_t len, size_t &consumed);

    /**
     * @brief Parse options from CoAP message body
     * @param data Pointer to options data
     * @param len Length of data
     * @param header_size Output: number of header bytes consumed
     * @return Vector of parsed options
     */
    std::vector<CoapOption> parse_options(const uint8_t *data, size_t len, size_t &header_size);

    /**
     * @brief Parse complete CoAP message from raw bytes
     * @param data Pointer to message data
     * @param len Length of data
     * @param out_msg Output: parsed message
     * @return true if parsing succeeded
     */
    bool parse_message(const uint8_t *data, size_t len, CoapMessage &out_msg);

    /**
     * @brief Validate CoAP response
     * @param msg Parsed message to validate
     * @return true if response is valid success response
     */
    bool validate_response(const CoapMessage &msg);

} // namespace midnight::protocols::coap
