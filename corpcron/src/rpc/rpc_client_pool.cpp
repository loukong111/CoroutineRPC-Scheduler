#include "corpcron/rpc/rpc_client_pool.hpp"
#include "corpcron/common/logger.hpp"
#include <algorithm>
#include <cstdlib>
#include <utility>

namespace corpcron {

RpcClientPool::RpcClientPool()
    : RpcClientPool(Options{}) {}

RpcClientPool::RpcClientPool(Options options)
    : options_(options) {
    if (options_.max_idle_per_endpoint == 0) {
        options_.max_idle_per_endpoint = 1;
    }
    if (options_.failure_threshold <= 0) {
        options_.failure_threshold = 1;
    }
    if (options_.cooldown_ms < 0) {
        options_.cooldown_ms = 0;
    }
}

RpcClientPool::~RpcClientPool() {
    clear();
}

bool RpcClientPool::parseEndpoint(const std::string& endpoint, EndpointAddress& address) {
    const size_t colon = endpoint.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= endpoint.size()) {
        return false;
    }

    address.host = endpoint.substr(0, colon);
    const std::string port_text = endpoint.substr(colon + 1);
    char* end = nullptr;
    long port = std::strtol(port_text.c_str(), &end, 10);
    if (end == port_text.c_str() || *end != '\0' || port <= 0 || port > 65535) {
        return false;
    }
    address.port = static_cast<int>(port);
    return true;
}

std::shared_ptr<RpcClientPool::EndpointState> RpcClientPool::stateFor(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(endpoint);
    if (it != states_.end()) {
        return it->second;
    }
    auto state = std::make_shared<EndpointState>();
    states_[endpoint] = state;
    return state;
}

bool RpcClientPool::isHealthyEnough(const std::string& endpoint,
                                    const std::chrono::steady_clock::time_point& now) {
    auto state = stateFor(endpoint);
    std::lock_guard<std::mutex> lock(state->mutex);
    return now >= state->unhealthy_until;
}

RpcClientPool::BorrowedClient RpcClientPool::borrow(const std::string& endpoint,
                                                    const EndpointAddress& address) {
    auto state = stateFor(endpoint);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->idle_clients.empty()) {
            auto client = std::move(state->idle_clients.back());
            state->idle_clients.pop_back();
            return BorrowedClient{state, std::move(client), true};
        }
    }
    return BorrowedClient{state, std::make_unique<RpcClient>(address.host, address.port), true};
}

void RpcClientPool::release(BorrowedClient borrowed, bool reusable) {
    if (!borrowed.client || !borrowed.state || !reusable) {
        return;
    }

    std::lock_guard<std::mutex> lock(borrowed.state->mutex);
    if (borrowed.state->idle_clients.size() < options_.max_idle_per_endpoint) {
        borrowed.state->idle_clients.push_back(std::move(borrowed.client));
    }
}

void RpcClientPool::recordSuccess(const std::string& endpoint) {
    auto state = stateFor(endpoint);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->consecutive_failures = 0;
    state->unhealthy_until = {};
}

void RpcClientPool::recordFailure(const std::string& endpoint) {
    auto state = stateFor(endpoint);
    std::lock_guard<std::mutex> lock(state->mutex);
    ++state->consecutive_failures;
    if (state->consecutive_failures >= options_.failure_threshold) {
        state->unhealthy_until = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds(options_.cooldown_ms);
        LOG_WARN_EVENT("rpc_endpoint_cooldown", {
            {"endpoint", endpoint},
            {"failures", std::to_string(state->consecutive_failures)},
            {"cooldown_ms", std::to_string(options_.cooldown_ms)}
        });
    }
}

std::vector<std::string> RpcClientPool::orderedCandidates(const std::vector<std::string>& endpoints) {
    std::vector<std::string> unique;
    unique.reserve(endpoints.size());
    for (const auto& endpoint : endpoints) {
        if (endpoint.empty()) continue;
        if (std::find(unique.begin(), unique.end(), endpoint) == unique.end()) {
            unique.push_back(endpoint);
        }
    }

    if (unique.empty()) return unique;

    const auto now = std::chrono::steady_clock::now();
    const size_t start = static_cast<size_t>(round_robin_.fetch_add(1, std::memory_order_relaxed)) % unique.size();
    std::vector<std::string> ordered;
    ordered.reserve(unique.size());

    for (size_t i = 0; i < unique.size(); ++i) {
        const auto& endpoint = unique[(start + i) % unique.size()];
        if (isHealthyEnough(endpoint, now)) {
            ordered.push_back(endpoint);
        }
    }

    if (ordered.empty()) {
        for (size_t i = 0; i < unique.size(); ++i) {
            ordered.push_back(unique[(start + i) % unique.size()]);
        }
    }
    return ordered;
}

bool RpcClientPool::call(const std::vector<std::string>& endpoints,
                         uint32_t serial_id,
                         const std::string& request_data,
                         uint32_t& response_serial_id,
                         std::string& response_data,
                         int timeout_ms,
                         std::string* selected_endpoint,
                         std::string* error) {
    auto candidates = orderedCandidates(endpoints);
    if (candidates.empty()) {
        if (error) *error = "no endpoint available";
        return false;
    }

    std::string last_error;
    for (const auto& endpoint : candidates) {
        EndpointAddress address;
        if (!parseEndpoint(endpoint, address)) {
            last_error = "invalid endpoint: " + endpoint;
            recordFailure(endpoint);
            continue;
        }

        auto borrowed = borrow(endpoint, address);
        const bool ok = borrowed.client->call(serial_id, request_data, response_serial_id,
                                             response_data, timeout_ms);
        release(std::move(borrowed), ok);

        if (ok) {
            recordSuccess(endpoint);
            if (selected_endpoint) *selected_endpoint = endpoint;
            if (error) error->clear();
            return true;
        }

        recordFailure(endpoint);
        last_error = "rpc call failed: " + endpoint;
        LOG_WARN_EVENT("rpc_endpoint_call_failed", {{"endpoint", endpoint}});
    }

    if (error) *error = last_error.empty() ? "all endpoints failed" : last_error;
    return false;
}

void RpcClientPool::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.clear();
}

} // namespace corpcron
