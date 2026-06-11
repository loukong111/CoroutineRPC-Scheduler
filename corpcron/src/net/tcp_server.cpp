#include "corpcron/net/tcp_server.hpp"
#include "corpcron/coroutine/task.hpp"
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/scheduler/cron_parser.hpp"
#include "rpc.pb.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <random>
#include <chrono>
#include <ctime>
#include <atomic>

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

static std::string to_datetime_string(uint64_t timestamp_ms) {
    time_t time_sec = timestamp_ms / 1000;
    std::tm tm_buf;
    localtime_r(&time_sec, &tm_buf);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

static std::string make_rpc_error(corpcron::rpc::ErrorCode code, const std::string& message) {
    corpcron::rpc::RpcError error;
    error.set_code(code);
    error.set_message(message);
    std::string data;
    error.SerializeToString(&data);
    return data;
}

static bool authorized(const std::string& configured_token, const std::string& request_token) {
    return configured_token.empty() || configured_token == request_token;
}

Task clientHandler(int fd, EpollLoop* loop,
                   std::shared_ptr<MySQLClient> db,
                   std::shared_ptr<RedisClient> redis,
                   std::string auth_token,
                   std::atomic<size_t>* active_connections) {
    std::string read_buffer;
    bool closed = false;
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
            size_t frame_size = 0;
            rpc::DecodeStatus status = rpc::tryDecodeFrame(read_buffer.data(), read_buffer.size(),
                                                           serial_id, payload, frame_size);
            if (status == rpc::DecodeStatus::Incomplete)
                break;
            if (status == rpc::DecodeStatus::Malformed || status == rpc::DecodeStatus::TooLarge) {
                std::cerr << "Invalid RPC frame, closing connection" << std::endl;
                closed = true;
                break;
            }
            read_buffer.erase(0, frame_size);

            std::string response_data;
            uint32_t response_id = 0;

            if (serial_id == 1) { // Echo
                corpcron::rpc::EchoRequest req;
                if (req.ParseFromString(payload)) {
                    if (!authorized(auth_token, req.auth_token())) {
                        response_data = make_rpc_error(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
                        response_id = rpc::kRpcErrorSerialId;
                    } else {
                        std::string result = HandlerRegistry::instance().execute("Echo", req.message());
                        corpcron::rpc::EchoResponse resp;
                        resp.set_message(result);
                        resp.SerializeToString(&response_data);
                        response_id = 2;
                    }
                } else {
                    response_data = make_rpc_error(corpcron::rpc::BAD_REQUEST, "Parse EchoRequest failed");
                    response_id = rpc::kRpcErrorSerialId;
                }
            } else if (serial_id == 3) { // SubmitTask
                corpcron::rpc::SubmitTaskRequest req;
                if (req.ParseFromString(payload)) {
                    if (!authorized(auth_token, req.auth_token())) {
                        response_data = make_rpc_error(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
                        response_id = rpc::kRpcErrorSerialId;
                    } else {
                    TaskMeta task;
                    task.id = generate_uuid();
                    task.cron_expr = req.cron_expr();
                    task.params = req.params();
                    task.handler = req.handler();
                    task.status = 1;
                    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    uint64_t next_run_ms = CronParser::nextExecution(task.cron_expr, now_ms);
                    if (next_run_ms == 0) {
                        corpcron::rpc::SubmitTaskResponse resp;
                        resp.set_success(false);
                        resp.set_error("Invalid cron expression");
                        resp.SerializeToString(&response_data);
                        response_id = 4;
                    } else {
                        task.next_run_at = to_datetime_string(next_run_ms);
                    }
                    if (response_id == 0) {
                        corpcron::rpc::SubmitTaskResponse resp;
                        if (db->addTask(task)) {
                            resp.set_task_id(task.id);
                            resp.set_success(true);
                        } else {
                            resp.set_success(false);
                            resp.set_error("DB insert failed");
                            response_data = make_rpc_error(corpcron::rpc::DB_ERROR, "DB insert failed");
                            response_id = rpc::kRpcErrorSerialId;
                        }
                        if (response_id == 0) {
                            resp.SerializeToString(&response_data);
                            response_id = 4;
                        }
                    }
                    }
                } else {
                    response_data = make_rpc_error(corpcron::rpc::BAD_REQUEST, "Parse SubmitTaskRequest failed");
                    response_id = rpc::kRpcErrorSerialId;
                }
            }else if (serial_id == 5) { // ExecuteTaskRequest
                corpcron::rpc::ExecuteTaskRequest req;
                if (req.ParseFromString(payload)) {
                    if (!authorized(auth_token, req.auth_token())) {
                        response_data = make_rpc_error(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
                        response_id = rpc::kRpcErrorSerialId;
                    } else {
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
                    }
                } else {
                    response_data = make_rpc_error(corpcron::rpc::BAD_REQUEST, "Parse ExecuteTaskRequest failed");
                    response_id = rpc::kRpcErrorSerialId;
                }
            } else if (serial_id == 7) { // CancelTaskRequest
                corpcron::rpc::CancelTaskRequest req;
                if (req.ParseFromString(payload)) {
                    if (!authorized(auth_token, req.auth_token())) {
                        response_data = make_rpc_error(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
                        response_id = rpc::kRpcErrorSerialId;
                    } else {
                        corpcron::rpc::CancelTaskResponse resp;
                        if (db->cancelTask(req.task_id())) {
                            resp.set_success(true);
                        } else {
                            resp.set_success(false);
                            resp.set_error("Task not found");
                        }
                        resp.SerializeToString(&response_data);
                        response_id = 8;
                    }
                } else {
                    response_data = make_rpc_error(corpcron::rpc::BAD_REQUEST, "Parse CancelTaskRequest failed");
                    response_id = rpc::kRpcErrorSerialId;
                }
            } else {
                response_data = make_rpc_error(corpcron::rpc::UNKNOWN_METHOD, "Unknown serial_id: " + std::to_string(serial_id));
                response_id = rpc::kRpcErrorSerialId;
            }

            std::string response = rpc::encode(response_id, response_data);
            size_t sent = 0;
            while (sent < response.size()) {
                ssize_t w = send(fd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
                if (w > 0) {
                    sent += static_cast<size_t>(w);
                    continue;
                }
                if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    co_await SocketAwaitable(fd, EPOLLOUT, loop);
                    continue;
                }
                if (w < 0 && errno == EINTR) continue;
                perror("send");
                closed = true;
                break;
            }
            if (closed) break;
        }
        if (closed) break;
    }
    loop->delFd(fd);
    close(fd);
    if (active_connections) --(*active_connections);
    co_return;
}

TcpServer::TcpServer(const std::string& addr, int port,
                     std::shared_ptr<MySQLClient> db,
                     std::shared_ptr<RedisClient> redis,
                     size_t max_connections,
                     const std::string& auth_token)
    : addr_(addr), port_(port), loop_(std::make_unique<EpollLoop>()),
      db_(db), redis_(redis), max_connections_(max_connections), auth_token_(auth_token) {}

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
        perror("bind"); 
        close(listen_fd_);
        return false;
    }
    if (listen(listen_fd_, 128) == -1) {
        perror("listen"); 
        close(listen_fd_); 
        return false;
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
        if (active_connections_.load() >= max_connections_) {
            std::cerr << "Rejecting connection from " << ip << ": connection limit reached" << std::endl;
            close(client_fd);
            continue;
        }
        ++active_connections_;
        std::cout << "New connection from " << ip << ":" << ntohs(client_addr.sin_port) << std::endl;
        auto* task = new Task(clientHandler(client_fd, loop_.get(), db_, redis_, auth_token_, &active_connections_));
        task->detach();
    }
}

} // namespace corpcron
