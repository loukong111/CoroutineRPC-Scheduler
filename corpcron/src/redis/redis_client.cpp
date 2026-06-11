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
    std::lock_guard<std::mutex> lock(mutex_);
    std::string set_key = "services:" + service_name;
    std::string node_key = set_key + ":" + endpoint;

    redisReply* reply = (redisReply*)redisCommand(ctx_, "SADD %s %s", set_key.c_str(), endpoint.c_str());
    if (reply == nullptr) return false;
    freeReplyObject(reply);

    reply = (redisReply*)redisCommand(ctx_, "SETEX %s %d %s", node_key.c_str(), ttl_sec, endpoint.c_str());
    if (reply == nullptr) return false;
    bool success = (reply->type == REDIS_REPLY_STATUS && reply->str && std::string(reply->str) == "OK");
    freeReplyObject(reply);
    return success;
}

void RedisClient::unregisterService(const std::string& service_name, const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string set_key = "services:" + service_name;
    std::string node_key = set_key + ":" + endpoint;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "SREM %s %s", set_key.c_str(), endpoint.c_str());
    if (reply) freeReplyObject(reply);
    reply = (redisReply*)redisCommand(ctx_, "DEL %s", node_key.c_str());
    if (reply) freeReplyObject(reply);
}

bool RedisClient::heartbeat(const std::string& service_name, const std::string& endpoint, int ttl_sec) {
    return registerService(service_name, endpoint, ttl_sec);
}

std::vector<std::string> RedisClient::discoverServices(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string set_key = "services:" + service_name;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "SMEMBERS %s", set_key.c_str());
    std::vector<std::string> endpoints;
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            redisReply* item = reply->element[i];
            if (!item || item->type != REDIS_REPLY_STRING) continue;
            std::string endpoint(item->str, item->len);
            std::string node_key = set_key + ":" + endpoint;
            redisReply* alive = (redisReply*)redisCommand(ctx_, "GET %s", node_key.c_str());
            if (alive && alive->type == REDIS_REPLY_STRING) {
                endpoints.emplace_back(alive->str, alive->len);
            } else {
                redisReply* cleanup = (redisReply*)redisCommand(ctx_, "SREM %s %s", set_key.c_str(), endpoint.c_str());
                if (cleanup) freeReplyObject(cleanup);
            }
            if (alive) freeReplyObject(alive);
        }
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
        //redisCommand 的返回类型是 void*，需要强转成 redisReply*
        redisReply* reply = (redisReply*)redisCommand(ctx_, "EVAL %s 1 %s %s %d", 
                            lua_script, lock_key.c_str(), value.c_str(), expire_sec);
        if (reply) {
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

bool RedisClient::renewLock(const std::string& key, const std::string& value, int expire_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lock_key = "lock:" + key;
    const char* script =
        "if redis.call('get', KEYS[1]) == ARGV[1] then "
        "   return redis.call('expire', KEYS[1], ARGV[2]) "
        "else "
        "   return 0 "
        "end";
    redisReply* reply = (redisReply*)redisCommand(ctx_, "EVAL %s 1 %s %s %d",
                                                  script, lock_key.c_str(), value.c_str(), expire_sec);
    bool success = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    if (reply) freeReplyObject(reply);
    return success;
}

void RedisClient::unlock(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lock_key = "lock:" + key;
    std::string script = 
        "if redis.call('get', KEYS[1]) == ARGV[1] then "
        "   return redis.call('del', KEYS[1]) "
        "else " 
        "   return 0 "
        "end";
    redisReply* reply = (redisReply*)redisCommand(ctx_, "EVAL %s 1 %s %s", script.c_str(), lock_key.c_str(), value.c_str());
    if (reply) freeReplyObject(reply);
}

} // namespace corpcron
