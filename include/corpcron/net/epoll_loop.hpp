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
    bool run();
    void stop();

    bool addFd(int fd, uint32_t events, EventCallback cb);
    bool modFd(int fd, uint32_t events);
    void delFd(int fd);

    bool addCoroutine(int fd, uint32_t events, CoroutineCallback cb);
    bool isStopping() const;

private:
    void resumePendingCoroutines();

    int epoll_fd_ = -1;
    int wake_fd_ = -1;
    std::atomic<bool> stop_requested_{false};
    std::unordered_map<int, EventCallback> callbacks_;//普通同步回调，事件触发当场执行逻辑
    std::unordered_map<int, CoroutineCallback> coro_callbacks_;//协程唤醒入口，只负责恢复协程
    static constexpr int MAX_EVENTS = 64;
    struct epoll_event events_[MAX_EVENTS];
};

} // namespace corpcron
