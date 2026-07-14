#include "corpcron/metrics/metrics.hpp"
#include <cassert>
#include <string>

int main() {
    corpcron::MetricsSnapshot snapshot;
    snapshot.rpc_requests_total = 12;
    snapshot.rpc_success_total = 10;
    snapshot.rpc_error_total = 2;
    snapshot.active_connections = 3;
    snapshot.bytes_in_total = 128;
    snapshot.bytes_out_total = 256;
    snapshot.task_success_total = 4;
    snapshot.lock_acquire_failure_total = 1;
    snapshot.max_task_duration_ms = 42;
    snapshot.task_duration_p95_ms = 40;
    snapshot.task_duration_p99_ms = 42;
    snapshot.task_duration_samples_total = 8;
    snapshot.schedule_delay_max_ms = 900;
    snapshot.schedule_delay_p95_ms = 800;
    snapshot.schedule_delay_p99_ms = 900;
    snapshot.schedule_delay_samples_total = 7;

    const std::string body = corpcron::Metrics::renderPrometheus(snapshot);
    assert(body.find("# TYPE corpcron_rpc_requests_total counter") != std::string::npos);
    assert(body.find("corpcron_rpc_requests_total 12") != std::string::npos);
    assert(body.find("corpcron_rpc_success_total 10") != std::string::npos);
    assert(body.find("corpcron_rpc_error_total 2") != std::string::npos);
    assert(body.find("corpcron_active_connections 3") != std::string::npos);
    assert(body.find("corpcron_bytes_in_total 128") != std::string::npos);
    assert(body.find("corpcron_bytes_out_total 256") != std::string::npos);
    assert(body.find("corpcron_task_success_total 4") != std::string::npos);
    assert(body.find("corpcron_lock_acquire_failure_total 1") != std::string::npos);
    assert(body.find("corpcron_max_task_duration_ms 42") != std::string::npos);
    assert(body.find("corpcron_task_duration_p95_ms 40") != std::string::npos);
    assert(body.find("corpcron_task_duration_p99_ms 42") != std::string::npos);
    assert(body.find("corpcron_task_duration_samples_total 8") != std::string::npos);
    assert(body.find("corpcron_schedule_delay_max_ms 900") != std::string::npos);
    assert(body.find("corpcron_schedule_delay_p95_ms 800") != std::string::npos);
    assert(body.find("corpcron_schedule_delay_p99_ms 900") != std::string::npos);
    assert(body.find("corpcron_schedule_delay_samples_total 7") != std::string::npos);

    auto& metrics = corpcron::Metrics::instance();
    metrics.observeTaskDuration(10);
    metrics.observeTaskDuration(20);
    metrics.observeTaskDuration(100);
    metrics.observeScheduleDelay(5);
    metrics.observeScheduleDelay(15);
    metrics.observeScheduleDelay(50);
    corpcron::MetricsSnapshot observed = metrics.snapshot();
    assert(observed.max_task_duration_ms >= 100);
    assert(observed.task_duration_p95_ms >= 100);
    assert(observed.task_duration_p99_ms >= 100);
    assert(observed.schedule_delay_max_ms >= 50);
    assert(observed.schedule_delay_p95_ms >= 50);
    assert(observed.schedule_delay_p99_ms >= 50);
    return 0;
}
