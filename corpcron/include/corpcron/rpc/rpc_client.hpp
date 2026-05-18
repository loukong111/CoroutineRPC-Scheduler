#pragma once
#include <string>
#include <memory>
#include <chrono>

namespace corpcron {

class RpcClient {
public:
    RpcClient(const std::string& host, int port);
    ~RpcClient();

    // 同步调用（简单实现，后续可改为协程）
    bool call(uint32_t serial_id, const std::string& request_data, std::string& response_data, int timeout_ms = 3000);

private:
    std::string host_;
    int port_;
    int sock_fd_;
    bool connect();
    void disconnect();
};

} // namespace corpcron