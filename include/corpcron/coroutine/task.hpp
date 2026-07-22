#pragma once
#include <coroutine>
#include <utility>
#include <exception>
#include <string>
#include "corpcron/common/logger.hpp"
#include "corpcron/net/epoll_loop.hpp"

namespace corpcron {

struct Task {
    struct promise_type {
        bool detached = false;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().detached) h.destroy();
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() noexcept {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& e) {
                LOG_ERROR(std::string("Unhandled coroutine exception: ") + e.what());
            } catch (...) {
                LOG_ERROR("Unhandled coroutine exception: unknown exception");
            }
        }
    };

    std::coroutine_handle<promise_type> handle;
    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    static void spawn(Task task) {
        auto owned = std::exchange(task.handle, nullptr);
        if (!owned) return;
        if (owned.done()) {
            owned.destroy();
            return;
        }
        owned.promise().detached = true;
    }
};

struct SocketAwaitable {
    int fd_;
    uint32_t events_;
    EpollLoop* loop_;
    bool registered_ = false;

    SocketAwaitable(int fd, uint32_t events, EpollLoop* loop)
        : fd_(fd), events_(events), loop_(loop) {}

    bool await_ready() const { return false; }
    bool await_suspend(std::coroutine_handle<> h) {
        registered_ = loop_ && loop_->addCoroutine(fd_, events_, [h]() mutable {
            if (h && !h.done()) h.resume();
        });
        return registered_;
    }
    bool await_resume() const { return registered_ && loop_ && !loop_->isStopping(); }
};

} // namespace corpcron
