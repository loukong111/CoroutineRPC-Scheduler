#pragma once
#include "epoll_loop.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace corpcron {

class RpcDispatcher;

class TcpServer {
public:
    TcpServer(const std::string& addr, int port,
              std::shared_ptr<RpcDispatcher> dispatcher,
              size_t max_connections = 1024);
    ~TcpServer();

    bool start();
    void stop();

private:
    void handleAccept();

    std::string addr_;
    int port_;
    int listen_fd_ = -1;
    std::unique_ptr<EpollLoop> loop_;
    std::shared_ptr<RpcDispatcher> dispatcher_;
    size_t max_connections_;
    std::atomic<size_t> active_connections_{0};
};

} // namespace corpcron
