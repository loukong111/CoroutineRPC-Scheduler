#include "corpcron/rpc/rpc_interceptor.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/metrics/metrics.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "rpc_service.hpp"
#include <exception>
#include <stdexcept>
#include <utility>

namespace corpcron {

namespace {

void fillMethodName(RpcContext& context) {
    if (!context.method_name.empty()) return;
    const auto* method = rpc::findMethodByRequestSerialId(context.request_serial_id);
    context.method_name = method ? method->name : "unknown";
}

void fillResponseFields(RpcContext& context, const RpcResponse& response) {
    context.response_serial_id = response.serial_id;
    context.response_bytes = response.payload.size();
    context.success = response.serial_id != rpc::kRpcErrorSerialId;
    if (context.started_at.time_since_epoch().count() != 0) {
        context.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - context.started_at);
    }
}

} // namespace

void RpcInterceptorChain::add(std::shared_ptr<RpcInterceptor> interceptor) {
    if (interceptor) interceptors_.push_back(std::move(interceptor));
}

bool RpcInterceptorChain::empty() const {
    return interceptors_.empty();
}

RpcResponse RpcInterceptorChain::invoke(RpcContext& context, RpcNext terminal) const {
    std::function<RpcResponse(size_t, RpcContext&)> call_next =
        [&](size_t index, RpcContext& current) -> RpcResponse {
        if (index >= interceptors_.size()) {
            return terminal(current);
        }
        return interceptors_[index]->intercept(current, [&](RpcContext& next_context) {
            return call_next(index + 1, next_context);
        });
    };
    return call_next(0, context);
}

RpcResponse RpcExceptionInterceptor::intercept(RpcContext& context, RpcNext next) {
    try {
        return next(context);
    } catch (const std::exception& e) {
        return RpcDispatcher::error(rpc::INTERNAL_ERROR, e.what());
    } catch (...) {
        return RpcDispatcher::error(rpc::INTERNAL_ERROR, "Unknown RPC exception");
    }
}

RpcResponse RpcMetricsInterceptor::intercept(RpcContext& context, RpcNext next) {
    Metrics::instance().incRpcRequest();
    RpcResponse response = next(context);
    fillResponseFields(context, response);
    if (context.success) {
        Metrics::instance().incRpcSuccess();
    } else {
        Metrics::instance().incRpcError();
    }
    return response;
}

RpcResponse RpcLoggingInterceptor::intercept(RpcContext& context, RpcNext next) {
    fillMethodName(context);
    LOG_INFO_EVENT("rpc_request", {
        {"request_id", context.request_id},
        {"remote", context.remote},
        {"method", context.method_name},
        {"serial_id", std::to_string(context.request_serial_id)},
        {"payload_bytes", std::to_string(context.request_bytes)}
    });

    RpcResponse response = next(context);
    fillResponseFields(context, response);

    LOG_INFO_EVENT("rpc_response", {
        {"request_id", context.request_id},
        {"remote", context.remote},
        {"method", context.method_name},
        {"response_serial_id", std::to_string(context.response_serial_id)},
        {"payload_bytes", std::to_string(context.response_bytes)},
        {"elapsed_ms", std::to_string(context.elapsed.count())},
        {"success", context.success ? "true" : "false"}
    });
    return response;
}

std::shared_ptr<RpcInterceptorChain> makeDefaultRpcInterceptorChain() {
    auto chain = std::make_shared<RpcInterceptorChain>();
    chain->add(std::make_shared<RpcLoggingInterceptor>());
    chain->add(std::make_shared<RpcMetricsInterceptor>());
    chain->add(std::make_shared<RpcExceptionInterceptor>());
    return chain;
}

} // namespace corpcron
