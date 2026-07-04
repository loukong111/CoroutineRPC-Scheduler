#include "corpcron/rpc/rpc_client.hpp"
#include "corpcron/rpc/protocol.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <vector>

namespace corpcron {

namespace {

bool waitForEvent(int fd, short events, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    int ret = poll(&pfd, 1, timeout_ms);
    return ret > 0 && (pfd.revents & events);
}

bool sendAll(int fd, const std::string& data, int timeout_ms) {
    size_t sent = 0;
    while (sent < data.size()) {
        if (!waitForEvent(fd, POLLOUT, timeout_ms)) return false;
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

bool recvExact(int fd, char* buffer, size_t size, int timeout_ms) {
    size_t received = 0;
    while (received < size) {
        if (!waitForEvent(fd, POLLIN, timeout_ms)) return false;
        ssize_t n = recv(fd, buffer + received, size - received, 0);
        if (n > 0) {
            received += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

} // namespace

RpcClient::RpcClient(const std::string& host, int port)
    : host_(host), port_(port), sock_fd_(-1) {}

RpcClient::~RpcClient() {
    disconnect();
}

bool RpcClient::connect() {
    if (sock_fd_ != -1) return true;
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) return false;
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }
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

bool RpcClient::ensureConnected() {
    return sock_fd_ != -1 || connect();
}

bool RpcClient::call(uint32_t serial_id, const std::string& request_data, std::string& response_data, int timeout_ms) {
    uint32_t response_serial_id = 0;
    return call(serial_id, request_data, response_serial_id, response_data, timeout_ms);
}

bool RpcClient::call(uint32_t serial_id, const std::string& request_data, uint32_t& response_serial_id,
                     std::string& response_data, int timeout_ms) {
    if (!ensureConnected()) return false;

    std::string data;
    if (!rpc::tryEncode(serial_id, request_data, data)) {
        disconnect();
        return false;
    }
    if (!sendAll(sock_fd_, data, timeout_ms)) {
        disconnect();
        return false;
    }

    char header[rpc::kHeaderSize];
    if (!recvExact(sock_fd_, header, sizeof(header), timeout_ms)) {
        disconnect();
        return false;
    }

    uint32_t total_len = ((unsigned char)header[0] << 24) | ((unsigned char)header[1] << 16) |
                         ((unsigned char)header[2] << 8) | (unsigned char)header[3];
    if (total_len < rpc::kSerialIdSize || total_len > rpc::kMaxFrameSize) {
        disconnect();
        return false;
    }

    std::vector<char> frame(rpc::kHeaderSize + total_len - rpc::kSerialIdSize);
    std::memcpy(frame.data(), header, sizeof(header));
    if (!recvExact(sock_fd_, frame.data() + rpc::kHeaderSize, total_len - rpc::kSerialIdSize, timeout_ms)) {
        disconnect();
        return false;
    }

    std::string payload;
    size_t frame_size = 0;
    if (rpc::tryDecodeFrame(frame.data(), frame.size(), response_serial_id, payload, frame_size) == rpc::DecodeStatus::Complete) {
        response_data = payload;
        return true;
    }
    disconnect();
    return false;
}

} // namespace corpcron
