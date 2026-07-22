#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/rpc/rpc_interceptor.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "rpc.pb.h"
#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace {

class RecordingInterceptor : public corpcron::RpcInterceptor {
public:
    RecordingInterceptor(std::string name, std::vector<std::string>* events)
        : name_(std::move(name)), events_(events) {}

    corpcron::RpcResponse intercept(corpcron::RpcContext& context,
                                    corpcron::RpcNext next) override {
        events_->push_back(name_ + ":before:" + context.method_name);
        auto response = next(context);
        events_->push_back(name_ + ":after:" + std::to_string(response.serial_id));
        return response;
    }

private:
    std::string name_;
    std::vector<std::string>* events_;
};

class ShortCircuitInterceptor : public corpcron::RpcInterceptor {
public:
    corpcron::RpcResponse intercept(corpcron::RpcContext&, corpcron::RpcNext) override {
        return corpcron::RpcDispatcher::error(corpcron::rpc::UNAUTHORIZED, "blocked by interceptor");
    }
};

} // namespace

int main() {
    std::vector<std::string> events;
    corpcron::RpcInterceptorChain chain;
    chain.add(std::make_shared<RecordingInterceptor>("first", &events));
    chain.add(std::make_shared<RecordingInterceptor>("second", &events));

    corpcron::RpcContext context;
    context.method_name = "Echo";
    auto response = chain.invoke(context, [](corpcron::RpcContext&) {
        corpcron::rpc::EchoResponse echo;
        echo.set_message("ok");
        corpcron::RpcResponse result{corpcron::rpc::kEchoResponseSerialId, {}};
        echo.SerializeToString(&result.payload);
        return result;
    });

    assert(response.serial_id == corpcron::rpc::kEchoResponseSerialId);
    assert((events == std::vector<std::string>{
        "first:before:Echo",
        "second:before:Echo",
        "second:after:2",
        "first:after:2",
    }));

    bool terminal_called = false;
    corpcron::RpcInterceptorChain short_circuit_chain;
    short_circuit_chain.add(std::make_shared<ShortCircuitInterceptor>());
    corpcron::RpcContext blocked_context;
    auto blocked = short_circuit_chain.invoke(blocked_context, [&](corpcron::RpcContext&) {
        terminal_called = true;
        return corpcron::RpcResponse{corpcron::rpc::kEchoResponseSerialId, {}};
    });

    assert(!terminal_called);
    assert(blocked.serial_id == corpcron::rpc::kRpcErrorSerialId);
    corpcron::rpc::RpcError error;
    assert(error.ParseFromString(blocked.payload));
    assert(error.code() == corpcron::rpc::UNAUTHORIZED);
    assert(error.message() == "blocked by interceptor");

    return 0;
}
