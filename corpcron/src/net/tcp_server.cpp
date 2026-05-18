#include "corpcron/net/tcp_server.hpp"
#include "corpcron/coroutine/task.hpp"
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "rpc.pb.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <random>

namespace corpcron {

static std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid(36, '-');
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        uuid[i] = hex[dis(gen)];
    }
    return uuid;
}

Task clientHandler(int fd, EpollLoop* loop,
                   std::shared_ptr<MySQLClient> db,
                   std::shared_ptr<RedisClient> redis) {
    std::string read_buffer;
    while (true) {
        co_await SocketAwaitable(fd, EPOLLIN, loop);
        char chunk[4096];
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (n == 0) std::cout << "Client closed" << std::endl;
            else if (errno != EAGAIN) perror("read");
            break;
        }
        read_buffer.append(chunk, n);
        while (true) {
            uint32_t serial_id;
            std::string payload;
            if (!rpc::decode(read_buffer.data(), read_buffer.size(), serial_id, payload))
                break;
            uint32_t total_len = 4 + 4 + payload.size();
            read_buffer.erase(0, 4 + total_len);

            std::string response_data;
            uint32_t response_id = 0;

            if (serial_id == 1) { // Echo
                corpcron::rpc::EchoRequest req;
                if (req.ParseFromString(payload)) {
                    std::string result = HandlerRegistry::instance().execute("Echo", req.message());
                    corpcron::rpc::EchoResponse resp;
                    resp.set_message(result);
                    resp.SerializeToString(&response_data);
                    response_id = 2;
                }
            } else if (serial_id == 3) { // SubmitTask
                corpcron::rpc::SubmitTaskRequest req;
                if (req.ParseFromString(payload)) {
                    TaskMeta task;
                    task.id = generate_uuid();
                    task.cron_expr = req.cron_expr();
                    task.params = req.params();
                    task.handler = req.handler();
                    task.status = 1;
                    if (db->addTask(task)) {
                        corpcron::rpc::SubmitTaskResponse resp;
                        resp.set_task_id(task.id);
                        resp.set_success(true);
                        resp.SerializeToString(&response_data);
                        response_id = 4;
                    } else {
                        corpcron::rpc::SubmitTaskResponse resp;
                        resp.set_success(false);
                        resp.set_error("DB insert failed");
                        resp.SerializeToString(&response_data);
                        response_id = 4;
                    }
                } else {
                    corpcron::rpc::SubmitTaskResponse resp;
                    resp.set_success(false);
                    resp.set_error("Parse error");
                    resp.SerializeToString(&response_data);
                    response_id = 4;
                }
            }else if (serial_id == 5) { // ExecuteTaskRequest
                corpcron::rpc::ExecuteTaskRequest req;
                if (req.ParseFromString(payload)) {
                    std::string result;
                    std::string error;
                    try {
                        result = HandlerRegistry::instance().execute(req.handler(), req.params());
                    } catch (const std::exception& e) {
                        error = e.what();
                        result = "Exception: " + error;
                    }
                    corpcron::rpc::ExecuteTaskResponse resp;
                    resp.set_success(error.empty());
                    resp.set_result(result);
                    resp.set_error(error);
                    resp.SerializeToString(&response_data);
                    response_id = 6;
                } else {
                    response_data = "Parse ExecuteTaskRequest failed";
                }
            } else {
                response_data = "Unknown serial_id";
            }

            std::string response = rpc::encode(response_id, response_data);
            write(fd, response.c_str(), response.size());
        }
    }
    close(fd);
    co_return;
}

TcpServer::TcpServer(const std::string& addr, int port,
                     std::shared_ptr<MySQLClient> db,
                     std::shared_ptr<RedisClient> redis)
    : addr_(addr), port_(port), db_(db), redis_(redis),
      loop_(std::make_unique<EpollLoop>()) {}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
    if (!loop_->init()) return false;
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ == -1) { perror("socket"); return false; }
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, addr_.c_str(), &addr.sin_addr);
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind"); close(listen_fd_); return false;
    }
    if (listen(listen_fd_, 128) == -1) {
        perror("listen"); close(listen_fd_); return false;
    }
    loop_->addFd(listen_fd_, EPOLLIN, [this](int, uint32_t) { handleAccept(); });
    std::cout << "TcpServer listening on " << addr_ << ":" << port_ << std::endl;
    loop_->run();
    return true;
}

void TcpServer::stop() {
    if (loop_) loop_->stop();
    if (listen_fd_ != -1) { close(listen_fd_); listen_fd_ = -1; }
}

void TcpServer::handleAccept() {
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept4(listen_fd_, (struct sockaddr*)&client_addr, &len, SOCK_NONBLOCK);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept"); break;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        std::cout << "New connection from " << ip << ":" << ntohs(client_addr.sin_port) << std::endl;
        auto* task = new Task(clientHandler(client_fd, loop_.get(), db_, redis_));
        (void)task; // 实际项目应管理生命周期，此处简化
    }
}

} // namespace corpcron