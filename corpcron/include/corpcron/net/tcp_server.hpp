#pragma once
#include "epoll_loop.hpp"
#include <memory>
#include <string>

namespace corpcron {

// 前向声明
class MySQLClient;
class RedisClient;

class TcpServer {
public:
    TcpServer(const std::string& addr, int port,
              std::shared_ptr<MySQLClient> db,
              std::shared_ptr<RedisClient> redis);
    ~TcpServer();

    bool start();
    void stop();

private:
    void handleAccept();
    // 不再使用 handleClient，改为协程处理，但保留声明用于兼容（实际不使用）
    // void handleClient(int client_fd, uint32_t events);

    std::string addr_;
    int port_;
    int listen_fd_ = -1;
    std::unique_ptr<EpollLoop> loop_;
    std::shared_ptr<MySQLClient> db_;
    std::shared_ptr<RedisClient> redis_;
};

} // namespace corpcron