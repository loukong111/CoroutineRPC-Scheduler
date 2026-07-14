#pragma once

#include "corpcron/metrics/metrics.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace corpcron {

struct AlertRuleConfig {
    uint64_t min_rpc_requests = 100;
    uint64_t rpc_error_rate_percent = 5;
    uint64_t min_task_executions = 10;
    uint64_t task_failure_rate_percent = 20;
    uint64_t min_lock_attempts = 10;
    uint64_t lock_failure_rate_percent = 50;
    uint64_t schedule_delay_p99_ms = 5000;
    uint64_t task_duration_p99_ms = 10000;
};

struct Alert {
    std::string name;
    std::string severity;
    std::string message;
    uint64_t observed = 0;
    uint64_t threshold = 0;
};

class AlertRules {
public:
    static std::vector<Alert> evaluate(const MetricsSnapshot& snapshot,
                                       const AlertRuleConfig& config);
    static std::string renderText(const MetricsSnapshot& snapshot,
                                  const AlertRuleConfig& config);
};

} // namespace corpcron
