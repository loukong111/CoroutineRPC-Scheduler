#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client.hpp"
#include "rpc.pb.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string getenv_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

int percentile(std::vector<int>& values, double pct) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>((values.size() - 1) * pct);
    return values[index];
}

} // namespace

int main(int argc, char* argv[]) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? std::stoi(argv[2]) : 8081;
    int concurrency = argc > 3 ? std::stoi(argv[3]) : 16;
    int requests = argc > 4 ? std::stoi(argv[4]) : 1000;
    std::string auth_token = getenv_or("CORPCRON_RPC_AUTH_TOKEN", "");

    std::atomic<int> next_request{0};
    std::atomic<int> success{0};
    std::atomic<int> failure{0};
    std::mutex latencies_mutex;
    std::vector<int> latencies_ms;
    latencies_ms.reserve(static_cast<size_t>(requests));

    auto started = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (int i = 0; i < concurrency; ++i) {
        workers.emplace_back([&, i]() {
            while (true) {
                int id = next_request.fetch_add(1);
                if (id >= requests) break;

                corpcron::rpc::EchoRequest req;
                req.set_message("bench-" + std::to_string(i) + "-" + std::to_string(id));
                req.set_auth_token(auth_token);
                std::string payload;
                req.SerializeToString(&payload);

                corpcron::RpcClient client(host, port);
                uint32_t response_serial_id = 0;
                std::string response_payload;
                auto begin = std::chrono::steady_clock::now();
                bool ok = client.call(1, payload, response_serial_id, response_payload, 3000);
                auto end = std::chrono::steady_clock::now();
                int latency = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

                if (ok && response_serial_id == 2) {
                    ++success;
                    std::lock_guard<std::mutex> lock(latencies_mutex);
                    latencies_ms.push_back(latency);
                } else {
                    ++failure;
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();

    auto ended = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(ended - started).count();
    double qps = elapsed > 0 ? success.load() / elapsed : 0;

    std::cout << "requests=" << requests
              << " concurrency=" << concurrency
              << " success=" << success.load()
              << " failure=" << failure.load()
              << " elapsed_sec=" << elapsed
              << " qps=" << qps
              << " p50_ms=" << percentile(latencies_ms, 0.50)
              << " p95_ms=" << percentile(latencies_ms, 0.95)
              << " p99_ms=" << percentile(latencies_ms, 0.99)
              << std::endl;

    return failure.load() == 0 ? 0 : 1;
}
