#include "corpcron/metrics/alert_rules.hpp"
#include <cassert>
#include <string>

int main() {
    corpcron::AlertRuleConfig config;
    config.min_rpc_requests = 10;
    config.rpc_error_rate_percent = 5;
    config.min_task_executions = 4;
    config.task_failure_rate_percent = 25;
    config.min_lock_attempts = 4;
    config.lock_failure_rate_percent = 50;
    config.schedule_delay_p99_ms = 1000;
    config.task_duration_p99_ms = 2000;

    corpcron::MetricsSnapshot ok;
    ok.rpc_requests_total = 9;
    ok.rpc_error_total = 9;
    assert(corpcron::AlertRules::evaluate(ok, config).empty());

    corpcron::MetricsSnapshot snapshot;
    snapshot.rpc_requests_total = 100;
    snapshot.rpc_error_total = 6;
    snapshot.task_success_total = 6;
    snapshot.task_failure_total = 2;
    snapshot.lock_acquire_success_total = 2;
    snapshot.lock_acquire_failure_total = 4;
    snapshot.schedule_delay_samples_total = 10;
    snapshot.schedule_delay_p99_ms = 1500;
    snapshot.task_duration_samples_total = 10;
    snapshot.task_duration_p99_ms = 3000;

    const auto alerts = corpcron::AlertRules::evaluate(snapshot, config);
    assert(alerts.size() == 5);

    const std::string body = corpcron::AlertRules::renderText(snapshot, config);
    assert(body.find("status=firing") != std::string::npos);
    assert(body.find("alerts_firing=5") != std::string::npos);
    assert(body.find("rpc_error_rate_high") != std::string::npos);
    assert(body.find("task_failure_rate_high") != std::string::npos);
    assert(body.find("lock_failure_rate_high") != std::string::npos);
    assert(body.find("schedule_delay_p99_high") != std::string::npos);
    assert(body.find("task_duration_p99_high") != std::string::npos);

    corpcron::MetricsSnapshot healthy;
    const std::string healthy_body = corpcron::AlertRules::renderText(healthy, config);
    assert(healthy_body.find("status=ok") != std::string::npos);
    assert(healthy_body.find("alerts_firing=0") != std::string::npos);
    return 0;
}
