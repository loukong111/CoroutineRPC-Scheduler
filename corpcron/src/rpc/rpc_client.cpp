#include "corpcron/rpc/rpc_client.hpp"
#include "corpcron/rpc/protocol.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <iostream>

namespace corpcron {

RpcClient::RpcClient(const std::string& host, int port)
    : host_(host), port_(port), sock_fd_(-1) {}

RpcClient::~RpcClient() {
    disconnect();
}

bool RpcClient::connect() {
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) return false;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    if (::connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }
    return true;
}

void RpcClient::disconnect() {
    if (sock_fd_ != -1) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
}

bool RpcClient::call(uint32_t serial_id, const std::string& request_data, std::string& response_data, int timeout_ms) {
    if (!connect()) return false;

    std::string data = rpc::encode(serial_id, request_data);
    if (send(sock_fd_, data.data(), data.size(), 0) < 0) {
        disconnect();
        return false;
    }

    // 设置超时
    struct pollfd pfd;
    pfd.fd = sock_fd_;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) {
        disconnect();
        return false;
    }

    char buffer[4096];
    ssize_t n = recv(sock_fd_, buffer, sizeof(buffer), 0);
    if (n <= 0) {
        disconnect();
        return false;
    }

    uint32_t resp_id;
    std::string payload;
    if (rpc::decode(buffer, n, resp_id, payload)) {
        response_data = payload;
        disconnect();
        return true;
    }
    disconnect();
    return false;
}

} // namespace corpcron