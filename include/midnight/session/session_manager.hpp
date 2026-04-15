#pragma once

#include <string>
#include <memory>
#include <map>
#include <chrono>

namespace midnight
{

    /**
     * @brief Session management for IoT connections
     */
    class SessionManager
    {
    public:
        using SessionId = std::string;

        struct SessionInfo
        {
            SessionId id;
            std::string device_id;
            std::chrono::system_clock::time_point created_at;
            std::chrono::system_clock::time_point last_activity;
            bool is_active;
        };

        /**
         * @brief Create a new session
         */
        SessionId create_session(const std::string &device_id);

        /**
         * @brief Get session information
         */
        SessionInfo get_session(const SessionId &session_id) const;

        /**
         * @brief Update session activity
         */
        void update_activity(const SessionId &session_id);

        /**
         * @brief Close session
         */
        void close_session(const SessionId &session_id);

        /**
         * @brief Check if session is valid
         */
        bool is_valid(const SessionId &session_id) const;

    private:
        std::map<SessionId, SessionInfo> sessions_;
    };

} // namespace midnight
