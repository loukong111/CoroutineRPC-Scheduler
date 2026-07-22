#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace corpcron {

struct RpcResponse;

struct RpcContext {
    std::string request_id;
    std::string remote;
    uint32_t request_serial_id = 0;
    uint32_t response_serial_id = 0;
    std::string method_name;
    size_t request_bytes = 0;
    size_t response_bytes = 0;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::milliseconds elapsed{0};
    bool success = false;
};

using RpcNext = std::function<RpcResponse(RpcContext&)>;

class RpcInterceptor {
public:
    virtual ~RpcInterceptor() = default;
    virtual RpcResponse intercept(RpcContext& context, RpcNext next) = 0;
};

class RpcInterceptorChain {
public:
    void add(std::shared_ptr<RpcInterceptor> interceptor);
    bool empty() const;
    RpcResponse invoke(RpcContext& context, RpcNext terminal) const;

private:
    std::vector<std::shared_ptr<RpcInterceptor>> interceptors_;
};

class RpcExceptionInterceptor : public RpcInterceptor {
public:
    RpcResponse intercept(RpcContext& context, RpcNext next) override;
};

class RpcMetricsInterceptor : public RpcInterceptor {
public:
    RpcResponse intercept(RpcContext& context, RpcNext next) override;
};

class RpcLoggingInterceptor : public RpcInterceptor {
public:
    RpcResponse intercept(RpcContext& context, RpcNext next) override;
};

std::shared_ptr<RpcInterceptorChain> makeDefaultRpcInterceptorChain();

} // namespace corpcron
