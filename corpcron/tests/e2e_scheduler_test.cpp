#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/net/tcp_server.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/scheduler/task_scheduler.hpp"
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
        std::cout << "Skipping e2e scheduler test. Set CORPCRON_RUN_INTEGRATION_TESTS=1 to enable.\n";
        return 77;
    }

    auto redis = std::make_shared<corpcron::RedisClient>(
        getenv_or("CORPCRON_REDIS_HOST", "127.0.0.1"),
        getenv_int_or("CORPCRON_REDIS_PORT", 6379));
    assert(redis->connect());

    auto db = std::make_shared<corpcron::MySQLClient>(
        getenv_or("CORPCRON_MYSQL_HOST", "127.0.0.1"),
        getenv_int_or("CORPCRON_MYSQL_PORT", 3306),
        getenv_or("CORPCRON_MYSQL_USER", "corpcron"),
        getenv_or("CORPCRON_MYSQL_PASSWORD", "corpcron_dev_password"),
        getenv_or("CORPCRON_MYSQL_DATABASE", "corpcron"));
    assert(db->connect());

    int port = getenv_int_or("CORPCRON_E2E_PORT", 18081);
    std::string endpoint = "127.0.0.1:" + std::to_string(port);
    std::string node_id = "e2e-node-" + std::to_string(port);

    corpcron::HandlerRegistry::instance().registerHandler("Echo", [](const std::string& params) {
        return "Echo: " + params;
    });

    corpcron::TcpServer server("127.0.0.1", port, db, redis, 128);
    std::thread server_thread([&]() {
        server.start();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    assert(redis->registerService("rpc", endpoint, 30));

    corpcron::TaskMeta task;
    task.id = unique_id("e2e-task-");
    task.cron_expr = "* * * * * ?";
    task.params = "from e2e";
    task.handler = "Echo";
    task.status = 1;
    task.next_run_at = "2000-01-01 00:00:00";
    task.max_retries = 3;
    assert(db->addTask(task));

    corpcron::TaskScheduler scheduler(db, redis, node_id);
    scheduler.start();

    bool observed = false;
    for (int i = 0; i < 20; ++i) {
        if (db->historyCount(task.id) > 0) {
            observed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    scheduler.stop();
    db->deleteTask(task.id);
    redis->unregisterService("rpc", endpoint);
    server.stop();
    if (server_thread.joinable()) server_thread.join();

    assert(observed);
    return 0;
}
