#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

namespace midnight::protocols::websocket
{

    using MessageHandler = std::function<void(const std::string &message)>;
    using ErrorHandler = std::function<void(const std::string &error)>;
    using OpenHandler = std::function<void()>;
    using CloseHandler = std::function<void()>;

    /**
     * @brief WebSocket Client for real-time communication using cpp-httplib
     *
     * Provides WebSocket connectivity with automatic reconnection,
     * message framing, and handler callbacks.
     */
    class WebSocketClient
    {
    public:
        /**
         * @brief Construct WebSocket client
         * @param url WebSocket server URL (ws:// or wss://)
         * @param timeout_ms Connection timeout in milliseconds
         */
        explicit WebSocketClient(const std::string &url, uint32_t timeout_ms = 10000);

        ~WebSocketClient();

        // Non-copyable, movable
        WebSocketClient(const WebSocketClient &) = delete;
        WebSocketClient &operator=(const WebSocketClient &) = delete;
        WebSocketClient(WebSocketClient &&) noexcept;
        WebSocketClient &operator=(WebSocketClient &&) noexcept;

        /**
         * @brief Connect to WebSocket server
         * @return true if connection successful
         */
        bool connect();

        /**
         * @brief Disconnect from server
         */
        void disconnect();

        /**
         * @brief Send text message
         * @param message Message to send
         * @return true if message sent successfully
         */
        bool send(const std::string &message);

        /**
         * @brief Send binary message
         * @param data Binary data to send
         * @return true if message sent successfully
         */
        bool send_binary(const std::vector<uint8_t> &data);

        /**
         * @brief Check if connected
         */
        bool is_connected() const;

        /**
         * @brief Set message handler (text messages)
         */
        void set_message_handler(MessageHandler handler);

        /**
         * @brief Set binary message handler
         */
        void set_binary_handler(std::function<void(const std::vector<uint8_t> &data)> handler);

        /**
         * @brief Set error handler
         */
        void set_error_handler(ErrorHandler handler);

        /**
         * @brief Set connection open handler
         */
        void set_open_handler(OpenHandler handler);

        /**
         * @brief Set connection close handler
         */
        void set_close_handler(CloseHandler handler);

        /**
         * @brief Get last error message
         */
        std::string last_error() const;

    private:
        void run_event_loop();
        void stop_event_loop();

        // Opaque implementation pointer - details hidden in .cpp
        struct Impl;
        std::unique_ptr<Impl> pImpl_;

        std::string url_;
        uint32_t timeout_ms_;
        std::atomic<bool> connected_{false};
        std::atomic<bool> running_{false};
        std::thread event_thread_;
        std::mutex mutex_;

        MessageHandler message_handler_;
        std::function<void(const std::vector<uint8_t> &)> binary_handler_;
        ErrorHandler error_handler_;
        OpenHandler open_handler_;
        CloseHandler close_handler_;

        std::string last_error_;
    };

} // namespace midnight::protocols::websocket
