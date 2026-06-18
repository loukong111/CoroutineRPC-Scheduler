#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/redis/redis_client.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string getenv_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

int getenv_int_or(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string unique_id(const std::string& prefix) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return prefix + std::to_string(now);
}

} // namespace

int main() {
    const char* run = std::getenv("CORPCRON_RUN_INTEGRATION_TESTS");
    if (!run || std::string(run) != "1") {
        std::cout << "Skipping integration test. Set CORPCRON_RUN_INTEGRATION_TESTS=1 to enable.\n";
        return 77;
    }

    corpcron::RedisClient redis(getenv_or("CORPCRON_REDIS_HOST", "127.0.0.1"),
                                getenv_int_or("CORPCRON_REDIS_PORT", 6379));
    assert(redis.connect());

    std::string service_name = "itest-" + unique_id("");
    std::string endpoint1 = "127.0.0.1:18081";
    std::string endpoint2 = "127.0.0.1:18082";
    assert(redis.registerService(service_name, endpoint1, 30));
    assert(redis.registerService(service_name, endpoint2, 2));
    auto endpoints = redis.discoverServices(service_name);
    assert(std::find(endpoints.begin(), endpoints.end(), endpoint1) != endpoints.end());
    assert(std::find(endpoints.begin(), endpoints.end(), endpoint2) != endpoints.end());
    assert(redis.heartbeat(service_name, endpoint2, 3));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    endpoints = redis.discoverServices(service_name);
    assert(std::find(endpoints.begin(), endpoints.end(), endpoint2) != endpoints.end());

    std::string lock_key = "itest:" + unique_id("");
    assert(redis.lock(lock_key, "owner-a", 5, 1000));
    redis.unlock(lock_key, "owner-b");
    assert(!redis.lock(lock_key, "owner-c", 5, 100));
    assert(redis.renewLock(lock_key, "owner-a", 5));
    redis.unlock(lock_key, "owner-a");
    assert(redis.lock(lock_key, "owner-c", 5, 1000));
    redis.unlock(lock_key, "owner-c");
    redis.unregisterService(service_name, endpoint1);
    redis.unregisterService(service_name, endpoint2);
    endpoints = redis.discoverServices(service_name);
    assert(std::find(endpoints.begin(), endpoints.end(), endpoint1) == endpoints.end());
    assert(std::find(endpoints.begin(), endpoints.end(), endpoint2) == endpoints.end());

    corpcron::MySQLClient db(getenv_or("CORPCRON_MYSQL_HOST", "127.0.0.1"),
                             getenv_int_or("CORPCRON_MYSQL_PORT", 3306),
                             getenv_or("CORPCRON_MYSQL_USER", "corpcron"),
                             getenv_or("CORPCRON_MYSQL_PASSWORD", "corpcron_dev_password"),
                             getenv_or("CORPCRON_MYSQL_DATABASE", "corpcron"));
    assert(db.connect());

    corpcron::TaskMeta task;
    task.id = unique_id("itest-task-");
    task.cron_expr = "* * * * * ?";
    task.params = "integration";
    task.handler = "Echo";
    task.status = 1;
    task.next_run_at = "2000-01-01 00:00:00";
    task.max_retries = 3;
    assert(db.addTask(task));

    corpcron::TaskMeta loaded;
    assert(db.getTask(task.id, loaded));
    assert(loaded.id == task.id);
    assert(loaded.status == 1);
    assert(loaded.next_run_at == "2000-01-01 00:00:00");

    auto tasks = db.getAllTasks();
    auto found = std::find_if(tasks.begin(), tasks.end(), [&](const corpcron::TaskMeta& item) {
        return item.id == task.id;
    });
    assert(found != tasks.end());
    auto due_tasks = db.getDueTasks(100);
    auto due_found = std::find_if(due_tasks.begin(), due_tasks.end(), [&](const corpcron::TaskMeta& item) {
        return item.id == task.id;
    });
    assert(due_found != due_tasks.end());

    assert(db.updateTaskRuntime(task.id, 0, "2099-01-02 00:00:00", "2026-01-01 00:00:00", 1));
    assert(db.getTask(task.id, loaded));
    assert(loaded.status == 0);
    assert(loaded.next_run_at == "2099-01-02 00:00:00");
    assert(loaded.last_run_at == "2026-01-01 00:00:00");
    assert(loaded.retry_count == 1);

    corpcron::TaskHistory history;
    history.task_id = task.id;
    history.exec_node = "integration-node";
    history.success = true;
    history.result = "ok";
    history.start_time = "2026-01-01 00:00:00";
    history.end_time = "2026-01-01 00:00:01";
    assert(db.addHistory(history));
    assert(db.historyCount(task.id) > 0);
    corpcron::TaskHistory latest;
    assert(db.getLatestHistory(task.id, latest));
    assert(latest.task_id == task.id);
    assert(latest.exec_node == "integration-node");
    assert(latest.success);
    assert(latest.result == "ok");
    assert(db.deleteTask(task.id));
    assert(!db.getTask(task.id, loaded));

    return 0;
}
