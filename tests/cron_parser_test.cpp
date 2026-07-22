#include "corpcron/scheduler/cron_parser.hpp"
#include <cassert>
#include <cstdint>

int main() {
    constexpr uint64_t now_ms = 1700000000123ULL;
    const uint64_t next_ms = corpcron::CronParser::nextExecution("* * * * * ?", now_ms);
    assert(next_ms > now_ms);
    assert(next_ms <= now_ms + 2000);
    assert(corpcron::CronParser::nextExecution("not-a-cron", now_ms) == 0);
    return 0;
}
