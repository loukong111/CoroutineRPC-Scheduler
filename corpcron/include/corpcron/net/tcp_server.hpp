#pragma once
#include "epoll_loop.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace corpcron {

class MySQLClient;
class RedisClient;

class TcpServer {
public:
    TcpServer(const std::string& addr, int port,
              std::shared_ptr<MySQLClient> db,
              std::shared_ptr<RedisClient> redis,
              size_t max_connections = 1024,
              const std::string& auth_token = "");
    ~TcpServer();

    bool start();
    void stop();

private:
    void handleAccept();

    std::string addr_;
    int port_;
    int listen_fd_ = -1;
    std::unique_ptr<EpollLoop> loop_;
    std::shared_ptr<MySQLClient> db_;
    std::shared_ptr<RedisClient> redis_;
    size_t max_connections_;
    std::string auth_token_;
    std::atomic<size_t> active_connections_{0};
};

} // namespace corpcron
