#pragma once
#include "corpcron/common/cancellation.hpp"
#include <string>
#include <memory>
#include <chrono>
#include <cstdint>
#include <functional>

namespace corpcron {

enum class RpcCallStatus {
    OK,
    ConnectFailed,
    PayloadTooLarge,
    DeadlineExceeded,
    Canceled,
    TransportError,
    ProtocolError,
};

struct RpcCallOptions {
    int timeout_ms = 3000;
    std::chrono::steady_clock::time_point deadline{};
    CancellationToken cancellation;

    static RpcCallOptions fromTimeout(int timeout_ms);
    static RpcCallOptions fromDeadline(std::chrono::steady_clock::time_point deadline);
    void ensureDeadline();
    bool hasDeadline() const;
    bool deadlineExceeded() const;
    int remainingMs() const;
    bool cancellationRequested() const;
};

class RpcClient {
public:
    RpcClient(const std::string& host, int port);
    ~RpcClient();

    // 同步调用
    bool call(uint32_t serial_id, const std::string& request_data, std::string& response_data, int timeout_ms = 3000);
    bool call(uint32_t serial_id, const std::string& request_data, uint32_t& response_serial_id,
              std::string& response_data, int timeout_ms = 3000);
    bool call(uint32_t serial_id, const std::string& request_data, uint32_t& response_serial_id,
              std::string& response_data, RpcCallOptions options);
    bool callStream(uint32_t serial_id, const std::string& request_data,
                    const std::function<bool(uint32_t, const std::string&)>& on_response,
                    int timeout_ms = 3000);
    bool callStream(uint32_t serial_id, const std::string& request_data,
                    const std::function<bool(uint32_t, const std::string&)>& on_response,
                    RpcCallOptions options);

    RpcCallStatus lastStatus() const;
    std::string lastError() const;

private:
    std::string host_;
    int port_;
    int sock_fd_;
    RpcCallStatus last_status_ = RpcCallStatus::OK;
    std::string last_error_;

    bool connect(RpcCallOptions& options);
    bool ensureConnected(RpcCallOptions& options);
    void disconnect();
    void setLastError(RpcCallStatus status, std::string error);
};

} // namespace corpcron
