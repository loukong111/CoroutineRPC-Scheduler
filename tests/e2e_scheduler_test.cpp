#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/net/tcp_server.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/scheduler/task_scheduler.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
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

corpcron::TaskMeta make_task(const std::string& id,
                             const std::string& handler,
                             const std::string& params,
                             int max_retries = 3) {
    corpcron::TaskMeta task;
    task.id = id;
    task.cron_expr = "0 0 0 1 1 ?";
    task.params = params;
    task.handler = handler;
    task.status = 1;
    task.next_run_at = "2000-01-01 00:00:00";
    task.max_retries = max_retries;
    return task;
}

bool wait_for_history(const std::shared_ptr<corpcron::MySQLClient>& db,
                      const std::string& task_id,
                      int expected_count,
                      int timeout_sec) {
    for (int i = 0; i < timeout_sec; ++i) {
        if (db->historyCount(task_id) >= expected_count) return true;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return db->historyCount(task_id) >= expected_count;
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
        getenv_int_or("CORPCRON_REDIS_PORT", 6380));
    assert(redis->connect());

    auto db = std::make_shared<corpcron::MySQLClient>(
        getenv_or("CORPCRON_MYSQL_HOST", "127.0.0.1"),
        getenv_int_or("CORPCRON_MYSQL_PORT", 3307),
        getenv_or("CORPCRON_MYSQL_USER", "corpcron"),
        getenv_or("CORPCRON_MYSQL_PASSWORD", "corpcron_dev_password"),
        getenv_or("CORPCRON_MYSQL_DATABASE", "corpcron"));
    assert(db->connect());

    int port = getenv_int_or("CORPCRON_E2E_PORT", 18081);
    std::string endpoint = "127.0.0.1:" + std::to_string(port);
    std::string node_id = "e2e-node-" + std::to_string(port);
    std::string service_name = unique_id("e2e-rpc-");
    std::string echo_handler = unique_id("E2EEcho");
    std::string fail_handler = unique_id("E2EFail");

    corpcron::HandlerRegistry::instance().registerHandler(
        echo_handler, [](const std::string& params) {
            return "Echo: " + params;
        });
    corpcron::HandlerRegistry::instance().registerHandler(
        fail_handler, [](const std::string& params) -> std::string {
            throw std::runtime_error("boom: " + params);
        });

    corpcron::RpcDispatcherOptions worker_options;
    worker_options.role = corpcron::RpcNodeRole::Worker;
    worker_options.service_name = service_name;
    worker_options.worker_service_name = service_name;
    auto dispatcher = std::make_shared<corpcron::RpcDispatcher>(
        nullptr, redis, "", node_id, worker_options);
    corpcron::TcpServer server("127.0.0.1", port, dispatcher, 128);
    std::thread server_thread([&]() {
        server.start();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    assert(redis->registerService(service_name, endpoint, 30));
    assert(redis->registerService(service_name + ":" + echo_handler, endpoint, 30));
    assert(redis->registerService(service_name + ":" + fail_handler, endpoint, 30));

    auto manual_task =
        make_task(unique_id("e2e-manual-"), echo_handler, "run-now");
    manual_task.next_run_at = "2099-01-01 00:00:00";
    assert(db->addTask(manual_task));

    corpcron::RpcDispatcherOptions control_options;
    control_options.role = corpcron::RpcNodeRole::ControlPlane;
    control_options.service_name = "e2e-control";
    control_options.worker_service_name = service_name;
    auto control_dispatcher = std::make_shared<corpcron::RpcDispatcher>(
        db, redis, "", node_id + "-control", control_options);

    corpcron::rpc::RunTaskNowRequest run_now;
    run_now.set_task_id(manual_task.id);
    std::string run_now_payload;
    assert(run_now.SerializeToString(&run_now_payload));
    auto run_now_response = control_dispatcher->dispatch(
        corpcron::rpc::kRunTaskNowRequestSerialId, run_now_payload);
    assert(run_now_response.serial_id ==
           corpcron::rpc::kRunTaskNowResponseSerialId);
    corpcron::rpc::RunTaskNowResponse run_now_result;
    assert(run_now_result.ParseFromString(run_now_response.payload));
    assert(run_now_result.success());
    assert(run_now_result.result() == "Echo: run-now");

    corpcron::TaskHistory manual_history;
    assert(db->getLatestHistory(manual_task.id, manual_history));
    assert(manual_history.success);
    assert(manual_history.exec_node == endpoint);
    corpcron::TaskMeta loaded_manual;
    assert(db->getTask(manual_task.id, loaded_manual));
    assert(loaded_manual.status == corpcron::TASK_SCHEDULED);

    auto success_task =
        make_task(unique_id("e2e-success-"), echo_handler, "from e2e");
    auto failure_task =
        make_task(unique_id("e2e-failure-"), fail_handler, "retry-disabled", 1);
    auto multi_node_task =
        make_task(unique_id("e2e-multinode-"), echo_handler, "run-once");
    auto unavailable_task = make_task(
        unique_id("e2e-unavailable-"), unique_id("MissingHandler"), "wait", 1);
    assert(db->addTask(success_task));
    assert(db->addTask(failure_task));
    assert(db->addTask(multi_node_task));
    assert(db->addTask(unavailable_task));

    corpcron::TaskScheduler scheduler_a(db, redis, node_id + "-a", service_name);
    corpcron::TaskScheduler scheduler_b(db, redis, node_id + "-b", service_name);
    scheduler_a.start();
    scheduler_b.start();

    bool success_observed = wait_for_history(db, success_task.id, 1, 25);
    bool failure_observed = wait_for_history(db, failure_task.id, 1, 25);
    bool multi_node_observed = wait_for_history(db, multi_node_task.id, 1, 25);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    scheduler_a.stop();
    scheduler_b.stop();

    corpcron::TaskHistory success_history;
    assert(db->getLatestHistory(success_task.id, success_history));
    assert(success_history.success);
    assert(success_history.result == "Echo: from e2e");
    assert(success_history.exec_node == endpoint);

    corpcron::TaskHistory failure_history;
    assert(db->getLatestHistory(failure_task.id, failure_history));
    assert(!failure_history.success);
    assert(failure_history.error.find("boom: retry-disabled") != std::string::npos);
    corpcron::TaskMeta loaded_failure;
    assert(db->getTask(failure_task.id, loaded_failure));
    assert(loaded_failure.status == 0);
    assert(loaded_failure.retry_count == 1);

    assert(db->historyCount(multi_node_task.id) == 1);
    corpcron::TaskMeta loaded_unavailable;
    assert(db->getTask(unavailable_task.id, loaded_unavailable));
    assert(loaded_unavailable.status == corpcron::TASK_SCHEDULED);
    assert(loaded_unavailable.retry_count == 0);
    assert(db->historyCount(unavailable_task.id) == 0);

    setenv("CORPCRON_SCHEDULER_MISFIRE_POLICY", "skip", 1);
    setenv("CORPCRON_SCHEDULER_MISFIRE_GRACE_SECONDS", "0", 1);
    auto misfire_task =
        make_task(unique_id("e2e-misfire-"), echo_handler, "should-skip");
    assert(db->addTask(misfire_task));
    corpcron::TaskScheduler scheduler_c(db, redis, node_id + "-misfire", service_name);
    scheduler_c.start();
    std::this_thread::sleep_for(std::chrono::seconds(7));
    scheduler_c.stop();
    unsetenv("CORPCRON_SCHEDULER_MISFIRE_POLICY");
    unsetenv("CORPCRON_SCHEDULER_MISFIRE_GRACE_SECONDS");

    corpcron::TaskMeta loaded_misfire;
    assert(db->getTask(misfire_task.id, loaded_misfire));
    assert(db->historyCount(misfire_task.id) == 0);
    assert(loaded_misfire.status == 1);
    assert(loaded_misfire.next_run_at != misfire_task.next_run_at);

    db->deleteTask(success_task.id);
    db->deleteTask(failure_task.id);
    db->deleteTask(multi_node_task.id);
    db->deleteTask(unavailable_task.id);
    db->deleteTask(misfire_task.id);
    db->deleteTask(manual_task.id);
    redis->unregisterService(service_name + ":" + fail_handler, endpoint);
    redis->unregisterService(service_name + ":" + echo_handler, endpoint);
    redis->unregisterService(service_name, endpoint);
    server.stop();
    if (server_thread.joinable()) server_thread.join();

    assert(success_observed);
    assert(failure_observed);
    assert(multi_node_observed);
    return 0;
}
