#include "corpcron/common/thread_pool.hpp"
#include "corpcron/common/logger.hpp"
#include <algorithm>
#include <chrono>
#include <exception>

namespace corpcron {

DynamicThreadPool::DynamicThreadPool(size_t init_threads, size_t max_threads,
                                     size_t backlog_threshold, int idle_timeout_sec,
                                     size_t max_pending_tasks)
    : init_threads_(std::max<size_t>(1, init_threads)),
      max_threads_(std::max(init_threads_, max_threads)),
      backlog_threshold_(backlog_threshold), max_pending_tasks_(max_pending_tasks),
      idle_timeout_sec_(idle_timeout_sec),
      current_threads_(init_threads_) {
    if (idle_timeout_sec_ <= 0) idle_timeout_sec_ = 1;
    workers_.reserve(max_threads_);
    try {
        for (size_t i = 0; i < init_threads_; ++i) {
            startWorker(i);
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.thread.joinable()) worker.thread.join();
        }
        workers_.clear();
        throw;
    }
}

DynamicThreadPool::~DynamicThreadPool() {
    stop();
}

void DynamicThreadPool::stop() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return;
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.thread.joinable()) worker.thread.join();
    }
    workers_.clear();
    stopped_ = true;
}

bool DynamicThreadPool::enqueue(std::function<void()> task) {
    if (!task) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        if (max_pending_tasks_ > 0 && tasks_.size() >= max_pending_tasks_) return false;
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
    adjustPoolSize();
    return true;
}

void DynamicThreadPool::workerLoop(int thread_id,
                                   const std::shared_ptr<std::atomic<bool>>& finished) {
    (void)thread_id;
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (tasks_.empty()) {
                ++idle_count_;
                const bool ready = cv_.wait_for(
                    lock, std::chrono::seconds(idle_timeout_sec_),
                    [this]() { return stopping_ || !tasks_.empty(); });
                --idle_count_;
                if (!ready && idle_count_ > 0 && current_threads_ > init_threads_) {
                    --current_threads_;
                    break;
                }
            }
            if (stopping_ && tasks_.empty()) {
                if (current_threads_ > 0) --current_threads_;
                break;
            }
            if (tasks_.empty()) continue;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR_EVENT("thread_pool_task_failed", {{"error", e.what()}});
        } catch (...) {
            LOG_ERROR_EVENT("thread_pool_task_failed", {{"error", "unknown exception"}});
        }
    }
    finished->store(true, std::memory_order_release);
}

void DynamicThreadPool::startWorker(size_t thread_id) {
    auto finished = std::make_shared<std::atomic<bool>>(false);
    std::thread thread(&DynamicThreadPool::workerLoop, this, static_cast<int>(thread_id), finished);
    workers_.push_back(Worker{std::move(thread), std::move(finished)});
}

void DynamicThreadPool::adjustPoolSize() {
    std::vector<std::thread> finished_threads;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;

        auto it = workers_.begin();
        while (it != workers_.end()) {
            if (it->finished->load(std::memory_order_acquire)) {
                finished_threads.push_back(std::move(it->thread));
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }

        size_t pending = tasks_.size();
        if (pending > backlog_threshold_ && current_threads_ < max_threads_) {
            size_t new_threads = std::min(max_threads_, current_threads_ + 2);
            const size_t previous_threads = current_threads_;
            while (current_threads_ < new_threads) {
                try {
                    startWorker(current_threads_);
                    ++current_threads_;
                } catch (const std::exception& e) {
                    LOG_ERROR_EVENT("thread_pool_expand_failed", {{"error", e.what()}});
                    break;
                }
            }
            if (current_threads_ > previous_threads) {
                LOG_INFO("ThreadPool increased to " + std::to_string(current_threads_) + " threads");
            }
        }
    }
    for (auto& thread : finished_threads) {
        if (thread.joinable()) thread.join();
    }
}

size_t DynamicThreadPool::taskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

} // namespace corpcron
