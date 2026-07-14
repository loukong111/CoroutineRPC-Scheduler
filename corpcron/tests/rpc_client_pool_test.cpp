#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client_pool.hpp"
#include "rpc.pb.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

bool recv_exact(int fd, char* buffer, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(fd, buffer + received, size - received, 0);
        if (n > 0) {
            received += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

void read_frame(int fd) {
    char header[corpcron::rpc::kHeaderSize];
    assert(recv_exact(fd, header, sizeof(header)));
    uint32_t total_len = ((unsigned char)header[0] << 24) |
                         ((unsigned char)header[1] << 16) |
                         ((unsigned char)header[2] << 8) |
                         (unsigned char)header[3];
    assert(total_len >= corpcron::rpc::kSerialIdSize);
    std::string rest(total_len - corpcron::rpc::kSerialIdSize, '\0');
    assert(recv_exact(fd, rest.data(), rest.size()));
}

int create_listen_socket(uint16_t& port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) return -1;
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        close(listen_fd);
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(listen_fd);
        return -1;
    }
    if (listen(listen_fd, 8) != 0) {
        close(listen_fd);
        return -1;
    }

    socklen_t len = sizeof(addr);
    assert(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    port = ntohs(addr.sin_port);
    return listen_fd;
}

void serve_same_connection(int listen_fd, const std::vector<std::string>& messages, int* accept_count) {
    int fd = accept(listen_fd, nullptr, nullptr);
    assert(fd >= 0);
    ++(*accept_count);
    for (const auto& message : messages) {
        read_frame(fd);
        corpcron::rpc::EchoResponse response;
        response.set_message(message);
        std::string payload;
        response.SerializeToString(&payload);
        assert(send_all(fd, corpcron::rpc::encode(corpcron::rpc::kEchoResponseSerialId, payload)));
    }
    close(fd);
    close(listen_fd);
}

std::string make_echo_payload() {
    corpcron::rpc::EchoRequest request;
    request.set_message("ping");
    std::string payload;
    request.SerializeToString(&payload);
    return payload;
}

std::string parse_echo(const std::string& payload) {
    corpcron::rpc::EchoResponse response;
    assert(response.ParseFromString(payload));
    return response.message();
}

} // namespace

int main() {
    uint16_t port1 = 0;
    uint16_t port2 = 0;
    int listen_fd1 = create_listen_socket(port1);
    int listen_fd2 = create_listen_socket(port2);
    if (listen_fd1 < 0 || listen_fd2 < 0) {
        std::cout << "Skipping RpcClientPool test: loopback socket is not available.\n";
        if (listen_fd1 >= 0) close(listen_fd1);
        if (listen_fd2 >= 0) close(listen_fd2);
        return 77;
    }

    int accept_count1 = 0;
    int accept_count2 = 0;
    std::thread server1(serve_same_connection, listen_fd1,
                        std::vector<std::string>{"node1-first", "node1-second"},
                        &accept_count1);
    std::thread server2(serve_same_connection, listen_fd2,
                        std::vector<std::string>{"node2-first"},
                        &accept_count2);

    corpcron::RpcClientPool::Options options;
    options.max_idle_per_endpoint = 1;
    options.failure_threshold = 1;
    options.cooldown_ms = 100;
    corpcron::RpcClientPool pool(options);

    const std::vector<std::string> endpoints = {
        "127.0.0.1:" + std::to_string(port1),
        "127.0.0.1:" + std::to_string(port2),
    };

    uint32_t response_serial_id = 0;
    std::string response_payload;
    std::string selected_endpoint;
    std::string error;
    const std::string request_payload = make_echo_payload();

    assert(pool.call(endpoints, corpcron::rpc::kEchoRequestSerialId, request_payload,
                     response_serial_id, response_payload, 1000, &selected_endpoint, &error));
    assert(selected_endpoint == endpoints[0]);
    assert(parse_echo(response_payload) == "node1-first");

    assert(pool.call(endpoints, corpcron::rpc::kEchoRequestSerialId, request_payload,
                     response_serial_id, response_payload, 1000, &selected_endpoint, &error));
    assert(selected_endpoint == endpoints[1]);
    assert(parse_echo(response_payload) == "node2-first");

    assert(pool.call(endpoints, corpcron::rpc::kEchoRequestSerialId, request_payload,
                     response_serial_id, response_payload, 1000, &selected_endpoint, &error));
    assert(selected_endpoint == endpoints[0]);
    assert(parse_echo(response_payload) == "node1-second");

    server1.join();
    server2.join();
    assert(accept_count1 == 1);
    assert(accept_count2 == 1);
    return 0;
}
