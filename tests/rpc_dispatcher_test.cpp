#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/rpc/rpc_interceptor.hpp"
#include "rpc.pb.h"
#include <cassert>
#include <memory>
#include <string>

namespace {

void assert_error(const corpcron::RpcResponse& response, corpcron::rpc::ErrorCode expected_code) {
    assert(response.serial_id == corpcron::rpc::kRpcErrorSerialId);
    corpcron::rpc::RpcError error;
    assert(error.ParseFromString(response.payload));
    assert(error.code() == expected_code);
}

std::string serialize_echo(const std::string& message, const std::string& token) {
    corpcron::rpc::EchoRequest request;
    request.set_message(message);
    request.set_auth_token(token);
    std::string payload;
    request.SerializeToString(&payload);
    return payload;
}

} // namespace

int main() {
    corpcron::HandlerRegistry::instance().registerHandler("Echo", [](const std::string& value) {
        return "Echo: " + value;
    });

    corpcron::RpcDispatcher dispatcher(nullptr, "secret");

    auto unauthorized = dispatcher.dispatch(corpcron::rpc::kEchoRequestSerialId,
                                            serialize_echo("hello", "wrong-token"));
    assert_error(unauthorized, corpcron::rpc::UNAUTHORIZED);

    auto unknown_method = dispatcher.dispatch(9999, "");
    assert_error(unknown_method, corpcron::rpc::UNKNOWN_METHOD);

    corpcron::rpc::ExecuteTaskRequest missing_handler_request;
    missing_handler_request.set_handler("MissingHandler");
    missing_handler_request.set_auth_token("secret");
    std::string missing_handler_payload;
    missing_handler_request.SerializeToString(&missing_handler_payload);
    auto missing_handler = dispatcher.dispatch(corpcron::rpc::kExecuteTaskRequestSerialId,
                                               missing_handler_payload);
    assert_error(missing_handler, corpcron::rpc::HANDLER_NOT_FOUND);

    std::string invalid_protobuf(1, static_cast<char>(0x80));
    auto bad_payload = dispatcher.dispatch(corpcron::rpc::kEchoRequestSerialId, invalid_protobuf);
    assert_error(bad_payload, corpcron::rpc::BAD_REQUEST);

    auto ok = dispatcher.dispatch(corpcron::rpc::kEchoRequestSerialId,
                                  serialize_echo("hello", "secret"));
    assert(ok.serial_id == corpcron::rpc::kEchoResponseSerialId);
    corpcron::rpc::EchoResponse echo_response;
    assert(echo_response.ParseFromString(ok.payload));
    assert(echo_response.message() == "Echo: hello");

    corpcron::rpc::SubmitTaskRequest submit;
    submit.set_cron_expr("0 0 0 1 1 ?");
    submit.set_handler("Echo");
    submit.set_params("needs db");
    submit.set_auth_token("secret");
    std::string submit_payload;
    submit.SerializeToString(&submit_payload);
    auto db_missing = dispatcher.dispatch(corpcron::rpc::kSubmitTaskRequestSerialId, submit_payload);
    assert_error(db_missing, corpcron::rpc::DB_ERROR);

    corpcron::rpc::GetMetricsRequest metrics_request;
    metrics_request.set_auth_token("secret");
    std::string metrics_payload;
    metrics_request.SerializeToString(&metrics_payload);
    auto metrics_response = dispatcher.dispatch(corpcron::rpc::kGetMetricsRequestSerialId, metrics_payload);
    assert(metrics_response.serial_id == corpcron::rpc::kGetMetricsResponseSerialId);
    corpcron::rpc::GetMetricsResponse metrics;
    assert(metrics.ParseFromString(metrics_response.payload));
    assert(metrics.success());

    auto health_response = dispatcher.dispatch(corpcron::rpc::kHealthCheckRequestSerialId,
                                               std::string(1, static_cast<char>(0x80)));
    assert_error(health_response, corpcron::rpc::BAD_REQUEST);

    corpcron::rpc::HealthCheckRequest health_request;
    health_request.set_service_name("rpc");
    std::string health_payload;
    health_request.SerializeToString(&health_payload);
    health_response = dispatcher.dispatch(corpcron::rpc::kHealthCheckRequestSerialId, health_payload);
    assert(health_response.serial_id == corpcron::rpc::kHealthCheckResponseSerialId);
    corpcron::rpc::HealthCheckResponse health;
    assert(health.ParseFromString(health_response.payload));
    assert(health.serving());
    assert(health.status() == "SERVING");

    health_request.set_service_name("missing-service");
    health_request.SerializeToString(&health_payload);
    health_response = dispatcher.dispatch(corpcron::rpc::kHealthCheckRequestSerialId, health_payload);
    assert(health_response.serial_id == corpcron::rpc::kHealthCheckResponseSerialId);
    assert(health.ParseFromString(health_response.payload));
    assert(!health.serving());
    assert(health.status() == "UNKNOWN_SERVICE");

    corpcron::rpc::StreamMetricsRequest stream_metrics_request;
    stream_metrics_request.set_auth_token("secret");
    stream_metrics_request.set_samples(3);
    std::string stream_metrics_payload;
    stream_metrics_request.SerializeToString(&stream_metrics_payload);
    corpcron::RpcContext stream_context;
    stream_context.request_serial_id = corpcron::rpc::kStreamMetricsRequestSerialId;
    auto stream_result = dispatcher.dispatchStreamWithContext(stream_context, stream_metrics_payload);
    assert(stream_result.responses.size() == 3);
    for (size_t i = 0; i < stream_result.responses.size(); ++i) {
        assert(stream_result.responses[i].serial_id == corpcron::rpc::kStreamMetricsResponseSerialId);
        corpcron::rpc::StreamMetricsResponse item;
        assert(item.ParseFromString(stream_result.responses[i].payload));
        assert(item.success());
        assert(item.sequence() == i);
        assert(item.end_of_stream() == (i + 1 == stream_result.responses.size()));
        assert(item.has_metrics());
    }

    stream_metrics_request.set_auth_token("wrong-token");
    stream_metrics_request.SerializeToString(&stream_metrics_payload);
    stream_result = dispatcher.dispatchStreamWithContext(stream_context, stream_metrics_payload);
    assert(stream_result.responses.size() == 1);
    assert_error(stream_result.responses.front(), corpcron::rpc::UNAUTHORIZED);

    return 0;
}
