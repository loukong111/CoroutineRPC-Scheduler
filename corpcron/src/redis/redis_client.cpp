#include "corpcron/redis/redis_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace corpcron {

RedisClient::RedisClient(const std::string& host, int port)
    : host_(host), port_(port), ctx_(nullptr) {}

RedisClient::~RedisClient() {
    disconnect();
}

bool RedisClient::connect() {
    ctx_ = redisConnect(host_.c_str(), port_);
    if (ctx_ == nullptr || ctx_->err) {
        if (ctx_) std::cerr << "Redis error: " << ctx_->errstr << std::endl;
        else std::cerr << "Can't allocate redis context" << std::endl;
        return false;
    }
    return true;
}

void RedisClient::disconnect() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisClient::registerService(const std::string& service_name, const std::string& endpoint, int ttl_sec) {
    std::string key = "services:" + service_name;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "SETEX %s %d %s", key.c_str(), ttl_sec, endpoint.c_str());
    if (reply == nullptr) return false;
    bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
    freeReplyObject(reply);
    return success;
}

bool RedisClient::heartbeat(const std::string& service_name, const std::string& endpoint, int ttl_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    return registerService(service_name, endpoint, ttl_sec);
}

std::vector<std::string> RedisClient::discoverServices(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = "services:" + service_name;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "GET %s", key.c_str());
    std::vector<std::string> endpoints;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        endpoints.push_back(std::string(reply->str, reply->len));
    }
    if (reply) freeReplyObject(reply);
    return endpoints;
}

bool RedisClient::lock(const std::string& key, const std::string& value, int expire_sec, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lock_key = "lock:" + key;
    // Lua 脚本：SETNX + EXPIRE 原子操作
    const char* lua_script =
        "if redis.call('setnx', KEYS[1], ARGV[1]) == 1 then "
        "   redis.call('expire', KEYS[1], ARGV[2]) "
        "   return 'OK' "
        "else "
        "   return nil "
        "end";
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::cout << "Trying lock: key=" << lock_key << " value=" << value << std::endl;
        redisReply* reply = (redisReply*)redisCommand(ctx_, "EVAL %s 1 %s %s %d", 
                            lua_script, lock_key.c_str(), value.c_str(), expire_sec);
        if (reply) {
            std::cout << "EVAL reply type=" << reply->type << " str=" << (reply->str ? reply->str : "null") << std::endl;
            if (reply->type == REDIS_REPLY_STRING && std::string(reply->str) == "OK") {
                freeReplyObject(reply);
                return true;
            }
            freeReplyObject(reply);
        } else {
            std::cerr << "Redis lock command failed: " << ctx_->errstr << std::endl;
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >= timeout_ms)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

void RedisClient::unlock(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lock_key = "lock:" + key;
    std::string script = "if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end";
    redisReply* reply = (redisReply*)redisCommand(ctx_, "EVAL %s 1 %s %s", script.c_str(), lock_key.c_str(), value.c_str());
    if (reply) freeReplyObject(reply);
}

} // namespace corpcron