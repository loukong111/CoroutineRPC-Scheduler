#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "rpc.pb.h"

namespace corpcron {

class MySQLClient;
class RedisClient;
class RpcClientPool;
class RpcInterceptorChain;
struct RpcContext;

struct RpcResponse {
    uint32_t serial_id;
    std::string payload;
};

struct RpcStreamResult {
    std::vector<RpcResponse> responses;
};

enum class RpcNodeRole {
    Combined,
    ControlPlane,
    Worker,
};

struct RpcDispatcherOptions {
    RpcNodeRole role = RpcNodeRole::Combined;
    std::string service_name = "rpc";
    std::string worker_service_name = "worker";
    int worker_timeout_ms = 5000;
};

class RpcDispatcher {
public:
    RpcDispatcher(std::shared_ptr<MySQLClient> db, std::string auth_token);
    RpcDispatcher(std::shared_ptr<MySQLClient> db, std::shared_ptr<RedisClient> redis,
                  std::string auth_token, std::string node_id = {},
                  RpcDispatcherOptions options = {});
    ~RpcDispatcher();

    RpcResponse dispatch(uint32_t serial_id, const std::string& payload) const;
    RpcResponse dispatchWithContext(RpcContext context, const std::string& payload) const;
    RpcStreamResult dispatchStreamWithContext(RpcContext context, const std::string& payload) const;
    void setInterceptors(std::shared_ptr<RpcInterceptorChain> interceptors);
    static RpcResponse error(corpcron::rpc::ErrorCode code, const std::string& message);

private:
    struct WorkerResult {
        bool success = false;
        std::string result;
        std::string error;
        std::string endpoint;
    };

    RpcResponse dispatchCore(uint32_t serial_id, const std::string& payload) const;

    RpcResponse handleEcho(const std::string& payload) const;
    RpcResponse handleSubmitTask(const std::string& payload) const;
    RpcResponse handleExecuteTask(const std::string& payload) const;
    RpcResponse handleCancelTask(const std::string& payload) const;
    RpcResponse handleListTasks(const std::string& payload) const;
    RpcResponse handleListHistory(const std::string& payload) const;
    RpcResponse handleListServices(const std::string& payload) const;
    RpcResponse handleUpdateTask(const std::string& payload) const;
    RpcResponse handleEnableTask(const std::string& payload) const;
    RpcResponse handleDeleteTask(const std::string& payload) const;
    RpcResponse handleRunTaskNow(const std::string& payload) const;
    RpcResponse handleGetMetrics(const std::string& payload) const;
    RpcResponse handleHealthCheck(const std::string& payload) const;
    RpcStreamResult handleStreamMetrics(const std::string& payload) const;

    bool authorized(const std::string& request_token) const;
    bool methodAllowed(uint32_t serial_id) const;
    bool handlerAvailable(const std::string& handler) const;
    WorkerResult invokeWorker(const std::string& task_id, const std::string& execution_id,
                              const std::string& handler, const std::string& params) const;

    std::shared_ptr<MySQLClient> db_;
    std::shared_ptr<RedisClient> redis_;
    mutable std::unique_ptr<RpcClientPool> worker_pool_;
    std::shared_ptr<RpcInterceptorChain> interceptors_;
    std::string auth_token_;
    std::string node_id_;
    RpcDispatcherOptions options_;
};

} // namespace corpcron
