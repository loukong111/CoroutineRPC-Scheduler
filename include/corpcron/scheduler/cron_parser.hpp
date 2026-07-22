#pragma once
#include <string>
#include <cstdint>

namespace corpcron {

class CronParser {
public:
    static uint64_t nextExecution(const std::string& cron_expr, uint64_t now_ms = 0);
};

} // namespace corpcron