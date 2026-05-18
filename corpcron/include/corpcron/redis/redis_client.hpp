#pragma once
#include <string>
#include <vector>
#include <hiredis/hiredis.h>
#include <mutex>

namespace corpcron {

class RedisClient {
public:
    RedisClient(const std::string& host, int port);
    ~RedisClient();

    bool connect();
    void disconnect();

    // 服务注册与心跳
    bool registerService(const std::string& service_name, const std::string& endpoint, int ttl_sec = 30);
    bool heartbeat(const std::string& service_name, const std::string& endpoint, int ttl_sec = 30);
    std::vector<std::string> discoverServices(const std::string& service_name);

    // 分布式锁
    bool lock(const std::string& key, const std::string& value, int expire_sec = 10, int timeout_ms = 1000);
    void unlock(const std::string& key, const std::string& value);

private:
    mutable std::mutex mutex_;
    std::string host_;
    int port_;
    redisContext* ctx_;
};

} // namespace corpcron