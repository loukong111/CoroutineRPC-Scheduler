#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace corpcron {

class DynamicThreadPool {
public:
    DynamicThreadPool(size_t init_threads = 4, size_t max_threads = 32,
                      size_t backlog_threshold = 100, int idle_timeout_sec = 5);
    ~DynamicThreadPool();

    void enqueue(std::function<void()> task);
    void stop();
    void waitForAll();

    size_t taskCount() const;

private:
    void workerLoop(int thread_id);
    void adjustPoolSize();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> idle_count_{0};
    size_t init_threads_;
    size_t max_threads_;
    size_t backlog_threshold_;
    int idle_timeout_sec_;
    size_t current_threads_;
};

} // namespace corpcron