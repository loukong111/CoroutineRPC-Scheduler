#include "corpcron/net/tcp_server.hpp"
#include "corpcron/coroutine/task.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/rpc/protocol.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <atomic>
#include <utility>

namespace corpcron {

namespace {

std::string errnoMessage(const std::string& action) {
    return action + ": " + std::strerror(errno);
}

} // namespace

Task clientHandler(int fd, EpollLoop* loop,
                   std::shared_ptr<RpcDispatcher> dispatcher,
                   std::atomic<size_t>* active_connections) {
    std::string read_buffer;
    bool closed = false;
    while (true) {
        co_await SocketAwaitable(fd, EPOLLIN, loop);
        char chunk[4096];
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (n == 0) LOG_INFO("Client closed");
            else if (errno != EAGAIN) LOG_ERROR(errnoMessage("read"));
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
                LOG_WARN("Invalid RPC frame, closing connection");
                closed = true;
                break;
            }
            read_buffer.erase(0, frame_size);

            RpcResponse rpc_response = dispatcher->dispatch(serial_id, payload);

            std::string response;
            if (!rpc::tryEncode(rpc_response.serial_id, rpc_response.payload, response)) {
                rpc_response = RpcDispatcher::error(corpcron::rpc::PAYLOAD_TOO_LARGE, "Response payload too large");
                if (!rpc::tryEncode(rpc_response.serial_id, rpc_response.payload, response)) {
                    closed = true;
                    break;
                }
            }
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
                LOG_ERROR(errnoMessage("send"));
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
                     std::shared_ptr<RpcDispatcher> dispatcher,
                     size_t max_connections)
    : addr_(addr), port_(port), loop_(std::make_unique<EpollLoop>()),
      dispatcher_(std::move(dispatcher)), max_connections_(max_connections) {}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
    if (!loop_->init()) return false;
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ == -1) { LOG_ERROR(errnoMessage("socket")); return false; }
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, addr_.c_str(), &addr.sin_addr);
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG_ERROR(errnoMessage("bind"));
        close(listen_fd_);
        return false;
    }
    if (listen(listen_fd_, 128) == -1) {
        LOG_ERROR(errnoMessage("listen"));
        close(listen_fd_); 
        return false;
    }
    loop_->addFd(listen_fd_, EPOLLIN, [this](int, uint32_t) { handleAccept(); });
    LOG_INFO("TcpServer listening on " + addr_ + ":" + std::to_string(port_));
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
            LOG_ERROR(errnoMessage("accept")); break;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        if (active_connections_.load() >= max_connections_) {
            LOG_WARN("Rejecting connection from " + std::string(ip) + ": connection limit reached");
            close(client_fd);
            continue;
        }
        ++active_connections_;
        LOG_INFO("New connection from " + std::string(ip) + ":" + std::to_string(ntohs(client_addr.sin_port)));
        Task::spawn(clientHandler(client_fd, loop_.get(), dispatcher_, &active_connections_));
    }
}

} // namespace corpcron
