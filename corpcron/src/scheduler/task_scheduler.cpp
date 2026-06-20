#include "corpcron/scheduler/task_scheduler.hpp"
#include "corpcron/common/config.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/scheduler/cron_parser.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client.hpp"   // for RpcClient
#include "rpc.pb.h"
#include <random> 
#include <chrono>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace corpcron {

static std::string to_datetime_string(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
    //localtime_r 要求调用者提供 tm_buf，各线程用自己的缓冲区，线程安全
    localtime_r(&time_t, &tm_buf);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

static std::string to_datetime_string(uint64_t timestamp_ms) {
    time_t time_sec = timestamp_ms / 1000;
    std::tm tm_buf;
    localtime_r(&time_sec, &tm_buf);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

//每失败一次等待时间就增加，最多等待5分钟
static int retry_delay_seconds(int retry_count) {
    int exponent = std::min(std::max(retry_count - 1, 0), 6);
    return std::min(300, 5 * (1 << exponent));
}

static bool parse_datetime(const std::string& value, std::chrono::system_clock::time_point& out) {
    if (value.empty()) return false;
    std::tm tm_buf{};
    std::istringstream ss(value);
    ss >> std::get_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return false;
    out = std::chrono::system_clock::from_time_t(std::mktime(&tm_buf));
    return true;
}

TaskScheduler::TaskScheduler(std::shared_ptr<MySQLClient> db,
                             std::shared_ptr<RedisClient> redis,
                             const std::string& node_id)
    : db_(db), redis_(redis), node_id_(node_id), running_(false),
      thread_pool_(std::make_unique<DynamicThreadPool>(2, 8, 50, 5)),
      lock_renewer_(std::make_unique<LockRenewer>(redis_)) {}

TaskScheduler::~TaskScheduler() {
    stop();
    if (lock_renewer_) lock_renewer_->stop();
    if (thread_pool_) thread_pool_->stop();
}

void TaskScheduler::start() {
    if (running_) return;
    running_ = true;
    if (lock_renewer_) lock_renewer_->start();
    thread_ = std::make_unique<std::thread>(&TaskScheduler::schedulerLoop, this);
}

void TaskScheduler::stop() {
    running_ = false;
    loop_cv_.notify_all();
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    if (lock_renewer_) lock_renewer_->stop();
}

void TaskScheduler::schedulerLoop() {
    while (running_) {
        try {
            scanAndDispatch();
        } catch (const std::exception& e) {
            LOG_ERROR("Scheduler error: " + std::string(e.what()));
        }
        std::unique_lock<std::mutex> lock(loop_mutex_);
        loop_cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return !running_; });
    }
}

void TaskScheduler::scanAndDispatch() {
    auto tasks = db_->getDueTasks(100);
    std::string misfire_policy = Config::instance().get("scheduler.misfire_policy", "once");
    int misfire_grace_seconds = Config::instance().getInt("scheduler.misfire_grace_seconds", 300);

    for (auto& task : tasks) {
        if (misfire_policy == "skip") {
            std::chrono::system_clock::time_point due_at;
            if (parse_datetime(task.next_run_at, due_at)) {
                auto now = std::chrono::system_clock::now();
                auto overdue = std::chrono::duration_cast<std::chrono::seconds>(now - due_at).count();
                if (overdue > misfire_grace_seconds) {
                    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
                    uint64_t next_ms = CronParser::nextExecution(task.cron_expr, now_ms);
                    if (next_ms != 0) {
                        db_->updateTaskRuntime(task.id, 1, to_datetime_string(next_ms), task.last_run_at, task.retry_count);
                    } else {
                        db_->updateTaskRuntime(task.id, 0, task.next_run_at, task.last_run_at, task.retry_count);
                    }
                    LOG_INFO("Skipped misfired task " + task.id + " overdue by " +
                             std::to_string(overdue) + " seconds");
                    continue;
                }
            }
        }

        std::string lock_key = "task:" + task.id;
        std::string lock_value = node_id_;
        constexpr int lock_ttl_sec = 60;
        if (redis_->lock(lock_key, lock_value, lock_ttl_sec, 100)) {
            thread_pool_->enqueue([this, task, lock_key, lock_value]() {
                constexpr int lock_ttl_sec = 60;
                lock_renewer_->add(lock_key, lock_value, lock_ttl_sec);

                auto start = std::chrono::system_clock::now();
                ExecutionOutcome outcome;
                try {
                    outcome = executeTask(task);
                } catch (const std::exception& e) {
                    outcome.success = false;
                    outcome.error = e.what();
                }
                auto end = std::chrono::system_clock::now();

                TaskHistory history;
                history.task_id = task.id;
                history.exec_node = node_id_;
                history.success = outcome.success;
                history.result = outcome.result;
                history.error = outcome.error;
                history.start_time = to_datetime_string(start);
                history.end_time = to_datetime_string(end);
                db_->addHistory(history);

                uint64_t end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end.time_since_epoch()).count();
                int next_status = 1;
                int next_retry_count = 0;
                std::string next_run_at = history.end_time;

                if (outcome.success) {
                    uint64_t next_ms = CronParser::nextExecution(task.cron_expr, end_ms);
                    if (next_ms != 0) {
                        next_run_at = to_datetime_string(next_ms);
                    } else {
                        next_status = 0;
                    }
                } else {
                    next_retry_count = task.retry_count + 1;
                    if (next_retry_count >= task.max_retries) {
                        next_status = 0;
                        LOG_WARN("Task " + task.id + " disabled after " +
                                 std::to_string(next_retry_count) + " failed attempts");
                    } else {
                        auto retry_at = end + std::chrono::seconds(retry_delay_seconds(next_retry_count));
                        next_run_at = to_datetime_string(retry_at);
                    }
                }

                db_->updateTaskRuntime(task.id, next_status, next_run_at, history.end_time, next_retry_count);
                lock_renewer_->remove(lock_key);
                if (!redis_->unlock(lock_key, lock_value)) {
                    LOG_WARN("Failed to unlock " + lock_key + " as " + lock_value);
                }
            });
        } else {
            LOG_INFO("Node " + node_id_ + " failed to get lock for task " + task.id);
        }
    }
}

TaskScheduler::ExecutionOutcome TaskScheduler::executeTask(const TaskMeta& task) {
    ExecutionOutcome outcome;
    auto endpoints = redis_->discoverServices("rpc");
    if (endpoints.empty()) {
        outcome.error = "No RPC service available";
        LOG_ERROR(outcome.error + " for task " + task.id);
        return outcome;
    }
    //std::mt19937不是多线程安全的，要让每个线程有自己的副本
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, endpoints.size() - 1);
    std::string endpoint = endpoints[dist(rng)];
    size_t colon = endpoint.find(':');
    if (colon == std::string::npos || colon + 1 >= endpoint.size()) {
        outcome.error = "Invalid RPC endpoint: " + endpoint;
        return outcome;
    }
    std::string host = endpoint.substr(0, colon);
    int port = std::stoi(endpoint.substr(colon + 1));

    // 构造 ExecuteTaskRequest
    corpcron::rpc::ExecuteTaskRequest req;
    req.set_task_id(task.id);
    req.set_params(task.params);
    req.set_handler(task.handler);
    req.set_auth_token(Config::instance().get("rpc.auth_token", ""));
    std::string req_data;
    req.SerializeToString(&req_data);

    RpcClient client(host, port);
    uint32_t response_serial_id = 0;
    std::string resp_data;
    if (client.call(corpcron::rpc::kExecuteTaskRequestSerialId, req_data, response_serial_id, resp_data, 5000)) {
        if (response_serial_id == corpcron::rpc::kRpcErrorSerialId) {
            corpcron::rpc::RpcError error;
            if (error.ParseFromString(resp_data)) {
                outcome.error = "RPC error " + std::to_string(error.code()) + ": " + error.message();
            } else {
                outcome.error = "RPC error response parse failed";
            }
            LOG_ERROR(outcome.error);
            return outcome;
        }
        corpcron::rpc::ExecuteTaskResponse resp;
        if (resp.ParseFromString(resp_data)) {
            LOG_INFO("Remote execution result: " + resp.result());
            outcome.success = resp.success();
            outcome.result = resp.result();
            outcome.error = resp.error();
        } else {
            outcome.error = "Failed to parse ExecuteTaskResponse";
            LOG_ERROR(outcome.error);
        }
    } else {
        outcome.error = "RPC call to " + endpoint + " failed";
        LOG_ERROR(outcome.error);
    }
    return outcome;
}

TaskScheduler::LockRenewer::LockRenewer(std::shared_ptr<RedisClient> redis)
    : redis_(std::move(redis)) {}

TaskScheduler::LockRenewer::~LockRenewer() {
    stop();
}

void TaskScheduler::LockRenewer::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&LockRenewer::loop, this);
}

void TaskScheduler::LockRenewer::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
        leases_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void TaskScheduler::LockRenewer::add(const std::string& key, const std::string& value, int ttl_sec) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        leases_[key] = Lease{key, value, ttl_sec, nextRenewTime(ttl_sec)};
    }
    cv_.notify_all();
}

void TaskScheduler::LockRenewer::remove(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        leases_.erase(key);
    }
    cv_.notify_all();
}

void TaskScheduler::LockRenewer::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
        if (leases_.empty()) {
            cv_.wait(lock, [this]() { return !running_ || !leases_.empty(); });
            continue;
        }

        auto next_time = leases_.begin()->second.next_renew_at;
        for (const auto& [_, lease] : leases_) {
            next_time = std::min(next_time, lease.next_renew_at);
        }

        if (cv_.wait_until(lock, next_time, [this]() { return !running_; })) {
            continue;
        }
        if (!running_) break;

        auto now = std::chrono::steady_clock::now();
        std::vector<Lease> due;
        for (auto& [_, lease] : leases_) {
            if (lease.next_renew_at <= now) {
                due.push_back(lease);
            }
        }

        lock.unlock();
        for (const auto& lease : due) {
            if (!redis_->renewLock(lease.key, lease.value, lease.ttl_sec)) {
                LOG_WARN("Failed to renew lock for " + lease.key);
            }
        }
        lock.lock();

        now = std::chrono::steady_clock::now();
        for (const auto& lease : due) {
            auto it = leases_.find(lease.key);
            if (it != leases_.end() && it->second.value == lease.value) {
                it->second.next_renew_at = now + std::chrono::seconds(std::max(1, lease.ttl_sec / 3));
            }
        }
    }
}

std::chrono::steady_clock::time_point TaskScheduler::LockRenewer::nextRenewTime(int ttl_sec) const {
    return std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, ttl_sec / 3));
}

} // namespace corpcron
