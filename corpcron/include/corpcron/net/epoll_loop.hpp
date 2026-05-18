#pragma once
#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <memory>

namespace corpcron {

class EpollLoop {
public:
    using EventCallback = std::function<void(int fd, uint32_t events)>;
    using CoroutineCallback = std::function<void()>;

    EpollLoop();
    ~EpollLoop();

    bool init();
    void run();
    void stop();

    void addFd(int fd, uint32_t events, EventCallback cb);
    void modFd(int fd, uint32_t events);
    void delFd(int fd);

    // 为协程注册 fd 事件
    void addCoroutine(int fd, uint32_t events, CoroutineCallback cb);

private:
    int epoll_fd_ = -1;
    std::atomic<bool> running_{false};
    std::unordered_map<int, EventCallback> callbacks_;
    std::unordered_map<int, CoroutineCallback> coro_callbacks_;
    static constexpr int MAX_EVENTS = 64;
    struct epoll_event events_[MAX_EVENTS];
};

} // namespace corpcron