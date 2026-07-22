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
    auto dispatcher = std::make_shared<corpcron::RpcDispatcher>(nullptr, "");
    corpcron::TcpServer server("127.0.0.1", port, dispatcher, 8);

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
