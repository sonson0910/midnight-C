#include "midnight/session/session_manager.hpp"
#include <array>
#include <iomanip>
#include <random>
#include <sstream>

namespace midnight
{

    std::string generate_uuid()
    {
        std::array<uint8_t, 16> bytes{};
        std::random_device rd;
        for (auto &byte : bytes)
        {
            byte = static_cast<uint8_t>(rd());
        }

        // RFC 4122 version 4 UUID bits
        bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
        bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            ss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
            if (i == 3 || i == 5 || i == 7 || i == 9)
            {
                ss << '-';
            }
        }
        return ss.str();
    }

    SessionManager::SessionId SessionManager::create_session(const std::string &device_id)
    {
        SessionId session_id = generate_uuid();
        auto now = std::chrono::system_clock::now();

        SessionInfo info{
            session_id,
            device_id,
            now,
            now,
            true};

        sessions_[session_id] = info;
        return session_id;
    }

    SessionManager::SessionInfo SessionManager::get_session(const SessionId &session_id) const
    {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            return it->second;
        }
        return SessionInfo{};
    }

    void SessionManager::update_activity(const SessionId &session_id)
    {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            it->second.last_activity = std::chrono::system_clock::now();
        }
    }

    void SessionManager::close_session(const SessionId &session_id)
    {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            it->second.is_active = false;
        }
    }

    bool SessionManager::is_valid(const SessionId &session_id) const
    {
        auto it = sessions_.find(session_id);
        return it != sessions_.end() && it->second.is_active;
    }

} // namespace midnight
