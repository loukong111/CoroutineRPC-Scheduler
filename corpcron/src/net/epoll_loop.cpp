#include "corpcron/net/epoll_loop.hpp"
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace corpcron {

EpollLoop::EpollLoop() = default;
EpollLoop::~EpollLoop() {
    if (epoll_fd_ != -1) close(epoll_fd_);
}

bool EpollLoop::init() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        perror("epoll_create1");
        return false;
    }
    return true;
}

void EpollLoop::run() {
    running_ = true;
    while (running_) {
        int n = epoll_wait(epoll_fd_, events_, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
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
}

void EpollLoop::addFd(int fd, uint32_t events, EventCallback cb) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl add");
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
        perror("epoll_ctl add coro");
        return;
    }
    coro_callbacks_[fd] = std::move(cb);
}

} // namespace corpcron
