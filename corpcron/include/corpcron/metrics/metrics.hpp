#pragma once

#include <atomic>
#include <cstdint>

namespace corpcron {

struct MetricsSnapshot {
    uint64_t rpc_requests_total = 0;
    uint64_t rpc_success_total = 0;
    uint64_t rpc_error_total = 0;
    uint64_t active_connections = 0;
    uint64_t rejected_connections = 0;
    uint64_t malformed_frames = 0;
    uint64_t bytes_in_total = 0;
    uint64_t bytes_out_total = 0;
    uint64_t task_success_total = 0;
    uint64_t task_failure_total = 0;
    uint64_t lock_acquire_success_total = 0;
    uint64_t lock_acquire_failure_total = 0;
    uint64_t max_task_duration_ms = 0;
};

class Metrics {
public:
    static Metrics& instance();

    void incRpcRequest();
    void incRpcSuccess();
    void incRpcError();
    void incActiveConnection();
    void decActiveConnection();
    void incRejectedConnection();
    void incMalformedFrame();
    void addBytesIn(uint64_t bytes);
    void addBytesOut(uint64_t bytes);
    void incTaskSuccess();
    void incTaskFailure();
    void incLockAcquireSuccess();
    void incLockAcquireFailure();
    void observeTaskDuration(uint64_t duration_ms);

    MetricsSnapshot snapshot() const;

private:
    Metrics() = default;

    std::atomic<uint64_t> rpc_requests_total_{0};
    std::atomic<uint64_t> rpc_success_total_{0};
    std::atomic<uint64_t> rpc_error_total_{0};
    std::atomic<uint64_t> active_connections_{0};
    std::atomic<uint64_t> rejected_connections_{0};
    std::atomic<uint64_t> malformed_frames_{0};
    std::atomic<uint64_t> bytes_in_total_{0};
    std::atomic<uint64_t> bytes_out_total_{0};
    std::atomic<uint64_t> task_success_total_{0};
    std::atomic<uint64_t> task_failure_total_{0};
    std::atomic<uint64_t> lock_acquire_success_total_{0};
    std::atomic<uint64_t> lock_acquire_failure_total_{0};
    std::atomic<uint64_t> max_task_duration_ms_{0};
};

} // namespace corpcron
