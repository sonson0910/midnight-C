#pragma once

#include <string>
#include <memory>

namespace midnight
{

#if defined(_WIN32) && defined(MIDNIGHT_BUILDING_DLL)
#define MIDNIGHT_LOGGER_API __declspec(dllexport)
#elif defined(_WIN32) && defined(MIDNIGHT_USING_DLL)
#define MIDNIGHT_LOGGER_API __declspec(dllimport)
#else
#define MIDNIGHT_LOGGER_API
#endif

    enum class LogLevel
    {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        CRITICAL = 5
    };

    /**
     * @brief Logger interface for Midnight SDK
     */
    class Logger
    {
    public:
        virtual ~Logger() = default;

        virtual void log(LogLevel level, const std::string &message) = 0;

        void trace(const std::string &msg) { log(LogLevel::TRACE, msg); }
        void debug(const std::string &msg) { log(LogLevel::DEBUG, msg); }
        void info(const std::string &msg) { log(LogLevel::INFO, msg); }
        void warn(const std::string &msg) { log(LogLevel::WARN, msg); }
        void error(const std::string &msg) { log(LogLevel::ERROR, msg); }
        void critical(const std::string &msg) { log(LogLevel::CRITICAL, msg); }
    };

    /**
     * @brief Console logger implementation
     */
    class ConsoleLogger : public Logger
    {
    public:
        void log(LogLevel level, const std::string &message) override;
    };

    // Global logger instance
    extern MIDNIGHT_LOGGER_API std::shared_ptr<Logger> g_logger;

} // namespace midnight
