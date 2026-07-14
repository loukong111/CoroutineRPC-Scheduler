#include "corpcron/metrics/alert_rules.hpp"
#include <sstream>

namespace corpcron {

namespace {

uint64_t percent(uint64_t part, uint64_t total) {
    if (total == 0) return 0;
    return static_cast<uint64_t>((static_cast<long double>(part) * 100.0L) /
                                 static_cast<long double>(total));
}

void addRatioAlert(std::vector<Alert>& alerts, const std::string& name,
                   const std::string& message, uint64_t total, uint64_t part,
                   uint64_t min_total, uint64_t threshold_percent) {
    if (total < min_total) return;
    const uint64_t observed_percent = percent(part, total);
    if (observed_percent >= threshold_percent) {
        alerts.push_back(Alert{name, "warning", message, observed_percent, threshold_percent});
    }
}

void addLatencyAlert(std::vector<Alert>& alerts, const std::string& name,
                     const std::string& message, uint64_t samples, uint64_t observed_ms,
                     uint64_t threshold_ms) {
    if (samples == 0 || threshold_ms == 0) return;
    if (observed_ms >= threshold_ms) {
        alerts.push_back(Alert{name, "warning", message, observed_ms, threshold_ms});
    }
}

std::string quote(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '"' || ch == '\\') escaped.push_back('\\');
        escaped.push_back(ch);
    }
    return "\"" + escaped + "\"";
}

} // namespace

std::vector<Alert> AlertRules::evaluate(const MetricsSnapshot& snapshot,
                                        const AlertRuleConfig& config) {
    std::vector<Alert> alerts;

    addRatioAlert(alerts, "rpc_error_rate_high", "RPC error rate is above threshold",
                  snapshot.rpc_requests_total, snapshot.rpc_error_total,
                  config.min_rpc_requests, config.rpc_error_rate_percent);

    const uint64_t task_executions = snapshot.task_success_total + snapshot.task_failure_total;
    addRatioAlert(alerts, "task_failure_rate_high", "Task failure rate is above threshold",
                  task_executions, snapshot.task_failure_total, config.min_task_executions,
                  config.task_failure_rate_percent);

    const uint64_t lock_attempts =
        snapshot.lock_acquire_success_total + snapshot.lock_acquire_failure_total;
    addRatioAlert(alerts, "lock_failure_rate_high", "Redis lock acquire failure rate is high",
                  lock_attempts, snapshot.lock_acquire_failure_total, config.min_lock_attempts,
                  config.lock_failure_rate_percent);

    addLatencyAlert(alerts, "schedule_delay_p99_high", "Schedule delay p99 is above threshold",
                    snapshot.schedule_delay_samples_total, snapshot.schedule_delay_p99_ms,
                    config.schedule_delay_p99_ms);

    addLatencyAlert(alerts, "task_duration_p99_high", "Task duration p99 is above threshold",
                    snapshot.task_duration_samples_total, snapshot.task_duration_p99_ms,
                    config.task_duration_p99_ms);

    return alerts;
}

std::string AlertRules::renderText(const MetricsSnapshot& snapshot,
                                   const AlertRuleConfig& config) {
    const auto alerts = evaluate(snapshot, config);

    std::ostringstream out;
    out << "# CorpCron Alert Status\n";
    out << "status=" << (alerts.empty() ? "ok" : "firing") << "\n";
    out << "alerts_firing=" << alerts.size() << "\n";
    for (const Alert& alert : alerts) {
        out << "alert name=" << quote(alert.name) << " severity=" << quote(alert.severity)
            << " observed=" << alert.observed << " threshold=" << alert.threshold
            << " message=" << quote(alert.message) << "\n";
    }
    return out.str();
}

} // namespace corpcron
