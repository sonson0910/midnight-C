#pragma once

#include <string>
#include <functional>
#include <memory>

namespace midnight::protocols::websocket
{

    using MessageHandler = std::function<void(const std::string &message)>;
    using ErrorHandler = std::function<void(const std::string &error)>;

    /**
     * @brief WebSocket Client for real-time communication
     */
    class WebSocketClient
    {
    public:
        /**
         * @brief Connect to WebSocket server
         */
        void connect(const std::string &url);

        /**
         * @brief Disconnect from server
         */
        void disconnect();

        /**
         * @brief Send message
         */
        void send(const std::string &message);

        /**
         * @brief Check if connected
         */
        bool is_connected() const;

        /**
         * @brief Set message handler
         */
        void set_message_handler(MessageHandler handler);

        /**
         * @brief Set error handler
         */
        void set_error_handler(ErrorHandler handler);

    private:
        bool connected_ = false;
        std::string url_;
        MessageHandler message_handler_;
        ErrorHandler error_handler_;
    };

} // namespace midnight::protocols::websocket
