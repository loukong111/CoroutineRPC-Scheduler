#pragma once

#include "corpcron/rpc/rpc_client.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace corpcron {

class RpcClientPool {
public:
    struct Options {
        size_t max_idle_per_endpoint = 4;
        int failure_threshold = 2;
        int cooldown_ms = 3000;
        bool health_check_probe = true;
        int health_check_timeout_ms = 500;
        std::string health_check_service_name = "rpc";
    };

    RpcClientPool();
    explicit RpcClientPool(Options options);
    ~RpcClientPool();

    bool call(const std::vector<std::string>& endpoints,
              uint32_t serial_id,
              const std::string& request_data,
              uint32_t& response_serial_id,
              std::string& response_data,
              int timeout_ms,
              std::string* selected_endpoint = nullptr,
              std::string* error = nullptr);
    bool call(const std::vector<std::string>& endpoints,
              uint32_t serial_id,
              const std::string& request_data,
              uint32_t& response_serial_id,
              std::string& response_data,
              RpcCallOptions options,
              std::string* selected_endpoint = nullptr,
              std::string* error = nullptr);

    void clear();

private:
    struct EndpointAddress {
        std::string host;
        int port = 0;
    };

    struct EndpointState {
        enum class CircuitState {
            Closed,
            Open,
            HalfOpen,
        };

        std::mutex mutex;
        std::vector<std::unique_ptr<RpcClient>> idle_clients;
        int consecutive_failures = 0;
        std::chrono::steady_clock::time_point unhealthy_until{};
        CircuitState circuit_state = CircuitState::Closed;
        bool half_open_probe_in_flight = false;
    };

    struct BorrowedClient {
        std::shared_ptr<EndpointState> state;
        std::unique_ptr<RpcClient> client;
    };

    static bool parseEndpoint(const std::string& endpoint, EndpointAddress& address);

    std::shared_ptr<EndpointState> stateFor(const std::string& endpoint);
    bool isHealthyEnough(const std::string& endpoint, const std::chrono::steady_clock::time_point& now);
    bool reserveHalfOpenProbe(const std::string& endpoint,
                              const std::chrono::steady_clock::time_point& now);
    void finishHalfOpenProbe(const std::string& endpoint, bool success);
    bool healthCheckEndpoint(const std::string& endpoint, const EndpointAddress& address,
                             const RpcCallOptions& parent_options);
    BorrowedClient borrow(const std::string& endpoint, const EndpointAddress& address);
    void release(BorrowedClient borrowed, bool reusable);
    void recordSuccess(const std::string& endpoint);
    void recordFailure(const std::string& endpoint);
    std::vector<std::string> orderedCandidates(const std::vector<std::string>& endpoints);

    Options options_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<EndpointState>> states_;
    std::atomic<uint64_t> round_robin_{0};
};

} // namespace corpcron
