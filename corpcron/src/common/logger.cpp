#include "corpcron/common/logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace corpcron {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::setLevel(LogLevel level) { current_level_ = level; }

const char* Logger::levelToString(LogLevel level) {
    switch(level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < current_level_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&now_c, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << " ["
       << levelToString(level) << "] " << msg;
    std::cout << ss.str() << std::endl;
}

void Logger::debug(const std::string& msg) { log(DEBUG, msg); }
void Logger::info(const std::string& msg)  { log(INFO, msg); }
void Logger::warn(const std::string& msg)  { log(WARN, msg); }
void Logger::error(const std::string& msg) { log(ERROR, msg); }

}