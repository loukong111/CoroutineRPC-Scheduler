#include "corpcron/net/epoll_loop.hpp"
#include "corpcron/common/logger.hpp"
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>
#include <exception>
#include <utility>
#include <vector>

namespace corpcron {

EpollLoop::EpollLoop() = default;
EpollLoop::~EpollLoop() {
    if (wake_fd_ != -1) close(wake_fd_);
    if (epoll_fd_ != -1) close(epoll_fd_);
}

bool EpollLoop::init() {
    if (epoll_fd_ != -1) return !stop_requested_.load(std::memory_order_acquire);
    if (stop_requested_.load(std::memory_order_acquire)) return false;
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        LOG_ERROR(std::string("epoll_create1 failed: ") + std::strerror(errno));
        return false;
    }
    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ == -1) {
        LOG_ERROR(std::string("eventfd failed: ") + std::strerror(errno));
        close(epoll_fd_);
        epoll_fd_ = -1;
        return false;
    }
    if (!addFd(wake_fd_, EPOLLIN, [this](int fd, uint32_t) {
        uint64_t value;
        while (read(fd, &value, sizeof(value)) == sizeof(value)) {}
    })) {
        close(wake_fd_);
        wake_fd_ = -1;
        close(epoll_fd_);
        epoll_fd_ = -1;
        return false;
    }
    return true;
}

bool EpollLoop::run() {
    if (epoll_fd_ == -1 || stop_requested_.load(std::memory_order_acquire)) {
        resumePendingCoroutines();
        return epoll_fd_ != -1;
    }
    bool success = true;
    while (!stop_requested_.load(std::memory_order_acquire)) {
        int n = epoll_wait(epoll_fd_, events_, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            LOG_ERROR(std::string("epoll_wait failed: ") + std::strerror(errno));
            success = false;
            break;
        }
        for (int i = 0; i < n; ++i) {
            try {
                int fd = events_[i].data.fd;
                auto it = callbacks_.find(fd);
                if (it != callbacks_.end()) {
                    // The callback is allowed to remove its own fd. Copy it first so
                    // erasing the map entry cannot destroy the active callable.
                    auto callback = it->second;
                    callback(fd, events_[i].events);
                }
                auto cit = coro_callbacks_.find(fd);
                if (cit != coro_callbacks_.end()) {
                    auto cb = std::move(cit->second);
                    delFd(fd);
                    cb();
                }
            } catch (const std::exception& e) {
                LOG_ERROR(std::string("epoll callback failed: ") + e.what());
                success = false;
                break;
            } catch (...) {
                LOG_ERROR("epoll callback failed: unknown exception");
                success = false;
                break;
            }
        }
        if (!success) break;
    }
    resumePendingCoroutines();
    return success;
}

void EpollLoop::stop() {
    if (stop_requested_.exchange(true, std::memory_order_acq_rel)) return;
    if (wake_fd_ != -1) {
        uint64_t value = 1;
        ssize_t n = write(wake_fd_, &value, sizeof(value));
        (void)n;
    }
}

bool EpollLoop::addFd(int fd, uint32_t events, EventCallback cb) {
    if (epoll_fd_ == -1 || fd < 0 || stop_requested_.load(std::memory_order_acquire)) {
        return false;
    }
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        LOG_ERROR(std::string("epoll_ctl add failed: ") + std::strerror(errno));
        return false;
    }
    callbacks_[fd] = std::move(cb);
    return true;
}

bool EpollLoop::modFd(int fd, uint32_t events) {
    if (epoll_fd_ == -1 || fd < 0 || stop_requested_.load(std::memory_order_acquire)) {
        return false;
    }
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        LOG_ERROR(std::string("epoll_ctl mod failed: ") + std::strerror(errno));
        return false;
    }
    return true;
}

void EpollLoop::delFd(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    callbacks_.erase(fd);
    coro_callbacks_.erase(fd);
}

bool EpollLoop::addCoroutine(int fd, uint32_t events, CoroutineCallback cb) {
    if (epoll_fd_ == -1 || fd < 0 || stop_requested_.load(std::memory_order_acquire)) {
        return false;
    }
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        LOG_ERROR(std::string("epoll_ctl add coroutine failed: ") + std::strerror(errno));
        return false;
    }
    coro_callbacks_[fd] = std::move(cb);
    return true;
}

bool EpollLoop::isStopping() const {
    return stop_requested_.load(std::memory_order_acquire);
}

void EpollLoop::resumePendingCoroutines() {
    std::vector<CoroutineCallback> pending;
    pending.reserve(coro_callbacks_.size());
    for (auto& [fd, callback] : coro_callbacks_) {
        if (epoll_fd_ != -1) epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        pending.push_back(std::move(callback));
    }
    coro_callbacks_.clear();
    for (auto& callback : pending) {
        if (callback) callback();
    }
}

} // namespace corpcron
