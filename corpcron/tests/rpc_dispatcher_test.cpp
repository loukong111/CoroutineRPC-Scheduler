#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
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

    return 0;
}
