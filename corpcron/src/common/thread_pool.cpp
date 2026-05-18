#include "corpcron/common/thread_pool.hpp"
#include <iostream>
#include <chrono>

namespace corpcron {

DynamicThreadPool::DynamicThreadPool(size_t init_threads, size_t max_threads,
                                     size_t backlog_threshold, int idle_timeout_sec)
    : init_threads_(init_threads), max_threads_(max_threads),
      backlog_threshold_(backlog_threshold), idle_timeout_sec_(idle_timeout_sec),
      current_threads_(init_threads) {
    for (size_t i = 0; i < init_threads_; ++i) {
        workers_.emplace_back(&DynamicThreadPool::workerLoop, this, i);
    }
}

DynamicThreadPool::~DynamicThreadPool() {
    stop();
}

void DynamicThreadPool::stop() {
    stop_ = true;
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void DynamicThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
    adjustPoolSize();
}

void DynamicThreadPool::workerLoop(int thread_id) {
    while (!stop_) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (tasks_.empty()) {
                ++idle_count_;
                auto status = cv_.wait_for(lock, std::chrono::seconds(idle_timeout_sec_));
                --idle_count_;
                if (status == std::cv_status::timeout && idle_count_ > 0 && current_threads_ > init_threads_) {
                    // 超时且当前线程数大于初始值，当前线程退出
                    break;
                }
                if (tasks_.empty()) continue;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        if (task) task();
    }
}

void DynamicThreadPool::adjustPoolSize() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t pending = tasks_.size();
    if (pending > backlog_threshold_ && current_threads_ < max_threads_) {
        size_t new_threads = std::min(max_threads_, current_threads_ + 2);
        for (size_t i = current_threads_; i < new_threads; ++i) {
            workers_.emplace_back(&DynamicThreadPool::workerLoop, this, i);
        }
        current_threads_ = new_threads;
        std::cout << "ThreadPool: increased to " << current_threads_ << " threads\n";
    }
}

size_t DynamicThreadPool::taskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

} // namespace corpcron