#pragma once
#include <string>
#include <memory>
#include <chrono>

namespace corpcron {

class RpcClient {
public:
    RpcClient(const std::string& host, int port);
    ~RpcClient();

    // 同步调用
    bool call(uint32_t serial_id, const std::string& request_data, std::string& response_data, int timeout_ms = 3000);
    bool call(uint32_t serial_id, const std::string& request_data, uint32_t& response_serial_id,
              std::string& response_data, int timeout_ms = 3000);

private:
    std::string host_;
    int port_;
    int sock_fd_;
    bool connect();
    bool ensureConnected();
    void disconnect();
};

} // namespace corpcron
