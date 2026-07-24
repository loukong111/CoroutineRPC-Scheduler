#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/metrics/metrics.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/rpc_client_pool.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_interceptor.hpp"
#include "corpcron/scheduler/cron_parser.hpp"
#include "corpcron/scheduler/task_cancellation_registry.hpp"
#include "rpc.pb.h"
#include <chrono>
#include <ctime>
#include <google/protobuf/message.h>
#include <random>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace corpcron {

namespace {

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
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
    std::tm tm_buf{};
    if (localtime_r(&time_sec, &tm_buf) == nullptr) return {};
    char buf[20];
    if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf) == 0) return {};
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

void fill_metrics_response(corpcron::rpc::GetMetricsResponse& resp) {
    auto snapshot = Metrics::instance().snapshot();
    resp.set_success(true);
    resp.set_rpc_requests_total(snapshot.rpc_requests_total);
    resp.set_rpc_success_total(snapshot.rpc_success_total);
    resp.set_rpc_error_total(snapshot.rpc_error_total);
    resp.set_active_connections(snapshot.active_connections);
    resp.set_rejected_connections(snapshot.rejected_connections);
    resp.set_malformed_frames(snapshot.malformed_frames);
    resp.set_bytes_in_total(snapshot.bytes_in_total);
    resp.set_bytes_out_total(snapshot.bytes_out_total);
    resp.set_task_success_total(snapshot.task_success_total);
    resp.set_task_failure_total(snapshot.task_failure_total);
    resp.set_lock_acquire_success_total(snapshot.lock_acquire_success_total);
    resp.set_lock_acquire_failure_total(snapshot.lock_acquire_failure_total);
    resp.set_max_task_duration_ms(snapshot.max_task_duration_ms);
    resp.set_task_duration_p95_ms(snapshot.task_duration_p95_ms);
    resp.set_task_duration_p99_ms(snapshot.task_duration_p99_ms);
    resp.set_task_duration_samples_total(snapshot.task_duration_samples_total);
    resp.set_schedule_delay_max_ms(snapshot.schedule_delay_max_ms);
    resp.set_schedule_delay_p95_ms(snapshot.schedule_delay_p95_ms);
    resp.set_schedule_delay_p99_ms(snapshot.schedule_delay_p99_ms);
    resp.set_schedule_delay_samples_total(snapshot.schedule_delay_samples_total);
}

} // namespace

RpcDispatcher::RpcDispatcher(std::shared_ptr<MySQLClient> db, std::string auth_token)
    : RpcDispatcher(std::move(db), nullptr, std::move(auth_token), {},
                    RpcDispatcherOptions{}) {}

RpcDispatcher::RpcDispatcher(std::shared_ptr<MySQLClient> db, std::shared_ptr<RedisClient> redis,
                             std::string auth_token, std::string node_id,
                             RpcDispatcherOptions options)
    : db_(std::move(db)),
      redis_(std::move(redis)),
      auth_token_(std::move(auth_token)),
      node_id_(std::move(node_id)),
      options_(std::move(options)) {
    if (options_.worker_timeout_ms <= 0) options_.worker_timeout_ms = 5000;
    if (options_.service_name.empty()) options_.service_name = "rpc";
    if (options_.worker_service_name.empty()) options_.worker_service_name = "worker";
    if (redis_) {
        RpcClientPool::Options pool_options;
        pool_options.health_check_service_name = options_.worker_service_name;
        worker_pool_ = std::make_unique<RpcClientPool>(std::move(pool_options));
    }
}

RpcDispatcher::~RpcDispatcher() = default;

RpcResponse RpcDispatcher::dispatch(uint32_t serial_id, const std::string& payload) const {
    RpcContext context;
    context.request_serial_id = serial_id;
    context.request_bytes = payload.size();
    context.started_at = std::chrono::steady_clock::now();
    return dispatchWithContext(std::move(context), payload);
}

RpcResponse RpcDispatcher::dispatchWithContext(RpcContext context, const std::string& payload) const {
    context.request_bytes = payload.size();
    if (context.started_at.time_since_epoch().count() == 0) {
        context.started_at = std::chrono::steady_clock::now();
    }
    auto terminal = [this, &payload](RpcContext& current) {
        return dispatchCore(current.request_serial_id, payload);
    };
    if (interceptors_ && !interceptors_->empty()) {
        return interceptors_->invoke(context, terminal);
    }
    return terminal(context);
}

RpcStreamResult RpcDispatcher::dispatchStreamWithContext(RpcContext context,
                                                         const std::string& payload) const {
    if (context.request_serial_id != rpc::kStreamMetricsRequestSerialId) {
        RpcStreamResult result;
        result.responses.push_back(dispatchWithContext(std::move(context), payload));
        return result;
    }
    if (!methodAllowed(context.request_serial_id)) {
        return RpcStreamResult{{make_error_response(
            rpc::UNKNOWN_METHOD, "Method is not available on this node role")}};
    }

    if (context.started_at.time_since_epoch().count() == 0) {
        context.started_at = std::chrono::steady_clock::now();
    }
    Metrics::instance().incRpcRequest();
    LOG_INFO_EVENT("rpc_stream_request", {
        {"request_id", context.request_id},
        {"remote", context.remote},
        {"method", "StreamMetrics"},
        {"serial_id", std::to_string(context.request_serial_id)},
        {"payload_bytes", std::to_string(payload.size())}
    });

    RpcStreamResult result;
    try {
        result = handleStreamMetrics(payload);
    } catch (const std::exception& e) {
        result.responses.push_back(make_error_response(rpc::INTERNAL_ERROR, e.what()));
    } catch (...) {
        result.responses.push_back(
            make_error_response(rpc::INTERNAL_ERROR, "Unknown streaming RPC exception"));
    }
    bool success = !result.responses.empty() &&
                   result.responses.front().serial_id != rpc::kRpcErrorSerialId;
    if (success) {
        Metrics::instance().incRpcSuccess();
    } else {
        Metrics::instance().incRpcError();
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - context.started_at).count();
    LOG_INFO_EVENT("rpc_stream_response", {
        {"request_id", context.request_id},
        {"remote", context.remote},
        {"method", "StreamMetrics"},
        {"frames", std::to_string(result.responses.size())},
        {"success", success ? "true" : "false"},
        {"elapsed_ms", std::to_string(elapsed)}
    });
    return result;
}

void RpcDispatcher::setInterceptors(std::shared_ptr<RpcInterceptorChain> interceptors) {
    interceptors_ = std::move(interceptors);
}

RpcResponse RpcDispatcher::dispatchCore(uint32_t serial_id, const std::string& payload) const {
    if (!methodAllowed(serial_id)) {
        return make_error_response(corpcron::rpc::UNKNOWN_METHOD,
                                   "Method is not available on this node role");
    }
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
        case rpc::kGetMetricsRequestSerialId:
            return handleGetMetrics(payload);
        case rpc::kHealthCheckRequestSerialId:
            return handleHealthCheck(payload);
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
    resp.set_message("Echo: " + req.message());
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
    task.status = TASK_SCHEDULED;

    if (task.cron_expr.empty() || task.handler.empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Cron and handler are required");
    }
    if (!handlerAvailable(task.handler)) {
        return make_error_response(corpcron::rpc::HANDLER_NOT_FOUND,
                                   "Handler not found: " + task.handler);
    }

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
    if (req.handler().empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Handler is required");
    }
    if (!HandlerRegistry::instance().hasHandler(req.handler())) {
        return make_error_response(corpcron::rpc::HANDLER_NOT_FOUND,
                                   "Handler not found: " + req.handler());
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (req.deadline_unix_ms() > 0 &&
        static_cast<uint64_t>(now_ms) >= req.deadline_unix_ms()) {
        return make_error_response(corpcron::rpc::DEADLINE_EXCEEDED,
                                   "Task deadline exceeded before execution");
    }

    std::string result;
    std::string error;
    try {
        TaskExecutionContext context;
        context.task_id = req.task_id();
        context.execution_id = req.execution_id();
        context.deadline_unix_ms = req.deadline_unix_ms();
        result = HandlerRegistry::instance().execute(req.handler(), context, req.params());
    } catch (const std::exception& e) {
        error = e.what();
        result = "Exception: " + error;
    } catch (...) {
        error = "Unknown handler exception";
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
    if (req.task_id().empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Task id is required");
    }

    corpcron::rpc::CancelTaskResponse resp;
    if (!db_) {
        return make_error_response(corpcron::rpc::DB_ERROR, "DB client is not available");
    }
    if (db_->cancelTask(req.task_id())) {
        resp.set_success(true);
        if (TaskCancellationRegistry::instance().cancel(req.task_id())) {
            LOG_INFO_EVENT("task_cancellation_requested", {{"task_id", req.task_id()}});
        }
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

    int limit = req.limit() > 0 ? req.limit() : 100;
    if (limit > 500) limit = 500;

    TaskQuery query;
    query.limit = static_cast<size_t>(limit);
    query.offset = req.offset() > 0 ? static_cast<size_t>(req.offset()) : 0;
    query.keyword = req.keyword();
    query.status_filter = -1;
    if (req.has_status_filter()) {
        query.status_filter = req.status_filter();
    } else if (req.enabled_only()) {
        query.status_filter = TASK_SCHEDULED;
    }
    auto page = db_->listTasks(query);

    corpcron::rpc::ListTasksResponse resp;
    resp.set_success(true);
    resp.set_total(static_cast<int>(page.total));
    resp.set_offset(static_cast<int>(query.offset));
    resp.set_limit(limit);
    for (const auto& task : page.items) {
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
        item->set_current_execution_id(task.current_execution_id);
        item->set_running_node(task.running_node);
        item->set_started_at(task.started_at);
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
    if (limit > 500) limit = 500;

    HistoryQuery query;
    query.task_id = req.task_id();
    query.limit = static_cast<size_t>(limit);
    query.offset = req.offset() > 0 ? static_cast<size_t>(req.offset()) : 0;
    query.success_filter = req.has_success_filter() ? req.success_filter() : -1;
    query.keyword = req.keyword();
    auto page = db_->listHistory(query);

    corpcron::rpc::ListHistoryResponse resp;
    resp.set_success(true);
    resp.set_total(static_cast<int>(page.total));
    resp.set_offset(static_cast<int>(query.offset));
    resp.set_limit(limit);
    for (const auto& item : page.items) {
        auto* out = resp.add_history();
        out->set_execution_id(item.execution_id);
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
    if (existing.status == TASK_RUNNING) {
        corpcron::rpc::UpdateTaskResponse resp;
        resp.set_success(false);
        resp.set_error("Running task cannot be edited; cancel it first");
        return make_response(rpc::kUpdateTaskResponseSerialId, resp);
    }

    TaskMeta task;
    task.id = req.task().id();
    task.cron_expr = req.task().cron_expr();
    task.params = req.task().params();
    task.handler = req.task().handler();
    task.status = req.task().status();
    task.last_run_at = existing.last_run_at;
    task.retry_count = existing.retry_count;
    task.max_retries = req.task().max_retries() > 0 ? req.task().max_retries() : 3;

    if (task.cron_expr.empty() || task.handler.empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Cron and handler are required");
    }
    if (task.status != TASK_DISABLED && task.status != TASK_SCHEDULED) {
        return make_error_response(corpcron::rpc::BAD_REQUEST,
                                   "Task status must be disabled or scheduled");
    }
    if (!handlerAvailable(task.handler)) {
        return make_error_response(corpcron::rpc::HANDLER_NOT_FOUND,
                                   "Handler not found: " + task.handler);
    }
    if (task.status == TASK_SCHEDULED) {
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
    if (req.task_id().empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Task id is required");
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
        if (resp.success()) {
            TaskCancellationRegistry::instance().cancel(req.task_id());
        } else {
            resp.set_error("DB update failed");
        }
        return make_response(rpc::kEnableTaskResponseSerialId, resp);
    }
    if (task.status == TASK_RUNNING) {
        resp.set_success(true);
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
    resp.set_success(db_->updateTaskRuntime(task.id, TASK_SCHEDULED, to_datetime_string(next_ms), task.last_run_at, task.retry_count));
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
    if (req.task_id().empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Task id is required");
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
    if (task.status == TASK_RUNNING) {
        resp.set_success(false);
        resp.set_error("Running task cannot be deleted; cancel it first");
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
    if (req.task_id().empty()) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Task id is required");
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
    if (task.status == TASK_RUNNING) {
        resp.set_success(false);
        resp.set_error("Task is already running");
        return make_response(rpc::kRunTaskNowResponseSerialId, resp);
    }
    if (task.status != TASK_DISABLED && task.status != TASK_SCHEDULED) {
        resp.set_success(false);
        resp.set_error("Task has an invalid status");
        return make_response(rpc::kRunTaskNowResponseSerialId, resp);
    }
    if (!handlerAvailable(task.handler)) {
        return make_error_response(corpcron::rpc::HANDLER_NOT_FOUND,
                                   "Handler not found: " + task.handler);
    }

    const std::string execution_id = "manual:" + task.id + ":" + generate_uuid();
    const std::string dispatch_node = node_id_.empty() ? "manual-rpc" : node_id_;
    if (!db_->claimTaskExecution(task.id, execution_id, dispatch_node, task.status)) {
        resp.set_success(false);
        resp.set_error("Task state changed before execution");
        return make_response(rpc::kRunTaskNowResponseSerialId, resp);
    }

    auto start = std::chrono::system_clock::now();
    auto steady_start = std::chrono::steady_clock::now();
    WorkerResult worker_result =
        invokeWorker(task.id, execution_id, task.handler, task.params);
    std::string result = worker_result.result;
    std::string error = worker_result.error;
    auto end = std::chrono::system_clock::now();
    auto duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - steady_start).count());
    Metrics::instance().observeTaskDuration(duration_ms);
    if (worker_result.success) {
        Metrics::instance().incTaskSuccess();
    } else {
        Metrics::instance().incTaskFailure();
    }

    TaskHistory history;
    history.execution_id = execution_id;
    history.task_id = task.id;
    history.exec_node = worker_result.endpoint.empty() ? dispatch_node : worker_result.endpoint;
    history.success = worker_result.success;
    history.result = result;
    history.error = error;
    history.start_time = to_datetime_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        start.time_since_epoch()).count());
    history.end_time = to_datetime_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        end.time_since_epoch()).count());
    const bool history_saved = db_->addHistory(history);
    const bool state_completed = db_->completeTaskExecution(
        task.id, execution_id, task.status, task.next_run_at, history.end_time, task.retry_count);

    if (!history_saved) {
        if (!error.empty()) error += "; ";
        error += "Failed to persist execution history";
    }
    if (!state_completed) {
        if (!error.empty()) error += "; ";
        error += "Task state changed while completing execution";
    }

    resp.set_success(worker_result.success && history_saved && state_completed);
    resp.set_result(result);
    resp.set_error(error);
    return make_response(rpc::kRunTaskNowResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleGetMetrics(const std::string& payload) const {
    corpcron::rpc::GetMetricsRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse GetMetricsRequest failed");
    }
    if (!authorized(req.auth_token())) {
        return make_error_response(corpcron::rpc::UNAUTHORIZED, "Invalid auth token");
    }

    corpcron::rpc::GetMetricsResponse resp;
    fill_metrics_response(resp);
    return make_response(rpc::kGetMetricsResponseSerialId, resp);
}

RpcResponse RpcDispatcher::handleHealthCheck(const std::string& payload) const {
    corpcron::rpc::HealthCheckRequest req;
    if (!req.ParseFromString(payload)) {
        return make_error_response(corpcron::rpc::BAD_REQUEST, "Parse HealthCheckRequest failed");
    }

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    corpcron::rpc::HealthCheckResponse resp;
    const bool known_service =
        req.service_name().empty() || req.service_name() == options_.service_name;
    resp.set_serving(known_service);
    resp.set_status(known_service ? "SERVING" : "UNKNOWN_SERVICE");
    resp.set_node_id(node_id_);
    resp.set_unix_time_ms(static_cast<uint64_t>(now_ms));
    return make_response(rpc::kHealthCheckResponseSerialId, resp);
}

RpcStreamResult RpcDispatcher::handleStreamMetrics(const std::string& payload) const {
    corpcron::rpc::StreamMetricsRequest req;
    if (!req.ParseFromString(payload)) {
        return RpcStreamResult{{make_error_response(corpcron::rpc::BAD_REQUEST,
                                                    "Parse StreamMetricsRequest failed")}};
    }
    if (!authorized(req.auth_token())) {
        return RpcStreamResult{{make_error_response(corpcron::rpc::UNAUTHORIZED,
                                                    "Invalid auth token")}};
    }

    int samples = req.samples() > 0 ? req.samples() : 3;
    if (samples > 10) samples = 10;

    RpcStreamResult result;
    result.responses.reserve(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        corpcron::rpc::StreamMetricsResponse resp;
        resp.set_success(true);
        resp.set_sequence(static_cast<uint32_t>(i));
        resp.set_end_of_stream(i + 1 == samples);
        fill_metrics_response(*resp.mutable_metrics());
        result.responses.push_back(make_response(rpc::kStreamMetricsResponseSerialId, resp));
    }
    return result;
}

bool RpcDispatcher::authorized(const std::string& request_token) const {
    if (auth_token_.empty()) return true;
    if (auth_token_.size() != request_token.size()) return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < auth_token_.size(); ++i) {
        difference |= static_cast<unsigned char>(auth_token_[i]) ^
                      static_cast<unsigned char>(request_token[i]);
    }
    return difference == 0;
}

bool RpcDispatcher::methodAllowed(uint32_t serial_id) const {
    if (options_.role == RpcNodeRole::Combined) return true;
    if (options_.role == RpcNodeRole::ControlPlane) {
        return serial_id != rpc::kExecuteTaskRequestSerialId;
    }
    switch (serial_id) {
        case rpc::kEchoRequestSerialId:
        case rpc::kExecuteTaskRequestSerialId:
        case rpc::kGetMetricsRequestSerialId:
        case rpc::kHealthCheckRequestSerialId:
        case rpc::kStreamMetricsRequestSerialId:
            return true;
        default:
            return false;
    }
}

bool RpcDispatcher::handlerAvailable(const std::string& handler) const {
    if (handler.empty()) return false;
    if (options_.role != RpcNodeRole::ControlPlane &&
        HandlerRegistry::instance().hasHandler(handler)) {
        return true;
    }
    if (!redis_) return HandlerRegistry::instance().hasHandler(handler);
    const std::string capability = options_.worker_service_name + ":" + handler;
    return !redis_->discoverServices(capability).empty();
}

RpcDispatcher::WorkerResult RpcDispatcher::invokeWorker(
    const std::string& task_id, const std::string& execution_id,
    const std::string& handler, const std::string& params) const {
    WorkerResult outcome;
    if (options_.role == RpcNodeRole::Combined &&
        HandlerRegistry::instance().hasHandler(handler)) {
        try {
            TaskExecutionContext context;
            context.task_id = task_id;
            context.execution_id = execution_id;
            context.deadline_unix_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() +
                options_.worker_timeout_ms);
            outcome.result = HandlerRegistry::instance().execute(handler, context, params);
            outcome.success = true;
            outcome.endpoint = node_id_.empty() ? "local" : node_id_;
        } catch (const std::exception& e) {
            outcome.error = e.what();
        } catch (...) {
            outcome.error = "Unknown handler exception";
        }
        return outcome;
    }

    if (!redis_ || !worker_pool_) {
        outcome.error = "Worker discovery is not available";
        return outcome;
    }
    std::vector<std::string> endpoints =
        redis_->discoverServices(options_.worker_service_name + ":" + handler);
    if (endpoints.empty()) {
        outcome.error = "No worker available for handler: " + handler;
        return outcome;
    }

    corpcron::rpc::ExecuteTaskRequest request;
    request.set_task_id(task_id);
    request.set_execution_id(execution_id);
    request.set_handler(handler);
    request.set_params(params);
    request.set_auth_token(auth_token_);
    const auto deadline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() +
        options_.worker_timeout_ms;
    request.set_deadline_unix_ms(static_cast<uint64_t>(deadline_ms));

    std::string request_payload;
    if (!request.SerializeToString(&request_payload)) {
        outcome.error = "Failed to serialize ExecuteTaskRequest";
        return outcome;
    }

    uint32_t response_serial_id = 0;
    std::string response_payload;
    std::string pool_error;
    RpcCallOptions call_options = RpcCallOptions::fromTimeout(options_.worker_timeout_ms);
    if (!worker_pool_->call(endpoints, rpc::kExecuteTaskRequestSerialId, request_payload,
                            response_serial_id, response_payload, call_options,
                            &outcome.endpoint, &pool_error)) {
        outcome.error = pool_error.empty() ? "Worker RPC call failed" : pool_error;
        return outcome;
    }
    if (response_serial_id == rpc::kRpcErrorSerialId) {
        corpcron::rpc::RpcError error;
        if (error.ParseFromString(response_payload)) {
            outcome.error = "RPC error " + std::to_string(error.code()) + ": " +
                            error.message();
        } else {
            outcome.error = "Worker RPC error response parse failed";
        }
        return outcome;
    }
    if (response_serial_id != rpc::kExecuteTaskResponseSerialId) {
        outcome.error = "Unexpected worker response serial_id: " +
                        std::to_string(response_serial_id);
        return outcome;
    }

    corpcron::rpc::ExecuteTaskResponse response;
    if (!response.ParseFromString(response_payload)) {
        outcome.error = "Failed to parse ExecuteTaskResponse";
        return outcome;
    }
    outcome.success = response.success();
    outcome.result = response.result();
    outcome.error = response.error();
    if (!outcome.success && outcome.error.empty()) {
        outcome.error = "Worker reported task failure";
    }
    return outcome;
}

} // namespace corpcron
