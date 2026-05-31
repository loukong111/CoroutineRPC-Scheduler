#include "corpcron/coroutine/scheduler.hpp"
#include "corpcron/coroutine/task.hpp"
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <coroutine>

namespace corpcron {

Scheduler& Scheduler::instance() {
    static Scheduler sched;
    return sched;
}

bool Scheduler::init() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) return false;
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd_ == -1) return false;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &ev) == -1) return false;
    return true;
}

void Scheduler::run() {
    running_ = true;
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];
    while (running_) {
        // 执行就绪队列中的协程
        while (true) {
            std::coroutine_handle<> h;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (ready_queue_.empty()) break;
                h = ready_queue_.front();
                ready_queue_.pop();
            }
            if (h) h.resume();
        }

        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 10);
        if (n == -1) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == timer_fd_) {
                processTimers();
            }
        }
    }
}

void Scheduler::stop() {
    running_ = false;
}

void Scheduler::schedule(std::coroutine_handle<> h) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    ready_queue_.push(h);
}

void Scheduler::addTimer(uint64_t ms, std::coroutine_handle<> h) {
    // 使用 timerfd 设置单次超时（简化版，只支持一个定时器）
    struct itimerspec new_value;
    new_value.it_value.tv_sec = ms / 1000;
    new_value.it_value.tv_nsec = (ms % 1000) * 1000000;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_nsec = 0;
    timerfd_settime(timer_fd_, 0, &new_value, nullptr);
    uint64_t id = ++next_timer_id_;
    timer_map_[id] = h;
}

void Scheduler::processTimers() {
    uint64_t exp;
    read(timer_fd_, &exp, sizeof(exp));
    // 唤醒所有定时器协程（简化版）
    for (auto& [id, h] : timer_map_) {
        if (h) schedule(h);
    }
    timer_map_.clear();
}

void TimeoutAwaitable::await_suspend(std::coroutine_handle<> h) {
    waiter_ = h;
    scheduler_->addTimer(ms_, h);
}

} // namespace corpcron