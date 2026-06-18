#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/scheduler/cron_parser.hpp"
#include "rpc.pb.h"
#include <chrono>
#include <ctime>
#include <google/protobuf/message.h>
#include <random>
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

bool RpcDispatcher::authorized(const std::string& request_token) const {
    return auth_token_.empty() || auth_token_ == request_token;
}

} // namespace corpcron
