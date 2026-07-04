#include "corpcron/net/epoll_loop.hpp"
#include "corpcron/common/logger.hpp"
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>

namespace corpcron {

EpollLoop::EpollLoop() = default;
EpollLoop::~EpollLoop() {
    if (wake_fd_ != -1) close(wake_fd_);
    if (epoll_fd_ != -1) close(epoll_fd_);
}

bool EpollLoop::init() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        LOG_ERROR(std::string("epoll_create1 failed: ") + std::strerror(errno));
        return false;
    }
    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ == -1) {
        LOG_ERROR(std::string("eventfd failed: ") + std::strerror(errno));
        return false;
    }
    addFd(wake_fd_, EPOLLIN, [this](int fd, uint32_t) {
        uint64_t value;
        while (read(fd, &value, sizeof(value)) == sizeof(value)) {}
    });
    return true;
}

void EpollLoop::run() {
    running_ = true;
    while (running_) {
        int n = epoll_wait(epoll_fd_, events_, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            LOG_ERROR(std::string("epoll_wait failed: ") + std::strerror(errno));
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events_[i].data.fd;
            auto it = callbacks_.find(fd);
            if (it != callbacks_.end()) {
                it->second(fd, events_[i].events);
            }
            auto cit = coro_callbacks_.find(fd);
            if (cit != coro_callbacks_.end()) {
                auto cb = std::move(cit->second);
                delFd(fd);
                cb();
            }
        }
    }
}

void EpollLoop::stop() {
    running_ = false;
    if (wake_fd_ != -1) {
        uint64_t value = 1;
        ssize_t n = write(wake_fd_, &value, sizeof(value));
        (void)n;
    }
}

void EpollLoop::addFd(int fd, uint32_t events, EventCallback cb) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        LOG_ERROR(std::string("epoll_ctl add failed: ") + std::strerror(errno));
        return;
    }
    callbacks_[fd] = std::move(cb);
}

void EpollLoop::modFd(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void EpollLoop::delFd(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    callbacks_.erase(fd);
    coro_callbacks_.erase(fd);
}

void EpollLoop::addCoroutine(int fd, uint32_t events, CoroutineCallback cb) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        LOG_ERROR(std::string("epoll_ctl add coroutine failed: ") + std::strerror(errno));
        return;
    }
    coro_callbacks_[fd] = std::move(cb);
}

} // namespace corpcron
