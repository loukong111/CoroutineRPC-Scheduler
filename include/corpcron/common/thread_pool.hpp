#pragma once
#include <atomic>
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace corpcron {

class DynamicThreadPool {
public:
    DynamicThreadPool(size_t init_threads = 4, size_t max_threads = 32,
                      size_t backlog_threshold = 100, int idle_timeout_sec = 5,
                      size_t max_pending_tasks = 0);
    ~DynamicThreadPool();

    bool enqueue(std::function<void()> task);
    void stop();

    size_t taskCount() const;

private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    void workerLoop(int thread_id, const std::shared_ptr<std::atomic<bool>>& finished);
    void startWorker(size_t thread_id);
    void adjustPoolSize();

    std::vector<Worker> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::mutex lifecycle_mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
    bool stopped_ = false;
    size_t idle_count_ = 0;
    size_t init_threads_;
    size_t max_threads_;
    size_t backlog_threshold_;
    size_t max_pending_tasks_;
    int idle_timeout_sec_;
    size_t current_threads_;
};

} // namespace corpcron
