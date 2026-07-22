#include "corpcron/redis/redis_client.hpp"
#include "corpcron/common/logger.hpp"
#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cstring>
#include <thread>
#include <chrono>

namespace corpcron {

struct RedisClient::ConnectionSlot {
    redisContext* ctx = nullptr;
    std::mutex mutex;

    ~ConnectionSlot() {
        if (ctx) redisFree(ctx);
    }
};

namespace {

timeval to_timeval(int timeout_ms) {
    if (timeout_ms <= 0) timeout_ms = 1000;
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return tv;
}

StorageErrorKind classify_redis_error(int err, const char* errstr) {
    std::string message = errstr ? errstr : "";
    std::transform(message.begin(), message.end(), message.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (message.find("timed out") != std::string::npos ||
        message.find("timeout") != std::string::npos) {
        return StorageErrorKind::Timeout;
    }
    switch (err) {
        case REDIS_ERR_IO:
        case REDIS_ERR_EOF:
            return StorageErrorKind::Connection;
        case REDIS_ERR_PROTOCOL:
            return StorageErrorKind::Protocol;
        case REDIS_ERR_OOM:
            return StorageErrorKind::Unknown;
        default:
            return StorageErrorKind::Unknown;
    }
}

} // namespace

RedisClient::RedisClient(const std::string& host, int port)
    : RedisClient(host, port, RedisClientOptions{}) {}

RedisClient::RedisClient(const std::string& host, int port, RedisClientOptions options)
    : host_(host), port_(port), options_(options) {
    if (options_.pool_size == 0) options_.pool_size = 1;
    if (options_.connect_timeout_ms <= 0) options_.connect_timeout_ms = 1000;
    if (options_.command_timeout_ms <= 0) options_.command_timeout_ms = 1000;
    slots_.reserve(options_.pool_size);
    for (size_t i = 0; i < options_.pool_size; ++i) {
        slots_.push_back(std::make_unique<ConnectionSlot>());
    }
}

RedisClient::~RedisClient() {
    disconnect();
}

bool RedisClient::connect() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    bool ok = true;
    for (auto& slot : slots_) {
        std::lock_guard<std::mutex> slot_lock(slot->mutex);
        ok = connectSlotLocked(*slot) && ok;
    }
    return ok;
}

bool RedisClient::connectSlotLocked(ConnectionSlot& slot) {
    if (slot.ctx) {
        redisFree(slot.ctx);
        slot.ctx = nullptr;
    }

    timeval connect_timeout = to_timeval(options_.connect_timeout_ms);
    slot.ctx = redisConnectWithTimeout(host_.c_str(), port_, connect_timeout);
    if (slot.ctx == nullptr || slot.ctx->err) {
        const int code = slot.ctx ? slot.ctx->err : 0;
        const std::string message = slot.ctx ? slot.ctx->errstr : "Can't allocate Redis context";
        setLastError(classify_redis_error(code, message.c_str()), code, message);
        LOG_ERROR("Redis connect error kind=" +
                  std::string(storageErrorKindName(lastError().kind)) + " message=" + message);
        if (slot.ctx) {
            redisFree(slot.ctx);
            slot.ctx = nullptr;
        }
        return false;
    }

    timeval command_timeout = to_timeval(options_.command_timeout_ms);
    if (redisSetTimeout(slot.ctx, command_timeout) != REDIS_OK) {
        const std::string message = slot.ctx->errstr[0] != '\0' ? slot.ctx->errstr : "redisSetTimeout failed";
        setLastError(classify_redis_error(slot.ctx->err, message.c_str()), slot.ctx->err, message);
        LOG_ERROR("Redis timeout setup error kind=" +
                  std::string(storageErrorKindName(lastError().kind)) + " message=" + message);
        redisFree(slot.ctx);
        slot.ctx = nullptr;
        return false;
    }

    setLastError(StorageErrorKind::None, 0, "");
    return true;
}

void RedisClient::disconnect() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    for (auto& slot : slots_) {
        std::lock_guard<std::mutex> slot_lock(slot->mutex);
        if (slot->ctx) {
            redisFree(slot->ctx);
            slot->ctx = nullptr;
        }
    }
}

StorageError RedisClient::lastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void RedisClient::setLastError(StorageErrorKind kind, int code, const std::string& message) const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = StorageError{kind, code, message};
}

RedisClient::ConnectionSlot& RedisClient::pickSlot() {
    const size_t index = next_slot_.fetch_add(1, std::memory_order_relaxed) % slots_.size();
    return *slots_[index];
}

bool RedisClient::reconnectSlotLocked(ConnectionSlot& slot) {
    LOG_WARN("Redis command failed, reconnecting to " + host_ + ":" + std::to_string(port_));
    return connectSlotLocked(slot);
}

redisReply* RedisClient::command(const char* format, ...) {
    ConnectionSlot& slot = pickSlot();
    std::lock_guard<std::mutex> slot_lock(slot.mutex);
    if (!slot.ctx && !connectSlotLocked(slot)) {
        return nullptr;
    }

    va_list args;
    va_start(args, format);
    va_list retry_args;
    va_copy(retry_args, args);
    redisReply* reply = static_cast<redisReply*>(redisvCommand(slot.ctx, format, args));
    va_end(args);
    if (reply) {
        va_end(retry_args);
        if (reply->type == REDIS_REPLY_ERROR) {
            setLastError(StorageErrorKind::Query, 0,
                         reply->str ? std::string(reply->str, reply->len) : "Redis command error");
        } else {
            setLastError(StorageErrorKind::None, 0, "");
        }
        return reply;
    }

    if (slot.ctx && slot.ctx->err) {
        setLastError(classify_redis_error(slot.ctx->err, slot.ctx->errstr),
                     slot.ctx->err, slot.ctx->errstr);
        LOG_ERROR("Redis command error kind=" +
                  std::string(storageErrorKindName(lastError().kind)) +
                  " message=" + lastError().message);
    }
    if (!reconnectSlotLocked(slot)) {
        va_end(retry_args);
        return nullptr;
    }

    reply = static_cast<redisReply*>(redisvCommand(slot.ctx, format, retry_args));
    va_end(retry_args);
    if (!reply && slot.ctx && slot.ctx->err) {
        setLastError(classify_redis_error(slot.ctx->err, slot.ctx->errstr),
                     slot.ctx->err, slot.ctx->errstr);
    } else if (reply) {
        if (reply->type == REDIS_REPLY_ERROR) {
            setLastError(StorageErrorKind::Query, 0,
                         reply->str ? std::string(reply->str, reply->len) : "Redis command error");
        } else {
            setLastError(StorageErrorKind::None, 0, "");
        }
    }
    return reply;
}

bool RedisClient::registerService(const std::string& service_name, const std::string& endpoint, int ttl_sec) {
    if (service_name.empty() || endpoint.empty() || ttl_sec <= 0) return false;
    std::string set_key = "services:" + service_name;
    std::string node_key = set_key + ":" + endpoint;

    const char* script =
        "local result = redis.call('set', KEYS[2], ARGV[1], 'EX', ARGV[2]) "
        "redis.call('sadd', KEYS[1], ARGV[1]) "
        "return result";
    redisReply* reply = command("EVAL %s 2 %s %s %s %d", script, set_key.c_str(),
                                node_key.c_str(), endpoint.c_str(), ttl_sec);
    bool success = reply && reply->type == REDIS_REPLY_STATUS && reply->str &&
                   std::string(reply->str, reply->len) == "OK";
    if (reply) freeReplyObject(reply);
    return success;
}

void RedisClient::unregisterService(const std::string& service_name, const std::string& endpoint) {
    if (service_name.empty() || endpoint.empty()) return;
    std::string set_key = "services:" + service_name;
    std::string node_key = set_key + ":" + endpoint;
    const char* script =
        "redis.call('srem', KEYS[1], ARGV[1]) "
        "return redis.call('del', KEYS[2])";
    redisReply* reply = command("EVAL %s 2 %s %s %s", script, set_key.c_str(),
                                node_key.c_str(), endpoint.c_str());
    if (reply) freeReplyObject(reply);
}

bool RedisClient::heartbeat(const std::string& service_name, const std::string& endpoint, int ttl_sec) {
    return registerService(service_name, endpoint, ttl_sec);
}

std::vector<std::string> RedisClient::discoverServices(const std::string& service_name) {
    if (service_name.empty()) return {};
    std::string set_key = "services:" + service_name;
    const char* script =
        "local members = redis.call('smembers', KEYS[1]) "
        "local alive = {} "
        "for _, endpoint in ipairs(members) do "
        "  if redis.call('exists', KEYS[1] .. ':' .. endpoint) == 1 then "
        "    table.insert(alive, endpoint) "
        "  else "
        "    redis.call('srem', KEYS[1], endpoint) "
        "  end "
        "end "
        "return alive";
    redisReply* reply = command("EVAL %s 1 %s", script, set_key.c_str());
    std::vector<std::string> endpoints;
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            redisReply* item = reply->element[i];
            if (!item || item->type != REDIS_REPLY_STRING) continue;
            endpoints.emplace_back(item->str, item->len);
        }
    }
    if (reply) freeReplyObject(reply);
    return endpoints;
}

bool RedisClient::lock(const std::string& key, const std::string& value, int expire_sec, int timeout_ms) {
    if (key.empty() || value.empty() || expire_sec <= 0) return false;
    std::string lock_key = "lock:" + key;
    const char* lua_script =
        "if redis.call('setnx', KEYS[1], ARGV[1]) == 1 then "
        "   redis.call('expire', KEYS[1], ARGV[2]) "
        "   return 'OK' "
        "else "
        "   return nil "
        "end";
    auto start = std::chrono::steady_clock::now();
    while (true) {
        redisReply* reply = command("EVAL %s 1 %s %s %d",
                                    lua_script, lock_key.c_str(), value.c_str(), expire_sec);
        if (reply) {
            if (reply->type == REDIS_REPLY_STRING && std::string(reply->str) == "OK") {
                freeReplyObject(reply);
                return true;
            }
            freeReplyObject(reply);
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() >= timeout_ms) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool RedisClient::renewLock(const std::string& key, const std::string& value, int expire_sec) {
    if (key.empty() || value.empty() || expire_sec <= 0) return false;
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
    if (key.empty() || value.empty()) return false;
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
