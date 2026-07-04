#include "corpcron/metrics/metrics.hpp"

namespace corpcron {

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
    uint64_t current = max_task_duration_ms_.load(std::memory_order_relaxed);
    while (duration_ms > current &&
           !max_task_duration_ms_.compare_exchange_weak(current, duration_ms, std::memory_order_relaxed)) {}
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
    return snapshot;
}

} // namespace corpcron
