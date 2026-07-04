#include "corpcron/redis/redis_client.hpp"
#include "corpcron/common/logger.hpp"
#include <cstdarg>
#include <thread>
#include <chrono>

namespace corpcron {

RedisClient::RedisClient(const std::string& host, int port)
    : host_(host), port_(port), ctx_(nullptr) {}

RedisClient::~RedisClient() {
    disconnect();
}

bool RedisClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connectLocked();
}

bool RedisClient::connectLocked() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
    ctx_ = redisConnect(host_.c_str(), port_);
    if (ctx_ == nullptr || ctx_->err) {
        if (ctx_) {
            LOG_ERROR(std::string("Redis error: ") + ctx_->errstr);
        } else {
            LOG_ERROR("Can't allocate Redis context");
        }
        return false;
    }
    return true;
}

void RedisClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisClient::reconnectLocked() {
    LOG_WARN("Redis command failed, reconnecting to " + host_ + ":" + std::to_string(port_));
    return connectLocked();
}

redisReply* RedisClient::command(const char* format, ...) {
    if (!ctx_ && !connectLocked()) {
        return nullptr;
    }

    va_list args;
    va_start(args, format);
    va_list retry_args;
    va_copy(retry_args, args);
    redisReply* reply = static_cast<redisReply*>(redisvCommand(ctx_, format, args));
    va_end(args);
    if (reply) {
        va_end(retry_args);
        return reply;
    }

    if (ctx_ && ctx_->err) {
        LOG_ERROR(std::string("Redis command error: ") + ctx_->errstr);
    }
    if (!reconnectLocked()) {
        va_end(retry_args);
        return nullptr;
    }

    reply = static_cast<redisReply*>(redisvCommand(ctx_, format, retry_args));
    va_end(retry_args);
    return reply;
}

bool RedisClient::registerService(const std::string& service_name, const std::string& endpoint, int ttl_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string set_key = "services:" + service_name;
    std::string node_key = set_key + ":" + endpoint;

    redisReply* reply = command("SADD %s %s", set_key.c_str(), endpoint.c_str());
    if (reply == nullptr) return false;
    freeReplyObject(reply);

    reply = command("SETEX %s %d %s", node_key.c_str(), ttl_sec, endpoint.c_str());
    if (reply == nullptr) return false;
    bool success = (reply->type == REDIS_REPLY_STATUS && reply->str && std::string(reply->str) == "OK");
    freeReplyObject(reply);
    return success;
}

void RedisClient::unregisterService(const std::string& service_name, const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string set_key = "services:" + service_name;
    std::string node_key = set_key + ":" + endpoint;
    redisReply* reply = command("SREM %s %s", set_key.c_str(), endpoint.c_str());
    if (reply) freeReplyObject(reply);
    reply = command("DEL %s", node_key.c_str());
    if (reply) freeReplyObject(reply);
}

bool RedisClient::heartbeat(const std::string& service_name, const std::string& endpoint, int ttl_sec) {
    return registerService(service_name, endpoint, ttl_sec);
}

std::vector<std::string> RedisClient::discoverServices(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string set_key = "services:" + service_name;
    redisReply* reply = command("SMEMBERS %s", set_key.c_str());
    std::vector<std::string> endpoints;
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            redisReply* item = reply->element[i];
            if (!item || item->type != REDIS_REPLY_STRING) continue;
            std::string endpoint(item->str, item->len);
            std::string node_key = set_key + ":" + endpoint;
            redisReply* alive = command("GET %s", node_key.c_str());
            if (alive && alive->type == REDIS_REPLY_STRING) {
                endpoints.emplace_back(alive->str, alive->len);
            } else {
                redisReply* cleanup = command("SREM %s %s", set_key.c_str(), endpoint.c_str());
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
        redisReply* reply = command("EVAL %s 1 %s %s %d",
                                    lua_script, lock_key.c_str(), value.c_str(), expire_sec);
        if (reply) {
            if (reply->type == REDIS_REPLY_STRING && std::string(reply->str) == "OK") {
                freeReplyObject(reply);
                return true;
            }
            freeReplyObject(reply);
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
    redisReply* reply = command("EVAL %s 1 %s %s %d",
                                script, lock_key.c_str(), value.c_str(), expire_sec);
    bool success = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    if (reply) freeReplyObject(reply);
    return success;
}

bool RedisClient::unlock(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lock_key = "lock:" + key;
    std::string script = 
        "if redis.call('get', KEYS[1]) == ARGV[1] then "
        "   return redis.call('del', KEYS[1]) "
        "else " 
        "   return 0 "
        "end";
    redisReply* reply = command("EVAL %s 1 %s %s", script.c_str(), lock_key.c_str(), value.c_str());
    bool success = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    if (reply) freeReplyObject(reply);
    return success;
}

} // namespace corpcron
