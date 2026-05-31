#include "corpcron/scheduler/cron_parser.hpp"
#include "corpcron/third_party/croncpp.h"
#include <chrono>
#include <ctime>
#include <iostream>

namespace corpcron {

uint64_t CronParser::nextExecution(const std::string& cron_expr, uint64_t now_ms) {
    if (now_ms == 0) {
        now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    time_t now_sec = now_ms / 1000;//time_t 只认秒
    std::tm now_tm;
    localtime_r(&now_sec, &now_tm);

    try {
        auto cron = cron::make_cron(cron_expr);
        auto next = cron::cron_next(cron, now_tm);
        time_t next_sec = std::mktime(const_cast<std::tm*>(&next));
        return static_cast<uint64_t>(next_sec) * 1000;
    } catch (const std::exception& e) {
        std::cerr << "Cron parse error: " << e.what() << std::endl;
        return 0;
    }
}

} // namespace corpcron