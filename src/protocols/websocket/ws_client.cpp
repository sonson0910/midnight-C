#include "midnight/protocols/websocket/ws_client.hpp"
#include "midnight/core/logger.hpp"

#include <httplib.h>

namespace midnight::protocols::websocket
{

    // =========================================================================
    // Impl - Internal implementation hiding httplib details
    // =========================================================================
    struct WebSocketClient::Impl
    {
        std::shared_ptr<httplib::ws::WebSocketClient> ws_client;

        Impl(const std::string& url, uint32_t timeout_ms)
        {
            // httplib::ws::WebSocketClient takes full URL as first argument
            ws_client = std::make_shared<httplib::ws::WebSocketClient>(url);
            ws_client->set_connection_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
            ws_client->set_read_timeout(60, 0); // 60 seconds read timeout

            midnight::g_logger->debug("WebSocket client created for: " + url);
        }
    };

    // =========================================================================
    // WebSocketClient - Public interface
    // =========================================================================

    WebSocketClient::WebSocketClient(const std::string &url, uint32_t timeout_ms)
        : pImpl_(std::make_unique<Impl>(url, timeout_ms))
        , url_(url)
        , timeout_ms_(timeout_ms)
        , last_error_()
    {
        midnight::g_logger->info("WebSocketClient created: " + url);
    }

    WebSocketClient::~WebSocketClient() = default;

    WebSocketClient::WebSocketClient(WebSocketClient &&other) noexcept
        : pImpl_(std::move(other.pImpl_))
        , url_(std::move(other.url_))
        , timeout_ms_(other.timeout_ms_)
        , connected_(other.connected_.load())
        , running_(other.running_.load())
        , event_thread_(std::move(other.event_thread_))
        , message_handler_(std::move(other.message_handler_))
        , binary_handler_(std::move(other.binary_handler_))
        , error_handler_(std::move(other.error_handler_))
        , open_handler_(std::move(other.open_handler_))
        , close_handler_(std::move(other.close_handler_))
        , last_error_(std::move(other.last_error_))
    {
    }

    WebSocketClient &WebSocketClient::operator=(WebSocketClient &&other) noexcept
    {
        if (this != &other) {
            disconnect();
            pImpl_ = std::move(other.pImpl_);
            url_ = std::move(other.url_);
            timeout_ms_ = other.timeout_ms_;
            connected_ = other.connected_.load();
            running_ = other.running_.load();
            event_thread_ = std::move(other.event_thread_);
            message_handler_ = std::move(other.message_handler_);
            binary_handler_ = std::move(other.binary_handler_);
            error_handler_ = std::move(other.error_handler_);
            open_handler_ = std::move(other.open_handler_);
            close_handler_ = std::move(other.close_handler_);
            last_error_ = std::move(other.last_error_);
        }
        return *this;
    }

    bool WebSocketClient::connect()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (connected_) {
            midnight::g_logger->warn("WebSocket already connected");
            return true;
        }

        try {
            midnight::g_logger->info("Connecting to WebSocket: " + url_);

            if (!pImpl_->ws_client->connect()) {
                last_error_ = "Failed to open WebSocket connection";
                midnight::g_logger->error("WebSocket connection failed");
                return false;
            }

            connected_ = true;
            running_ = true;

            if (open_handler_) {
                open_handler_();
            }

            // Start event loop in background thread
            event_thread_ = std::thread([this]() {
                run_event_loop();
            });

            midnight::g_logger->info("WebSocket connection started");
            return true;
        } catch (const std::exception &e) {
            last_error_ = e.what();
            midnight::g_logger->error("WebSocket exception: " + last_error_);
            return false;
        }
    }

    void WebSocketClient::disconnect()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_) {
            return;
        }

        midnight::g_logger->info("Disconnecting WebSocket");

        running_ = false;
        connected_ = false;

        if (event_thread_.joinable()) {
            event_thread_.join();
        }

        try {
            pImpl_->ws_client->close();
        } catch (...) {
            // Ignore close errors
        }

        if (close_handler_) {
            close_handler_();
        }

        midnight::g_logger->info("WebSocket disconnected");
    }

    bool WebSocketClient::send(const std::string &message)
    {
        if (!connected_) {
            last_error_ = "Not connected";
            return false;
        }

        try {
            if (!pImpl_->ws_client->send(message)) {
                last_error_ = "Failed to send message";
                midnight::g_logger->error("WebSocket send failed");
                return false;
            }
            midnight::g_logger->debug("WebSocket sent message: " + message.substr(0, 100));
            return true;
        } catch (const std::exception &e) {
            last_error_ = e.what();
            midnight::g_logger->error("WebSocket send exception: " + last_error_);
            return false;
        }
    }

    bool WebSocketClient::send_binary(const std::vector<uint8_t> &data)
    {
        if (!connected_) {
            last_error_ = "Not connected";
            return false;
        }

        try {
            // Cast to const char* for the overload
            if (!pImpl_->ws_client->send(reinterpret_cast<const char*>(data.data()), data.size())) {
                last_error_ = "Failed to send binary message";
                midnight::g_logger->error("WebSocket binary send failed");
                return false;
            }
            midnight::g_logger->debug("WebSocket sent binary: " + std::to_string(data.size()) + " bytes");
            return true;
        } catch (const std::exception &e) {
            last_error_ = e.what();
            midnight::g_logger->error("WebSocket binary send exception: " + last_error_);
            return false;
        }
    }

    bool WebSocketClient::is_connected() const
    {
        return connected_ && pImpl_->ws_client->is_open();
    }

    void WebSocketClient::set_message_handler(MessageHandler handler)
    {
        message_handler_ = std::move(handler);
    }

    void WebSocketClient::set_binary_handler(std::function<void(const std::vector<uint8_t> &data)> handler)
    {
        binary_handler_ = std::move(handler);
    }

    void WebSocketClient::set_error_handler(ErrorHandler handler)
    {
        error_handler_ = std::move(handler);
    }

    void WebSocketClient::set_open_handler(OpenHandler handler)
    {
        open_handler_ = std::move(handler);
    }

    void WebSocketClient::set_close_handler(CloseHandler handler)
    {
        close_handler_ = std::move(handler);
    }

    std::string WebSocketClient::last_error() const
    {
        return last_error_;
    }

    void WebSocketClient::run_event_loop()
    {
        midnight::g_logger->debug("WebSocket event loop started");

        while (running_ && connected_) {
            try {
                std::string msg;
                auto result = pImpl_->ws_client->read(msg);

                if (result == httplib::ws::ReadResult::Text) {
                    if (!msg.empty()) {
                        midnight::g_logger->debug("WebSocket received text: " + msg.substr(0, 100));
                        if (message_handler_) {
                            message_handler_(msg);
                        }
                    }
                } else if (result == httplib::ws::ReadResult::Binary) {
                    midnight::g_logger->debug("WebSocket received binary: " + std::to_string(msg.size()) + " bytes");
                    if (binary_handler_) {
                        // Note: httplib's ws::WebSocketClient::read() uses std::string for both
                        // text and binary frames. The bytes are valid, but the string may not
                        // be valid UTF-8. Copying raw bytes from string to vector is safe here.
                        std::vector<uint8_t> data(msg.begin(), msg.end());
                        binary_handler_(data);
                    }
                } else {
                    midnight::g_logger->debug("WebSocket connection closed or error");
                    break;
                }
            } catch (const std::exception &e) {
                midnight::g_logger->error("WebSocket event loop exception: " + std::string(e.what()));
                if (error_handler_) {
                    error_handler_(e.what());
                }
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        connected_ = false;
        running_ = false;
        midnight::g_logger->debug("WebSocket event loop ended");

        if (close_handler_) {
            close_handler_();
        }
    }

    void WebSocketClient::stop_event_loop()
    {
        running_ = false;
    }

} // namespace midnight::protocols::websocket
