#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/scheduler/cron_parser.hpp"
#include "rpc.pb.h"
#include <chrono>
#include <ctime>
#include <google/protobuf/message.h>
#include <random>
#include <stdexcept>
#include <utility>

namespace corpcron {

namespace {

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid(36, '-');
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        uuid[i] = hex[dis(gen)];
    }
    return uuid;
}

std::string to_datetime_string(uint64_t timestamp_ms) {
    time_t time_sec = timestamp_ms / 1000;
    std::tm tm_buf;
    localtime_r(&time_sec, &tm_buf);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

std::string make_rpc_error(corpcron::rpc::ErrorCode code, const std::string& message) {
    corpcron::rpc::RpcError error;
    error.set_code(code);
    error.set_message(message);
    std::string data;
    error.SerializeToString(&data);
    return data;
}

RpcResponse make_response(uint32_t serial_id, const google::protobuf::Message& message) {
    RpcResponse response{serial_id, {}};
    message.SerializeToString(&response.payload);
    return response;
}

RpcResponse make_error_response(corpcron::rpc::ErrorCode code, const std::string& message) {
    return RpcResponse{rpc::kRpcErrorSerialId, make_rpc_error(code, message)};
}

} // namespace

RpcDispatcher::RpcDispatcher(std::shared_ptr<MySQLClient> db, std::string auth_token)
    : db_(std::move(db)), auth_token_(std::move(auth_token)) {}

RpcDispatcher::RpcDispatcher(std::shared_ptr<MySQLClient> db, std::shared_ptr<RedisClient> redis, std::string auth_token)
    : db_(std::move(db)), redis_(std::move(redis)), auth_token_(std::move(auth_token)) {}

RpcResponse RpcDispatcher::dispatch(uint32_t serial_id, const std::string& payload) const {
    switch (serial_id) {
        case rpc::kEchoRequestSerialId:
            return handleEcho(payload);
        case rpc::kSubmitTaskRequestSerialId:
            return handleSubmitTask(payload);
        case rpc::kExecuteTaskRequestSerialId:
            return handleExecuteTask(payload);
        case rpc::kCancelTaskRequestSerialId:
            return handleCancelTask(payload);
        case rpc::kListTasksRequestSerialId:
            return handleListTasks(payload);
        case rpc::kListHistoryRequestSerialId:
            return handleListHistory(payload);
        case rpc::kListServicesRequestSerialId:
            return handleListServices(payload);
        case rpc::kUpdateTaskRequestSerialId:
            return handleUpdateTask(payload);
        case rpc::kEnableTaskRequestSerialId:
            return handleEnableTask(payload);
        case rpc::kDeleteTaskRequestSerialId:
            return handleDeleteTask(payload);
        case rpc::kRunTaskNowRequestSerialId:
            return handleRunTaskNow(payload);
        default:
            return make_error_response(corpcron::rpc::UNKNOWN_METHOD,
                                       "Unknown serial_id: " + std::to_string(serial_id));
    }
}

RpcResponse RpcDispatcher::error(corpcron::rpc::ErrorCode code, const std::string& message) {
    return make_error_response(code, message);
}

RpcResponse RpcDispatcher::handleEcho(const std::string& payload) const {
    corpcron::rpc::EchoRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse EchoRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }

    corpcron::rpc::EchoResponse resp;
    resp.set_message(HandlerRegistry::instance().execute("Echo", req.message()));
    return make_response(rpc::kEchoResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleSubmitTask(const std::string& payload) const {
    corpcron::rpc::SubmitTaskRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse SubmitTaskRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }

    TaskMeta task;
    task.id = generate_uuid();
    task.cron_expr = req.cron_expr();
    task.params = req.params();
    task.handler = req.handler();
    task.status = 1;

    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t next_run_ms = CronParser::nextExecution(task.cron_expr, now_ms);

    corpcron::rpc::SubmitTaskResponse resp;
    if (next_run_ms == 0) {
        resp.set_success(false);
        resp.set_error("Invalid cron expression");
        return make_response(rpc::kSubmitTaskResponseSerialId, resp);
    }

    task.next_run_at = to_datetime_string(next_run_ms);
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }
    if (!db_->addTask(task)) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB insert failed");
    }

    resp.set_task_id(task.id);
    resp.set_success(true);
    return make_response(rpc::kSubmitTaskResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleExecuteTask(const std::string& payload) const {
    corpcron::rpc::ExecuteTaskRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse ExecuteTaskRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }

    std::string result;
    std::string error;
    try {
        result = HandlerRegistry::instance().execute(req.handler(), req.params());
    } catch (const std::exception& e) {
        error = e.what();
        result = "Exception: " + error;
    }

    corpcron::rpc::ExecuteTaskResponse resp;
    resp.set_success(error.empty());
    resp.set_result(result);
    resp.set_error(error);
    return make_response(rpc::kExecuteTaskResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleCancelTask(const std::string& payload) const {
    corpcron::rpc::CancelTaskRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse CancelTaskRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }

    corpcron::rpc::CancelTaskResponse resp;
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }
    if (db_->cancelTask(req.task_id())) {
        resp.set_success(true);
    } else {
        resp.set_success(false);
        resp.set_error("Task not found");
    }
    return make_response(rpc::kCancelTaskResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleListTasks(const std::string& payload) const {
    corpcron::rpc::ListTasksRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse ListTasksRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }

    auto tasks = req.enabled_only() ? db_->getEnabledTasks() : db_->getAllTasks();
    int limit = req.limit() > 0 ? req.limit() : 100;

    corpcron::rpc::ListTasksResponse resp;
    resp.set_success(true);
    int count = 0;
    for (const auto& task : tasks) {
        if (count++ >= limit) break;
        auto* item = resp.add_tasks();
        item->set_id(task.id);
        item->set_cron_expr(task.cron_expr);
        item->set_params(task.params);
        item->set_handler(task.handler);
        item->set_status(task.status);
        item->set_next_run_at(task.next_run_at);
        item->set_last_run_at(task.last_run_at);
        item->set_retry_count(task.retry_count);
        item->set_max_retries(task.max_retries);
    }
    return make_response(rpc::kListTasksResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleListHistory(const std::string& payload) const {
    corpcron::rpc::ListHistoryRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse ListHistoryRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }

    int limit = req.limit() > 0 ? req.limit() : 100;
    auto history = db_->getHistory(req.task_id(), static_cast<size_t>(limit));

    corpcron::rpc::ListHistoryResponse resp;
    resp.set_success(true);
    for (const auto& item : history) {
        auto* out = resp.add_history();
        out->set_task_id(item.task_id);
        out->set_exec_node(item.exec_node);
        out->set_success(item.success);
        out->set_result(item.result);
        out->set_error(item.error);
        out->set_start_time(item.start_time);
        out->set_end_time(item.end_time);
    }
    return make_response(rpc::kListHistoryResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleListServices(const std::string& payload) const {
    corpcron::rpc::ListServicesRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse ListServicesRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }
    if (!redis_) {
        return make_error_response(corpcron::rpc::INTERNAL_ERROR, "Redis client is not available");
    }

    std::string service_name = req.service_name().empty() ? "rpc" : req.service_name();
    auto endpoints = redis_->discoverServices(service_name);

    corpcron::rpc::ListServicesResponse resp;
    resp.set_success(true);
    for (const auto& endpoint : endpoints) {
        resp.add_endpoints(endpoint);
    }
    return make_response(rpc::kListServicesResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleUpdateTask(const std::string& payload) const {
    corpcron::rpc::UpdateTaskRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse UpdateTaskRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }
    if (!req.has_task() || req.task().id().empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Task id is required");
    }

    TaskMeta existing;
    if (!db_->getTask(req.task().id(), existing)) {
        corpcron::rpc::UpdateTaskResponse resp;
        resp.set_success(false);
        resp.set_error("Task not found");
        return make_response(rpc::kUpdateTaskResponseSerialId, resp);
    }

    TaskMeta task;
    task.id = req.task().id();
    task.cron_expr = req.task().cron_expr();
    task.params = req.task().params();
    task.handler = req.task().handler();
    task.status = req.task().status();
    task.last_run_at = existing.last_run_at;
    task.retry_count = req.task().retry_count();
    task.max_retries = req.task().max_retries() > 0 ? req.task().max_retries() : 3;

    if (task.cron_expr.empty() || task.handler.empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Cron and handler are required");
    }
    if (task.status == 1) {
        uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t next_ms = CronParser::nextExecution(task.cron_expr, now_ms);
        if (next_ms == 0) {
            corpcron::rpc::UpdateTaskResponse resp;
            resp.set_success(false);
            resp.set_error("Invalid cron expression");
            return make_response(rpc::kUpdateTaskResponseSerialId, resp);
        }
        task.next_run_at = to_datetime_string(next_ms);
    } else {
        task.next_run_at = existing.next_run_at;
    }

    corpcron::rpc::UpdateTaskResponse resp;
    resp.set_success(db_->updateTaskDefinition(task));
    if (!resp.success()) resp.set_error("DB update failed");
    return make_response(rpc::kUpdateTaskResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleEnableTask(const std::string& payload) const {
    corpcron::rpc::EnableTaskRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse EnableTaskRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }

    TaskMeta task;
    corpcron::rpc::EnableTaskResponse resp;
    if (!db_->getTask(req.task_id(), task)) {
        resp.set_success(false);
        resp.set_error("Task not found");
        return make_response(rpc::kEnableTaskResponseSerialId, resp);
    }
    if (!req.enabled()) {
        resp.set_success(db_->cancelTask(req.task_id()));
        if (!resp.success()) resp.set_error("DB update failed");
        return make_response(rpc::kEnableTaskResponseSerialId, resp);
    }

    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t next_ms = CronParser::nextExecution(task.cron_expr, now_ms);
    if (next_ms == 0) {
        resp.set_success(false);
        resp.set_error("Invalid cron expression");
        return make_response(rpc::kEnableTaskResponseSerialId, resp);
    }
    resp.set_success(db_->updateTaskRuntime(task.id, 1, to_datetime_string(next_ms), task.last_run_at, task.retry_count));
    if (!resp.success()) resp.set_error("DB update failed");
    return make_response(rpc::kEnableTaskResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleDeleteTask(const std::string& payload) const {
    corpcron::rpc::DeleteTaskRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse DeleteTaskRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }

    corpcron::rpc::DeleteTaskResponse resp;
    TaskMeta task;
    if (!db_->getTask(req.task_id(), task)) {
        resp.set_success(false);
        resp.set_error("Task not found");
        return make_response(rpc::kDeleteTaskResponseSerialId, resp);
    }
    resp.set_success(db_->deleteTask(req.task_id()));
    if (!resp.success()) resp.set_error("DB delete failed");
    return make_response(rpc::kDeleteTaskResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleRunTaskNow(const std::string& payload) const {
    corpcron::rpc::RunTaskNowRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse RunTaskNowRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }

    corpcron::rpc::RunTaskNowResponse resp;
    TaskMeta task;
    if (!db_->getTask(req.task_id(), task)) {
        resp.set_success(false);
        resp.set_error("Task not found");
        return make_response(rpc::kRunTaskNowResponseSerialId, resp);
    }

    auto start = std::chrono::system_clock::now();
    std::string result;
    std::string error;
    try {
        result = HandlerRegistry::instance().execute(task.handler, task.params);
    } catch (const std::exception& e) {
        error = e.what();
        result = "Exception: " + error;
    }
    auto end = std::chrono::system_clock::now();

    TaskHistory history;
    history.task_id = task.id;
    history.exec_node = "manual-rpc";
    history.success = error.empty();
    history.result = result;
    history.error = error;
    history.start_time = to_datetime_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        start.time_since_epoch()).count());
    history.end_time = to_datetime_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        end.time_since_epoch()).count());
    db_->addHistory(history);
    db_->updateTaskRuntime(task.id, task.status, task.next_run_at, history.end_time, task.retry_count);

    resp.set_success(error.empty());
    resp.set_result(result);
    resp.set_error(error);
    return make_response(rpc::kRunTaskNowResponseSerialId, resp);
}

bool RpcDispatcher::authorized(const std::string& request_token) const {
    return auth_token_.empty() || auth_token_ == request_token;
}

} // namespace corpcron
