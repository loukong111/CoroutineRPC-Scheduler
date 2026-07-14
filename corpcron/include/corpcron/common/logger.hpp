#pragma once
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace corpcron {

enum LogLevel { DEBUG, INFO, WARN, ERROR };
using LogFields = std::initializer_list<std::pair<std::string, std::string>>;

class Logger {
public:
    static Logger& instance();
    void setLevel(LogLevel level);
    void log(LogLevel level, const std::string& msg);
    void log(LogLevel level, const std::string& event, LogFields fields);
    void debug(const std::string& msg);
    void debug(const std::string& event, LogFields fields);
    void info(const std::string& msg);
    void info(const std::string& event, LogFields fields);
    void warn(const std::string& msg);
    void warn(const std::string& event, LogFields fields);
    void error(const std::string& msg);
    void error(const std::string& event, LogFields fields);
private:
    Logger() = default;
    LogLevel current_level_ = INFO;
    std::mutex mutex_;
    const char* levelToString(LogLevel level);
    static std::string quoteValue(const std::string& value);
};

#define LOG_DEBUG(msg) corpcron::Logger::instance().debug(msg)
#define LOG_INFO(msg)  corpcron::Logger::instance().info(msg)
#define LOG_WARN(msg)  corpcron::Logger::instance().warn(msg)
#define LOG_ERROR(msg) corpcron::Logger::instance().error(msg)

#define LOG_DEBUG_EVENT(event, ...) corpcron::Logger::instance().debug(event, __VA_ARGS__)
#define LOG_INFO_EVENT(event, ...)  corpcron::Logger::instance().info(event, __VA_ARGS__)
#define LOG_WARN_EVENT(event, ...)  corpcron::Logger::instance().warn(event, __VA_ARGS__)
#define LOG_ERROR_EVENT(event, ...) corpcron::Logger::instance().error(event, __VA_ARGS__)

}
