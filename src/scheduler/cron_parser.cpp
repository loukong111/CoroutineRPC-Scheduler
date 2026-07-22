#include "corpcron/scheduler/cron_parser.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/third_party/croncpp.h"
#include <chrono>
#include <ctime>

namespace corpcron {

uint64_t CronParser::nextExecution(const std::string& cron_expr, uint64_t now_ms) {
    if (now_ms == 0) {
        now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    time_t now_sec = static_cast<time_t>(now_ms / 1000); // time_t uses seconds.
    std::tm now_tm{};
    if (!localtime_r(&now_sec, &now_tm)) {
        LOG_WARN("Cron conversion failed for current time");
        return 0;
    }

    try {
        auto cron = cron::make_cron(cron_expr);
        auto next = cron::cron_next(cron, now_tm);
        time_t next_sec = std::mktime(&next);
        if (next_sec == static_cast<time_t>(-1) || next_sec <= now_sec) {
            LOG_WARN("Cron expression did not produce a future execution time");
            return 0;
        }
        return static_cast<uint64_t>(next_sec) * 1000;
    } catch (const std::exception& e) {
        LOG_WARN(std::string("Cron parse error: ") + e.what());
        return 0;
    }
}

} // namespace corpcron
