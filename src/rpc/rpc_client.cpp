#include "corpcron/rpc/rpc_client.hpp"
#include "corpcron/rpc/protocol.hpp"
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <algorithm>
#include <utility>
#include <vector>

namespace corpcron {

namespace {

constexpr int kPollSliceMs = 100;

const char* statusName(RpcCallStatus status) {
    switch (status) {
        case RpcCallStatus::OK: return "ok";
        case RpcCallStatus::ConnectFailed: return "connect failed";
        case RpcCallStatus::PayloadTooLarge: return "payload too large";
        case RpcCallStatus::DeadlineExceeded: return "deadline exceeded";
        case RpcCallStatus::Canceled: return "canceled";
        case RpcCallStatus::TransportError: return "transport error";
        case RpcCallStatus::ProtocolError: return "protocol error";
    }
    return "unknown";
}

RpcCallStatus terminalStatus(const RpcCallOptions& options) {
    if (options.cancellationRequested()) return RpcCallStatus::Canceled;
    if (options.deadlineExceeded()) return RpcCallStatus::DeadlineExceeded;
    return RpcCallStatus::TransportError;
}

bool waitForEvent(int fd, short events, RpcCallOptions& options, RpcCallStatus& status) {
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    while (true) {
        if (options.cancellationRequested()) {
            status = RpcCallStatus::Canceled;
            return false;
        }
        int remaining = options.remainingMs();
        if (remaining <= 0) {
            status = RpcCallStatus::DeadlineExceeded;
            return false;
        }
        int wait_ms = std::min(remaining, kPollSliceMs);
        int ret = poll(&pfd, 1, wait_ms);
        if (ret > 0) {
            if (pfd.revents & events) return true;
            status = RpcCallStatus::TransportError;
            return false;
        }
        if (ret == 0) continue;
        if (errno == EINTR) continue;
        status = RpcCallStatus::TransportError;
        return false;
    }
}

bool sendAll(int fd, const std::string& data, RpcCallOptions& options, RpcCallStatus& status) {
    size_t sent = 0;
    while (sent < data.size()) {
        if (!waitForEvent(fd, POLLOUT, options, status)) return false;
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        status = terminalStatus(options);
        return false;
    }
    return true;
}

bool recvExact(int fd, char* buffer, size_t size, RpcCallOptions& options, RpcCallStatus& status) {
    size_t received = 0;
    while (received < size) {
        if (!waitForEvent(fd, POLLIN, options, status)) return false;
        ssize_t n = recv(fd, buffer + received, size - received, 0);
        if (n > 0) {
            received += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        status = terminalStatus(options);
        return false;
    }
    return true;
}

} // namespace

RpcCallOptions RpcCallOptions::fromTimeout(int timeout_ms) {
    RpcCallOptions options;
    options.timeout_ms = timeout_ms > 0 ? timeout_ms : 3000;
    options.deadline = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(options.timeout_ms);
    return options;
}

RpcCallOptions RpcCallOptions::fromDeadline(std::chrono::steady_clock::time_point deadline) {
    RpcCallOptions options;
    options.deadline = deadline;
    return options;
}

void RpcCallOptions::ensureDeadline() {
    if (!hasDeadline()) {
        int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : 3000;
        deadline = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(effective_timeout_ms);
    }
}

bool RpcCallOptions::hasDeadline() const {
    return deadline.time_since_epoch().count() != 0;
}

bool RpcCallOptions::deadlineExceeded() const {
    return hasDeadline() && std::chrono::steady_clock::now() >= deadline;
}

int RpcCallOptions::remainingMs() const {
    if (!hasDeadline()) return timeout_ms > 0 ? timeout_ms : 3000;
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) return 0;
    return remaining > 2147483647 ? 2147483647 : static_cast<int>(remaining);
}

bool RpcCallOptions::cancellationRequested() const {
    return cancellation.isCancellationRequested();
}

RpcClient::RpcClient(const std::string& host, int port)
    : host_(host), port_(port), sock_fd_(-1) {}

RpcClient::~RpcClient() {
    disconnect();
}

bool RpcClient::connect(RpcCallOptions& options) {
    if (sock_fd_ != -1) return true;
    if (host_.empty() || port_ <= 0 || port_ > 65535) {
        setLastError(RpcCallStatus::ConnectFailed, "invalid RPC endpoint");
        return false;
    }
    sock_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock_fd_ < 0) {
        setLastError(RpcCallStatus::ConnectFailed, std::strerror(errno));
        return false;
    }
    int flags = fcntl(sock_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(sock_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        const std::string error = std::strerror(errno);
        disconnect();
        setLastError(RpcCallStatus::ConnectFailed, error);
        return false;
    }
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        close(sock_fd_);
        sock_fd_ = -1;
        setLastError(RpcCallStatus::ConnectFailed, "invalid host: " + host_);
        return false;
    }
    int ret = ::connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(sock_fd_);
        sock_fd_ = -1;
        setLastError(RpcCallStatus::ConnectFailed, std::strerror(errno));
        return false;
    }
    if (ret < 0) {
        RpcCallStatus status = RpcCallStatus::OK;
        if (!waitForEvent(sock_fd_, POLLOUT, options, status)) {
            disconnect();
            setLastError(status, statusName(status));
            return false;
        }
        int socket_error = 0;
        socklen_t len = sizeof(socket_error);
        if (getsockopt(sock_fd_, SOL_SOCKET, SO_ERROR, &socket_error, &len) != 0 ||
            socket_error != 0) {
            std::string error = socket_error == 0 ? std::strerror(errno) : std::strerror(socket_error);
            disconnect();
            setLastError(RpcCallStatus::ConnectFailed, error);
            return false;
        }
    }
    return true;
}

void RpcClient::disconnect() {
    if (sock_fd_ != -1) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
}

bool RpcClient::ensureConnected(RpcCallOptions& options) {
    return sock_fd_ != -1 || connect(options);
}

bool RpcClient::call(uint32_t serial_id, const std::string& request_data, std::string& response_data, int timeout_ms) {
    uint32_t response_serial_id = 0;
    return call(serial_id, request_data, response_serial_id, response_data, timeout_ms);
}

bool RpcClient::call(uint32_t serial_id, const std::string& request_data, uint32_t& response_serial_id,
                     std::string& response_data, int timeout_ms) {
    return call(serial_id, request_data, response_serial_id, response_data,
                RpcCallOptions::fromTimeout(timeout_ms));
}

bool RpcClient::call(uint32_t serial_id, const std::string& request_data, uint32_t& response_serial_id,
                     std::string& response_data, RpcCallOptions options) {
    response_serial_id = 0;
    response_data.clear();
    options.ensureDeadline();
    last_status_ = RpcCallStatus::OK;
    last_error_.clear();
    if (!ensureConnected(options)) return false;

    std::string data;
    if (!rpc::tryEncode(serial_id, request_data, data)) {
        disconnect();
        setLastError(RpcCallStatus::PayloadTooLarge, "request payload too large");
        return false;
    }
    RpcCallStatus status = RpcCallStatus::OK;
    if (!sendAll(sock_fd_, data, options, status)) {
        disconnect();
        setLastError(status, statusName(status));
        return false;
    }

    char header[rpc::kHeaderSize];
    if (!recvExact(sock_fd_, header, sizeof(header), options, status)) {
        disconnect();
        setLastError(status, statusName(status));
        return false;
    }

    uint32_t total_len = ((unsigned char)header[0] << 24) | ((unsigned char)header[1] << 16) |
                         ((unsigned char)header[2] << 8) | (unsigned char)header[3];
    if (total_len < rpc::kSerialIdSize || total_len > rpc::kMaxFrameSize) {
        disconnect();
        setLastError(RpcCallStatus::ProtocolError, "invalid response frame length");
        return false;
    }

    std::vector<char> frame(rpc::kHeaderSize + total_len - rpc::kSerialIdSize);
    std::memcpy(frame.data(), header, sizeof(header));
    if (!recvExact(sock_fd_, frame.data() + rpc::kHeaderSize,
                   total_len - rpc::kSerialIdSize, options, status)) {
        disconnect();
        setLastError(status, statusName(status));
        return false;
    }

    std::string payload;
    size_t frame_size = 0;
    if (rpc::tryDecodeFrame(frame.data(), frame.size(), response_serial_id, payload, frame_size) == rpc::DecodeStatus::Complete) {
        response_data = payload;
        last_status_ = RpcCallStatus::OK;
        return true;
    }
    disconnect();
    setLastError(RpcCallStatus::ProtocolError, "decode response frame failed");
    return false;
}

bool RpcClient::callStream(uint32_t serial_id, const std::string& request_data,
                           const std::function<bool(uint32_t, const std::string&)>& on_response,
                           int timeout_ms) {
    return callStream(serial_id, request_data, on_response, RpcCallOptions::fromTimeout(timeout_ms));
}

bool RpcClient::callStream(uint32_t serial_id, const std::string& request_data,
                           const std::function<bool(uint32_t, const std::string&)>& on_response,
                           RpcCallOptions options) {
    options.ensureDeadline();
    last_status_ = RpcCallStatus::OK;
    last_error_.clear();
    if (!ensureConnected(options)) return false;

    std::string data;
    if (!rpc::tryEncode(serial_id, request_data, data)) {
        disconnect();
        setLastError(RpcCallStatus::PayloadTooLarge, "request payload too large");
        return false;
    }

    RpcCallStatus status = RpcCallStatus::OK;
    if (!sendAll(sock_fd_, data, options, status)) {
        disconnect();
        setLastError(status, statusName(status));
        return false;
    }

    while (true) {
        char header[rpc::kHeaderSize];
        if (!recvExact(sock_fd_, header, sizeof(header), options, status)) {
            disconnect();
            setLastError(status, statusName(status));
            return false;
        }

        uint32_t total_len = ((unsigned char)header[0] << 24) | ((unsigned char)header[1] << 16) |
                             ((unsigned char)header[2] << 8) | (unsigned char)header[3];
        if (total_len < rpc::kSerialIdSize || total_len > rpc::kMaxFrameSize) {
            disconnect();
            setLastError(RpcCallStatus::ProtocolError, "invalid stream response frame length");
            return false;
        }

        std::vector<char> frame(rpc::kHeaderSize + total_len - rpc::kSerialIdSize);
        std::memcpy(frame.data(), header, sizeof(header));
        if (!recvExact(sock_fd_, frame.data() + rpc::kHeaderSize,
                       total_len - rpc::kSerialIdSize, options, status)) {
            disconnect();
            setLastError(status, statusName(status));
            return false;
        }

        uint32_t response_serial_id = 0;
        std::string payload;
        size_t frame_size = 0;
        if (rpc::tryDecodeFrame(frame.data(), frame.size(), response_serial_id, payload,
                                frame_size) != rpc::DecodeStatus::Complete) {
            disconnect();
            setLastError(RpcCallStatus::ProtocolError, "decode stream response frame failed");
            return false;
        }

        if (on_response && !on_response(response_serial_id, payload)) {
            // The transport cannot know whether the callback stopped on the final frame.
            // Drop the connection so unread stream frames cannot corrupt the next RPC.
            disconnect();
            last_status_ = RpcCallStatus::OK;
            return true;
        }
    }
}

RpcCallStatus RpcClient::lastStatus() const {
    return last_status_;
}

std::string RpcClient::lastError() const {
    return last_error_;
}

void RpcClient::setLastError(RpcCallStatus status, std::string error) {
    last_status_ = status;
    last_error_ = std::move(error);
}

} // namespace corpcron
