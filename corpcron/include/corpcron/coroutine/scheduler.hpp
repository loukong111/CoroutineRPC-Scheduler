#pragma once
#include <coroutine>
#include <cstdint>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <functional>

namespace corpcron {

class Scheduler {
public:
    static Scheduler& instance();
    bool init();
    void run();
    void stop();
    void schedule(std::coroutine_handle<> h);
    void addTimer(uint64_t ms, std::coroutine_handle<> h);

private:
    Scheduler() = default;
    int epoll_fd_ = -1;
    int timer_fd_ = -1;
    std::atomic<bool> running_{false};
    std::queue<std::coroutine_handle<>> ready_queue_;
    std::mutex queue_mutex_;
    std::unordered_map<uint64_t, std::coroutine_handle<>> timer_map_;
    uint64_t next_timer_id_ = 0;

    void processTimers();
};

struct TimeoutAwaitable {
    uint64_t ms_;
    Scheduler* scheduler_;
    std::coroutine_handle<> waiter_;

    TimeoutAwaitable(uint64_t ms, Scheduler* scheduler)
        : ms_(ms), scheduler_(scheduler), waiter_(nullptr) {}

    bool await_ready() const { return false; }
    void await_suspend(std::coroutine_handle<> h);
    void await_resume() {}
};

} // namespace corpcron
