/**
 * @file json_bridge_utils.hpp
 * @brief Bridge utilities between nlohmann::json and jsoncpp (Json::Value)
 *
 * Provides conversion functions for translation units that need to work
 * with both JSON libraries. This header is intentionally separate from
 * common_utils.hpp to avoid forcing all consumers to link against both
 * nlohmann/json and jsoncpp.
 */

#pragma once

#include <nlohmann/json.hpp>
#include <json/json.h>
#include <sstream>
#include <stdexcept>

namespace midnight::util
{

    /**
     * @brief Convert nlohmann::json to Json::Value (jsoncpp)
     * @param input nlohmann::json value to convert
     * @return Equivalent Json::Value
     * @throws std::runtime_error if JSON parsing fails
     */
    inline Json::Value nlohmann_to_jsoncpp(const nlohmann::json &input)
    {
        Json::CharReaderBuilder builder;
        Json::Value parsed;
        std::string errors;
        std::istringstream stream(input.dump());
        if (!Json::parseFromStream(builder, stream, &parsed, &errors))
        {
            throw std::runtime_error("Failed to convert nlohmann::json to jsoncpp: " + errors);
        }
        return parsed;
    }

    /**
     * @brief Convert Json::Value (jsoncpp) to nlohmann::json
     * @param input Json::Value to convert
     * @return Equivalent nlohmann::json
     */
    inline nlohmann::json jsoncpp_to_nlohmann(const Json::Value &input)
    {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        const std::string serialized = Json::writeString(writer, input);
        return nlohmann::json::parse(serialized.empty() ? "{}" : serialized, nullptr, false);
    }

} // namespace midnight::util
