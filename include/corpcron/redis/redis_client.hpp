#pragma once
#include "corpcron/common/storage_error.hpp"
#include <string>
#include <vector>
#include <hiredis/hiredis.h>
#include <atomic>
#include <mutex>
#include <memory>

namespace corpcron {

struct RedisClientOptions {
    size_t pool_size = 1;
    int connect_timeout_ms = 1000;
    int command_timeout_ms = 1000;
};

class RedisClient {
public:
    RedisClient(const std::string& host, int port);
    RedisClient(const std::string& host, int port, RedisClientOptions options);
    ~RedisClient();

    bool connect();
    void disconnect();
    StorageError lastError() const;

    // 服务注册与心跳
    bool registerService(const std::string& service_name, const std::string& endpoint, int ttl_sec = 30);
    void unregisterService(const std::string& service_name, const std::string& endpoint);
    bool heartbeat(const std::string& service_name, const std::string& endpoint, int ttl_sec = 30);
    std::vector<std::string> discoverServices(const std::string& service_name);

    // 分布式锁
    bool lock(const std::string& key, const std::string& value, int expire_sec = 10, int timeout_ms = 1000);
    bool renewLock(const std::string& key, const std::string& value, int expire_sec = 10);
    bool unlock(const std::string& key, const std::string& value);

private:
    struct ConnectionSlot;
    std::string host_;
    int port_;
    RedisClientOptions options_;
    std::vector<std::unique_ptr<ConnectionSlot>> slots_;
    std::atomic<size_t> next_slot_{0};
    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex error_mutex_;
    mutable StorageError last_error_;

    bool connectSlotLocked(ConnectionSlot& slot);
    bool reconnectSlotLocked(ConnectionSlot& slot);
    ConnectionSlot& pickSlot();
    void setLastError(StorageErrorKind kind, int code, const std::string& message) const;
    redisReply* command(const char* format, ...);
};

} // namespace corpcron
