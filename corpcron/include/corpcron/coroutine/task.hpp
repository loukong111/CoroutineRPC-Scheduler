#pragma once
#include <coroutine>
#include <utility>
#include <exception>
#include "corpcron/net/epoll_loop.hpp"

namespace corpcron {

class Scheduler; // 暂时未使用，保留

struct Task {
    struct promise_type {
        Task* owner = nullptr;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                Task* owner = h.promise().owner;
                if (owner && owner->detached_) {
                    owner->detached_ = false;
                    delete owner;
                }
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {
        if (handle) handle.promise().owner = this;
    }
    ~Task() { if (handle) handle.destroy(); }
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)), detached_(other.detached_) {
        if (handle) handle.promise().owner = this;
        other.detached_ = false;
    }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, nullptr);
            detached_ = other.detached_;
            if (handle) handle.promise().owner = this;
            other.detached_ = false;
        }
        return *this;
    }
    void detach() { detached_ = true; }

private:
    bool detached_ = false;
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
        loop_->addCoroutine(fd_, events_, [h]() mutable {
            if (h && !h.done()) h.resume();
        });
    }
    void await_resume() {}
};

} // namespace corpcron
