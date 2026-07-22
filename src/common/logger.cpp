#include "corpcron/common/logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace corpcron {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::setLevel(LogLevel level) { current_level_.store(level, std::memory_order_relaxed); }

const char* Logger::levelToString(LogLevel level) {
    switch(level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::quoteValue(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    out.push_back('"');
    return out;
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < current_level_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    std::stringstream ss;
    if (localtime_r(&now_c, &tm_buf) != nullptr) {
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    } else {
        ss << "0000-00-00 00:00:00";
    }
    ss << " [" << levelToString(level) << "] " << msg;
    std::cout << ss.str() << std::endl;
}

void Logger::log(LogLevel level, const std::string& event, LogFields fields) {
    if (level < current_level_.load(std::memory_order_relaxed)) return;
    std::stringstream msg;
    msg << "event=" << quoteValue(event);
    for (const auto& [key, value] : fields) {
        msg << ' ' << key << '=' << quoteValue(value);
    }
    log(level, msg.str());
}

void Logger::debug(const std::string& msg) { log(DEBUG, msg); }
void Logger::debug(const std::string& event, LogFields fields) { log(DEBUG, event, fields); }
void Logger::info(const std::string& msg)  { log(INFO, msg); }
void Logger::info(const std::string& event, LogFields fields)  { log(INFO, event, fields); }
void Logger::warn(const std::string& msg)  { log(WARN, msg); }
void Logger::warn(const std::string& event, LogFields fields)  { log(WARN, event, fields); }
void Logger::error(const std::string& msg) { log(ERROR, msg); }
void Logger::error(const std::string& event, LogFields fields) { log(ERROR, event, fields); }

}
