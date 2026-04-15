#include "midnight/core/logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

namespace midnight
{

    std::shared_ptr<Logger> g_logger = std::make_shared<ConsoleLogger>();

    static std::string level_to_string(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
        }
    }

    void ConsoleLogger::log(LogLevel level, const std::string &message)
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::cout << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
                  << '.' << std::setfill('0') << std::setw(3) << ms.count()
                  << " [" << level_to_string(level) << "] " << message << std::endl;
    }

} // namespace midnight
