#include "corpcron/scheduler/task_scheduler.hpp"
#include "corpcron/scheduler/cron_parser.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/rpc_client.hpp"   // for RpcClient
#include "rpc.pb.h"
#include <random> 
#include <iostream>
#include <chrono>
#include <thread>

namespace corpcron {

static std::string to_datetime_string(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
    localtime_r(&time_t, &tm_buf);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

TaskScheduler::TaskScheduler(std::shared_ptr<MySQLClient> db,
                             std::shared_ptr<RedisClient> redis,
                             const std::string& node_id)
    : db_(db), redis_(redis), node_id_(node_id), running_(false),
      thread_pool_(std::make_unique<DynamicThreadPool>(2, 8, 50, 5)) {}

TaskScheduler::~TaskScheduler() {
    stop();
    if (thread_pool_) thread_pool_->stop();
}

void TaskScheduler::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::make_unique<std::thread>(&TaskScheduler::schedulerLoop, this);
}

void TaskScheduler::stop() {
    running_ = false;
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

void TaskScheduler::schedulerLoop() {
    while (running_) {
        try {
            scanAndDispatch();
        } catch (const std::exception& e) {
            std::cerr << "Scheduler error: " << e.what() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void TaskScheduler::scanAndDispatch() {
    auto tasks = db_->getEnabledTasks();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (auto& task : tasks) {
        uint64_t next_time = CronParser::nextExecution(task.cron_expr, now_ms);
        if (next_time == 0) continue;

        if (next_time <= now_ms + 5000) {
            bool should_run = false;
            {
                std::lock_guard<std::mutex> lock(last_fire_mutex_);
                auto it = last_fire_ms_.find(task.id);
                if (it == last_fire_ms_.end() || (now_ms - it->second) >= 60000) {
                    last_fire_ms_[task.id] = now_ms;
                    should_run = true;
                }
            }
            if (!should_run) continue;
            std::string lock_key = "task:" + task.id;   // 注意前缀，保持与 lock 函数内部一致（lock 函数会再加 "lock:"）
            std::string lock_value = node_id_;
            if (redis_->lock(lock_key, lock_value, 10, 100)) {
                thread_pool_->enqueue([this, task, lock_key, lock_value]() {
                    executeTask(task);
                    redis_->unlock(lock_key, lock_value);
                });
            } else{
                // 可选：打印日志
                std::cout << "Node " << node_id_ << " failed to get lock for task " << task.id << std::endl;
            }
        }
    }
}

void TaskScheduler::executeTask(const TaskMeta& task) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    // 获取服务列表
    auto endpoints = redis_->discoverServices("rpc");
    if (endpoints.empty()) {
        std::cerr << "No RPC service available for task " << task.id << std::endl;
        return;
    }
    // 随机选择一个节点
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, endpoints.size() - 1);
    std::string endpoint = endpoints[dist(rng)];
    size_t colon = endpoint.find(':');
    std::string host = endpoint.substr(0, colon);
    int port = std::stoi(endpoint.substr(colon + 1));

    // 构造 ExecuteTaskRequest
    corpcron::rpc::ExecuteTaskRequest req;
    req.set_task_id(task.id);
    req.set_params(task.params);
    req.set_handler(task.handler);
    std::string req_data;
    req.SerializeToString(&req_data);

    RpcClient client(host, port);
    std::string resp_data;
    if (client.call(5, req_data, resp_data, 5000)) {
        corpcron::rpc::ExecuteTaskResponse resp;
        if (resp.ParseFromString(resp_data)) {
            std::cout << "Remote execution result: " << resp.result() << std::endl;
            // 可选：由执行节点记录历史，或由调用节点记录
        } else {
            std::cerr << "Failed to parse ExecuteTaskResponse" << std::endl;
        }
    } else {
        std::cerr << "RPC call to " << endpoint << " failed" << std::endl;
    }
}

} // namespace corpcron