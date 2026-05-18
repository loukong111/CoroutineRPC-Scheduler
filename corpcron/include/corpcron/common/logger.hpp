#pragma once
#include <string>
#include <iostream>
#include <mutex>

namespace corpcron {

enum LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static Logger& instance();
    void setLevel(LogLevel level);
    void log(LogLevel level, const std::string& msg);
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
private:
    Logger() = default;
    LogLevel current_level_ = INFO;
    std::mutex mutex_;
    const char* levelToString(LogLevel level);
};

#define LOG_DEBUG(msg) corpcron::Logger::instance().debug(msg)
#define LOG_INFO(msg)  corpcron::Logger::instance().info(msg)
#define LOG_WARN(msg)  corpcron::Logger::instance().warn(msg)
#define LOG_ERROR(msg) corpcron::Logger::instance().error(msg)

}