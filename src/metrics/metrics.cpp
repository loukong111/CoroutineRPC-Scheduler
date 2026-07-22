#include "corpcron/metrics/metrics.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace corpcron {

namespace {

uint64_t percentile(std::vector<uint64_t> values, double quantile) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const double rank = std::ceil(quantile * static_cast<double>(values.size()));
    size_t index = static_cast<size_t>(rank <= 1.0 ? 0 : rank - 1.0);
    if (index >= values.size()) index = values.size() - 1;
    return values[index];
}

void updateMax(std::atomic<uint64_t>& max_value, uint64_t observed) {
    uint64_t current = max_value.load(std::memory_order_relaxed);
    while (observed > current &&
           !max_value.compare_exchange_weak(current, observed, std::memory_order_relaxed)) {}
}

} // namespace

Metrics& Metrics::instance() {
    static Metrics metrics;
    return metrics;
}

void Metrics::incRpcRequest() { rpc_requests_total_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::incRpcSuccess() { rpc_success_total_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::incRpcError() { rpc_error_total_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::incActiveConnection() { active_connections_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::decActiveConnection() { active_connections_.fetch_sub(1, std::memory_order_relaxed); }
void Metrics::incRejectedConnection() { rejected_connections_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::incMalformedFrame() { malformed_frames_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::addBytesIn(uint64_t bytes) { bytes_in_total_.fetch_add(bytes, std::memory_order_relaxed); }
void Metrics::addBytesOut(uint64_t bytes) { bytes_out_total_.fetch_add(bytes, std::memory_order_relaxed); }
void Metrics::incTaskSuccess() { task_success_total_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::incTaskFailure() { task_failure_total_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::incLockAcquireSuccess() { lock_acquire_success_total_.fetch_add(1, std::memory_order_relaxed); }
void Metrics::incLockAcquireFailure() { lock_acquire_failure_total_.fetch_add(1, std::memory_order_relaxed); }

void Metrics::observeTaskDuration(uint64_t duration_ms) {
    updateMax(max_task_duration_ms_, duration_ms);
    std::lock_guard<std::mutex> lock(samples_mutex_);
    task_duration_samples_[task_duration_samples_total_ % kSampleWindowSize] = duration_ms;
    ++task_duration_samples_total_;
}

void Metrics::observeScheduleDelay(uint64_t delay_ms) {
    updateMax(schedule_delay_max_ms_, delay_ms);
    std::lock_guard<std::mutex> lock(samples_mutex_);
    schedule_delay_samples_[schedule_delay_samples_total_ % kSampleWindowSize] = delay_ms;
    ++schedule_delay_samples_total_;
}

MetricsSnapshot Metrics::snapshot() const {
    MetricsSnapshot snapshot;
    snapshot.rpc_requests_total = rpc_requests_total_.load(std::memory_order_relaxed);
    snapshot.rpc_success_total = rpc_success_total_.load(std::memory_order_relaxed);
    snapshot.rpc_error_total = rpc_error_total_.load(std::memory_order_relaxed);
    snapshot.active_connections = active_connections_.load(std::memory_order_relaxed);
    snapshot.rejected_connections = rejected_connections_.load(std::memory_order_relaxed);
    snapshot.malformed_frames = malformed_frames_.load(std::memory_order_relaxed);
    snapshot.bytes_in_total = bytes_in_total_.load(std::memory_order_relaxed);
    snapshot.bytes_out_total = bytes_out_total_.load(std::memory_order_relaxed);
    snapshot.task_success_total = task_success_total_.load(std::memory_order_relaxed);
    snapshot.task_failure_total = task_failure_total_.load(std::memory_order_relaxed);
    snapshot.lock_acquire_success_total = lock_acquire_success_total_.load(std::memory_order_relaxed);
    snapshot.lock_acquire_failure_total = lock_acquire_failure_total_.load(std::memory_order_relaxed);
    snapshot.max_task_duration_ms = max_task_duration_ms_.load(std::memory_order_relaxed);
    snapshot.schedule_delay_max_ms = schedule_delay_max_ms_.load(std::memory_order_relaxed);

    std::vector<uint64_t> task_duration_values;
    std::vector<uint64_t> schedule_delay_values;
    {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        snapshot.task_duration_samples_total = task_duration_samples_total_;
        snapshot.schedule_delay_samples_total = schedule_delay_samples_total_;

        const size_t task_count = static_cast<size_t>(
            std::min<uint64_t>(task_duration_samples_total_, kSampleWindowSize));
        task_duration_values.reserve(task_count);
        for (size_t i = 0; i < task_count; ++i) {
            task_duration_values.push_back(task_duration_samples_[i]);
        }

        const size_t delay_count = static_cast<size_t>(
            std::min<uint64_t>(schedule_delay_samples_total_, kSampleWindowSize));
        schedule_delay_values.reserve(delay_count);
        for (size_t i = 0; i < delay_count; ++i) {
            schedule_delay_values.push_back(schedule_delay_samples_[i]);
        }
    }

    snapshot.task_duration_p95_ms = percentile(task_duration_values, 0.95);
    snapshot.task_duration_p99_ms = percentile(task_duration_values, 0.99);
    snapshot.schedule_delay_p95_ms = percentile(schedule_delay_values, 0.95);
    snapshot.schedule_delay_p99_ms = percentile(schedule_delay_values, 0.99);
    return snapshot;
}

std::string Metrics::renderPrometheus() const {
    return renderPrometheus(snapshot());
}

std::string Metrics::renderPrometheus(const MetricsSnapshot& snapshot) {
    std::ostringstream out;
    out << "# HELP corpcron_rpc_requests_total Total RPC requests received.\n";
    out << "# TYPE corpcron_rpc_requests_total counter\n";
    out << "corpcron_rpc_requests_total " << snapshot.rpc_requests_total << "\n";
    out << "# HELP corpcron_rpc_success_total Total successful RPC responses.\n";
    out << "# TYPE corpcron_rpc_success_total counter\n";
    out << "corpcron_rpc_success_total " << snapshot.rpc_success_total << "\n";
    out << "# HELP corpcron_rpc_error_total Total RPC errors.\n";
    out << "# TYPE corpcron_rpc_error_total counter\n";
    out << "corpcron_rpc_error_total " << snapshot.rpc_error_total << "\n";
    out << "# HELP corpcron_active_connections Current active TCP connections.\n";
    out << "# TYPE corpcron_active_connections gauge\n";
    out << "corpcron_active_connections " << snapshot.active_connections << "\n";
    out << "# HELP corpcron_rejected_connections_total Total rejected TCP connections.\n";
    out << "# TYPE corpcron_rejected_connections_total counter\n";
    out << "corpcron_rejected_connections_total " << snapshot.rejected_connections << "\n";
    out << "# HELP corpcron_malformed_frames_total Total malformed RPC frames.\n";
    out << "# TYPE corpcron_malformed_frames_total counter\n";
    out << "corpcron_malformed_frames_total " << snapshot.malformed_frames << "\n";
    out << "# HELP corpcron_bytes_in_total Total inbound TCP bytes.\n";
    out << "# TYPE corpcron_bytes_in_total counter\n";
    out << "corpcron_bytes_in_total " << snapshot.bytes_in_total << "\n";
    out << "# HELP corpcron_bytes_out_total Total outbound TCP bytes.\n";
    out << "# TYPE corpcron_bytes_out_total counter\n";
    out << "corpcron_bytes_out_total " << snapshot.bytes_out_total << "\n";
    out << "# HELP corpcron_task_success_total Total successful task executions.\n";
    out << "# TYPE corpcron_task_success_total counter\n";
    out << "corpcron_task_success_total " << snapshot.task_success_total << "\n";
    out << "# HELP corpcron_task_failure_total Total failed task executions.\n";
    out << "# TYPE corpcron_task_failure_total counter\n";
    out << "corpcron_task_failure_total " << snapshot.task_failure_total << "\n";
    out << "# HELP corpcron_lock_acquire_success_total Total successful Redis lock acquisitions.\n";
    out << "# TYPE corpcron_lock_acquire_success_total counter\n";
    out << "corpcron_lock_acquire_success_total " << snapshot.lock_acquire_success_total << "\n";
    out << "# HELP corpcron_lock_acquire_failure_total Total failed Redis lock acquisitions.\n";
    out << "# TYPE corpcron_lock_acquire_failure_total counter\n";
    out << "corpcron_lock_acquire_failure_total " << snapshot.lock_acquire_failure_total << "\n";
    out << "# HELP corpcron_max_task_duration_ms Maximum observed task duration in milliseconds.\n";
    out << "# TYPE corpcron_max_task_duration_ms gauge\n";
    out << "corpcron_max_task_duration_ms " << snapshot.max_task_duration_ms << "\n";
    out << "# HELP corpcron_task_duration_p95_ms P95 task execution duration in milliseconds over the recent sample window.\n";
    out << "# TYPE corpcron_task_duration_p95_ms gauge\n";
    out << "corpcron_task_duration_p95_ms " << snapshot.task_duration_p95_ms << "\n";
    out << "# HELP corpcron_task_duration_p99_ms P99 task execution duration in milliseconds over the recent sample window.\n";
    out << "# TYPE corpcron_task_duration_p99_ms gauge\n";
    out << "corpcron_task_duration_p99_ms " << snapshot.task_duration_p99_ms << "\n";
    out << "# HELP corpcron_task_duration_samples_total Total observed task duration samples.\n";
    out << "# TYPE corpcron_task_duration_samples_total counter\n";
    out << "corpcron_task_duration_samples_total " << snapshot.task_duration_samples_total << "\n";
    out << "# HELP corpcron_schedule_delay_max_ms Maximum observed scheduling delay in milliseconds.\n";
    out << "# TYPE corpcron_schedule_delay_max_ms gauge\n";
    out << "corpcron_schedule_delay_max_ms " << snapshot.schedule_delay_max_ms << "\n";
    out << "# HELP corpcron_schedule_delay_p95_ms P95 scheduling delay in milliseconds over the recent sample window.\n";
    out << "# TYPE corpcron_schedule_delay_p95_ms gauge\n";
    out << "corpcron_schedule_delay_p95_ms " << snapshot.schedule_delay_p95_ms << "\n";
    out << "# HELP corpcron_schedule_delay_p99_ms P99 scheduling delay in milliseconds over the recent sample window.\n";
    out << "# TYPE corpcron_schedule_delay_p99_ms gauge\n";
    out << "corpcron_schedule_delay_p99_ms " << snapshot.schedule_delay_p99_ms << "\n";
    out << "# HELP corpcron_schedule_delay_samples_total Total observed scheduling delay samples.\n";
    out << "# TYPE corpcron_schedule_delay_samples_total counter\n";
    out << "corpcron_schedule_delay_samples_total " << snapshot.schedule_delay_samples_total << "\n";
    return out.str();
}

} // namespace corpcron
