#pragma once
#include <coroutine>
#include <utility>
#include <exception>
#include "corpcron/net/epoll_loop.hpp"

namespace corpcron {

class Scheduler; // 暂时未使用，保留

struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    std::coroutine_handle<promise_type> handle;
    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }
};

struct SocketAwaitable {
    int fd_;
    uint32_t events_;
    EpollLoop* loop_;
    std::coroutine_handle<> waiter_;

    SocketAwaitable(int fd, uint32_t events, EpollLoop* loop)
        : fd_(fd), events_(events), loop_(loop), waiter_(nullptr) {}

    bool await_ready() const { return false; }
    void await_suspend(std::coroutine_handle<> h) {
        waiter_ = h;
        loop_->addCoroutine(fd_, events_, [this]() {
            if (waiter_) waiter_.resume();
        });
    }
    void await_resume() {}
};

} // namespace corpcron