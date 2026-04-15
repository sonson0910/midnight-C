#include "midnight/session/session_manager.hpp"
#include <uuid/uuid.h>
#include <cstring>

namespace midnight
{

    std::string generate_uuid()
    {
        char uuid_str[37];
        uuid_t uuid;
        uuid_generate(uuid);
        uuid_unparse(uuid, uuid_str);
        return std::string(uuid_str);
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
