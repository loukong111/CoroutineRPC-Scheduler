#include "corpcron/net/tcp_server.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "rpc.pb.h"
#include <arpa/inet.h>
#include <cassert>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

int reservePort(uint16_t& port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    socklen_t length = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        close(fd);
        return -1;
    }
    port = ntohs(address.sin_port);
    return fd;
}

} // namespace

int main() {
    uint16_t port = 0;
    int reservation = reservePort(port);
    if (reservation < 0) {
        std::cout << "Skipping TcpServer test: loopback socket is not available.\n";
        return 77;
    }
    close(reservation);

    corpcron::HandlerRegistry::instance().registerHandler("Echo", [](const std::string& value) {
        return "Echo: " + value;
    });
    std::atomic<bool> slow_started{false};
    std::atomic<bool> release_slow{false};
    corpcron::HandlerRegistry::instance().registerHandler(
        "Slow", [&](const std::string& value) {
            slow_started.store(true, std::memory_order_release);
            while (!release_slow.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return "Slow: " + value;
        });
    auto dispatcher = std::make_shared<corpcron::RpcDispatcher>(nullptr, "");
    corpcron::RpcExecutorOptions executor_options;
    executor_options.min_threads = 2;
    executor_options.max_threads = 2;
    executor_options.max_pending_requests = 8;
    corpcron::TcpServer server("127.0.0.1", port, dispatcher, 8, executor_options);

    bool server_result = false;
    std::atomic<bool> listening_hook_called{false};
    std::thread server_thread([&]() {
        server_result = server.start([&]() {
            listening_hook_called.store(true, std::memory_order_release);
            return true;
        });
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    corpcron::rpc::EchoRequest request;
    request.set_message("lifecycle");
    std::string request_payload;
    assert(request.SerializeToString(&request_payload));

    corpcron::RpcClient client("127.0.0.1", port);
    uint32_t response_serial_id = 0;
    std::string response_payload;
    assert(client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                       response_serial_id, response_payload, 1000));
    assert(response_serial_id == corpcron::rpc::kEchoResponseSerialId);

    corpcron::rpc::EchoResponse response;
    assert(response.ParseFromString(response_payload));
    assert(response.message() == "Echo: lifecycle");
    assert(listening_hook_called.load(std::memory_order_acquire));

    corpcron::rpc::ExecuteTaskRequest slow_request;
    slow_request.set_task_id("slow-task");
    slow_request.set_execution_id("slow-execution");
    slow_request.set_handler("Slow");
    slow_request.set_params("value");
    std::string slow_payload;
    assert(slow_request.SerializeToString(&slow_payload));
    std::atomic<bool> slow_ok{false};
    std::thread slow_client([&]() {
        corpcron::RpcClient client("127.0.0.1", port);
        uint32_t serial_id = 0;
        std::string payload;
        if (!client.call(corpcron::rpc::kExecuteTaskRequestSerialId, slow_payload,
                         serial_id, payload, 2000)) {
            return;
        }
        corpcron::rpc::ExecuteTaskResponse result;
        slow_ok.store(
            serial_id == corpcron::rpc::kExecuteTaskResponseSerialId &&
                result.ParseFromString(payload) && result.success() &&
                result.result() == "Slow: value",
            std::memory_order_release);
    });
    const auto slow_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!slow_started.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < slow_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(slow_started.load(std::memory_order_acquire));

    corpcron::RpcClient concurrent_client("127.0.0.1", port);
    response_serial_id = 0;
    response_payload.clear();
    const auto echo_start = std::chrono::steady_clock::now();
    assert(concurrent_client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                                  response_serial_id, response_payload, 500));
    const auto echo_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - echo_start);
    assert(echo_elapsed < std::chrono::milliseconds(400));
    assert(response_serial_id == corpcron::rpc::kEchoResponseSerialId);

    release_slow.store(true, std::memory_order_release);
    slow_client.join();
    assert(slow_ok.load(std::memory_order_acquire));

    server.stop();
    server_thread.join();
    assert(server_result);

    uint16_t rejected_port = 0;
    int rejected_reservation = reservePort(rejected_port);
    assert(rejected_reservation >= 0);
    close(rejected_reservation);
    corpcron::TcpServer rejected_server("127.0.0.1", rejected_port, dispatcher, 8);
    bool rejected_hook_called = false;
    assert(!rejected_server.start([&]() {
        rejected_hook_called = true;
        return false;
    }));
    assert(rejected_hook_called);
    return 0;
}
