#pragma once
#include "epoll_loop.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace corpcron {

class DynamicThreadPool;
class RpcDispatcher;

struct RpcExecutorOptions {
    size_t min_threads = 2;
    size_t max_threads = 8;
    size_t backlog_threshold = 8;
    int idle_timeout_sec = 5;
    size_t max_pending_requests = 256;
};

class TcpServer {
public:
    TcpServer(const std::string& addr, int port,
              std::shared_ptr<RpcDispatcher> dispatcher,
              size_t max_connections = 1024,
              RpcExecutorOptions executor_options = {});
    ~TcpServer();

    bool start(const std::function<bool()>& on_listening = {});
    void stop();

private:
    void handleAccept();
    void closeListenSocket();

    std::string addr_;
    int port_;
    int listen_fd_ = -1;
    std::unique_ptr<EpollLoop> loop_;
    std::shared_ptr<RpcDispatcher> dispatcher_;
    std::unique_ptr<DynamicThreadPool> request_executor_;
    size_t max_connections_;
    std::atomic<size_t> active_connections_{0};
};

} // namespace corpcron
