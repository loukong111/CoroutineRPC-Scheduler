#include "corpcron/net/tcp_server.hpp"
#include "corpcron/coroutine/task.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/metrics/metrics.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/rpc/rpc_interceptor.hpp"
#include "corpcron/rpc/protocol.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <atomic>
#include <chrono>
#include <utility>

namespace corpcron {

namespace {

std::string errnoMessage(const std::string& action) {
    return action + ": " + std::strerror(errno);
}

std::string nextRequestId() {
    static std::atomic<uint64_t> sequence{0};
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(now_ms) + "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

class ClientConnectionGuard {
public:
    ClientConnectionGuard(int fd, EpollLoop* loop, std::atomic<size_t>* active_connections)
        : fd_(fd), loop_(loop), active_connections_(active_connections) {}

    ~ClientConnectionGuard() {
        if (loop_) loop_->delFd(fd_);
        if (fd_ >= 0) close(fd_);
        if (active_connections_) active_connections_->fetch_sub(1, std::memory_order_relaxed);
        Metrics::instance().decActiveConnection();
    }

private:
    int fd_;
    EpollLoop* loop_;
    std::atomic<size_t>* active_connections_;
};

} // namespace

Task clientHandler(int fd, EpollLoop* loop,
                   std::shared_ptr<RpcDispatcher> dispatcher,
                   std::atomic<size_t>* active_connections,
                   std::string remote) {
    ClientConnectionGuard connection_guard(fd, loop, active_connections);
    std::string read_buffer;
    bool closed = false;
    while (!loop->isStopping()) {
        if (!(co_await SocketAwaitable(fd, EPOLLIN, loop))) break;
        char chunk[4096];
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (n == 0) {
                LOG_INFO_EVENT("client_closed", {{"remote", remote}});
            } else if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                LOG_ERROR_EVENT("socket_read_failed", {{"remote", remote}, {"error", errnoMessage("read")}});
            }
            break;
        }
        Metrics::instance().addBytesIn(static_cast<uint64_t>(n));
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
                LOG_WARN_EVENT("invalid_rpc_frame", {
                    {"remote", remote},
                    {"decode_status", status == rpc::DecodeStatus::TooLarge ? "too_large" : "malformed"}
                });
                Metrics::instance().incMalformedFrame();
                closed = true;
                break;
            }
            read_buffer.erase(0, frame_size);

            RpcContext context;
            context.request_id = nextRequestId();
            context.remote = remote;
            context.request_serial_id = serial_id;
            context.request_bytes = payload.size();
            context.started_at = std::chrono::steady_clock::now();
            RpcStreamResult rpc_result = dispatcher->dispatchStreamWithContext(std::move(context), payload);
            if (rpc_result.responses.empty()) {
                rpc_result.responses.push_back(
                    RpcDispatcher::error(corpcron::rpc::INTERNAL_ERROR, "Empty RPC response"));
            }
            for (auto& rpc_response : rpc_result.responses) {
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
                        Metrics::instance().addBytesOut(static_cast<uint64_t>(w));
                        continue;
                    }
                    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        if (!(co_await SocketAwaitable(fd, EPOLLOUT, loop))) {
                            closed = true;
                            break;
                        }
                        continue;
                    }
                    if (w < 0 && errno == EINTR) continue;
                    LOG_ERROR_EVENT("socket_send_failed", {{"remote", remote}, {"error", errnoMessage("send")}});
                    closed = true;
                    break;
                }
                if (closed) break;
            }
            if (closed) break;
        }
        if (closed) break;
    }
    co_return;
}

TcpServer::TcpServer(const std::string& addr, int port,
                     std::shared_ptr<RpcDispatcher> dispatcher,
                     size_t max_connections)
    : addr_(addr), port_(port), loop_(std::make_unique<EpollLoop>()),
      dispatcher_(std::move(dispatcher)), max_connections_(max_connections) {}

TcpServer::~TcpServer() {
    stop();
    closeListenSocket();
}

bool TcpServer::start(const std::function<bool()>& on_listening) {
    if (!loop_->init()) return false;
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ == -1) { LOG_ERROR(errnoMessage("socket")); return false; }
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        LOG_ERROR(errnoMessage("setsockopt"));
        closeListenSocket();
        return false;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, addr_.c_str(), &addr.sin_addr) != 1) {
        LOG_ERROR("Invalid IPv4 bind address: " + addr_);
        closeListenSocket();
        return false;
    }
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG_ERROR(errnoMessage("bind"));
        closeListenSocket();
        return false;
    }
    if (listen(listen_fd_, 128) == -1) {
        LOG_ERROR(errnoMessage("listen"));
        closeListenSocket();
        return false;
    }
    if (!loop_->addFd(listen_fd_, EPOLLIN, [this](int, uint32_t) { handleAccept(); })) {
        closeListenSocket();
        return false;
    }
    LOG_INFO_EVENT("tcp_server_listening", {{"bind", addr_}, {"port", std::to_string(port_)}});
    if (on_listening) {
        try {
            if (!on_listening()) {
                loop_->delFd(listen_fd_);
                closeListenSocket();
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR_EVENT("tcp_server_startup_hook_failed", {{"error", e.what()}});
            loop_->delFd(listen_fd_);
            closeListenSocket();
            return false;
        } catch (...) {
            LOG_ERROR_EVENT("tcp_server_startup_hook_failed", {{"error", "unknown exception"}});
            loop_->delFd(listen_fd_);
            closeListenSocket();
            return false;
        }
    }
    const bool loop_ok = loop_->run();
    closeListenSocket();
    return loop_ok;
}

void TcpServer::stop() {
    if (loop_) loop_->stop();
}

void TcpServer::handleAccept() {
    while (!loop_->isStopping()) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept4(listen_fd_, (struct sockaddr*)&client_addr, &len,
                                SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            LOG_ERROR(errnoMessage("accept")); break;
        }
        char ip[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip))) {
            std::strncpy(ip, "unknown", sizeof(ip) - 1);
        }
        if (active_connections_.load() >= max_connections_) {
            LOG_WARN_EVENT("connection_rejected", {
                {"remote", std::string(ip) + ":" + std::to_string(ntohs(client_addr.sin_port))},
                {"reason", "connection_limit"},
                {"max_connections", std::to_string(max_connections_)}
            });
            Metrics::instance().incRejectedConnection();
            close(client_fd);
            continue;
        }
        ++active_connections_;
        Metrics::instance().incActiveConnection();
        std::string remote = std::string(ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
        LOG_INFO_EVENT("connection_accepted", {{"remote", remote}});
        try {
            Task::spawn(clientHandler(client_fd, loop_.get(), dispatcher_, &active_connections_, remote));
        } catch (const std::exception& e) {
            close(client_fd);
            active_connections_.fetch_sub(1, std::memory_order_relaxed);
            Metrics::instance().decActiveConnection();
            LOG_ERROR_EVENT("connection_handler_start_failed", {
                {"remote", remote}, {"error", e.what()}
            });
        }
    }
}

void TcpServer::closeListenSocket() {
    if (listen_fd_ == -1) return;
    close(listen_fd_);
    listen_fd_ = -1;
}

} // namespace corpcron
