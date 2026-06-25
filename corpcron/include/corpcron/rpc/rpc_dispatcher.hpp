#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "rpc.pb.h"

namespace corpcron {

class MySQLClient;
class RedisClient;

struct RpcResponse {
    uint32_t serial_id;
    std::string payload;
};

class RpcDispatcher {
public:
    RpcDispatcher(std::shared_ptr<MySQLClient> db, std::string auth_token);
    RpcDispatcher(std::shared_ptr<MySQLClient> db, std::shared_ptr<RedisClient> redis, std::string auth_token);

    RpcResponse dispatch(uint32_t serial_id, const std::string& payload) const;
    static RpcResponse error(corpcron::rpc::ErrorCode code, const std::string& message);

private:
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

    bool authorized(const std::string& request_token) const;

    std::shared_ptr<MySQLClient> db_;
    std::shared_ptr<RedisClient> redis_;
    std::string auth_token_;
};

} // namespace corpcron
