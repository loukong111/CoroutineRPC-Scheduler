#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client.hpp"
#include "rpc.pb.h"
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

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

std::string read_frame_payload(int fd, uint32_t& serial_id) {
    char header[corpcron::rpc::kHeaderSize];
    assert(recv_exact(fd, header, sizeof(header)));
    uint32_t total_len = ((unsigned char)header[0] << 24) |
                         ((unsigned char)header[1] << 16) |
                         ((unsigned char)header[2] << 8) |
                         (unsigned char)header[3];
    assert(total_len >= corpcron::rpc::kSerialIdSize);
    std::string frame(sizeof(header) + total_len - corpcron::rpc::kSerialIdSize, '\0');
    std::memcpy(frame.data(), header, sizeof(header));
    assert(recv_exact(fd, frame.data() + sizeof(header), total_len - corpcron::rpc::kSerialIdSize));
    std::string payload;
    size_t frame_size = 0;
    assert(corpcron::rpc::tryDecodeFrame(frame.data(), frame.size(), serial_id, payload, frame_size) ==
           corpcron::rpc::DecodeStatus::Complete);
    return payload;
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

void serve_two_requests(int listen_fd) {
    for (int i = 0; i < 2; ++i) {
        int fd = accept(listen_fd, nullptr, nullptr);
        assert(fd >= 0);
        uint32_t request_serial_id = 0;
        (void)read_frame_payload(fd, request_serial_id);

        std::string response_payload;
        uint32_t response_serial_id = 0;
        if (i == 0) {
            corpcron::rpc::EchoResponse response;
            response.set_message("pong");
            response.SerializeToString(&response_payload);
            response_serial_id = corpcron::rpc::kEchoResponseSerialId;
        } else {
            corpcron::rpc::RpcError error;
            error.set_code(corpcron::rpc::UNKNOWN_METHOD);
            error.set_message("unknown method");
            error.SerializeToString(&response_payload);
            response_serial_id = corpcron::rpc::kRpcErrorSerialId;
        }

        std::string frame = corpcron::rpc::encode(response_serial_id, response_payload);
        assert(send_all(fd, frame));
        close(fd);
    }
    close(listen_fd);
}

void serve_two_requests_same_connection(int listen_fd) {
    int fd = accept(listen_fd, nullptr, nullptr);
    assert(fd >= 0);
    for (int i = 0; i < 2; ++i) {
        uint32_t request_serial_id = 0;
        (void)read_frame_payload(fd, request_serial_id);

        corpcron::rpc::EchoResponse response;
        response.set_message(i == 0 ? "reuse-1" : "reuse-2");
        std::string response_payload;
        response.SerializeToString(&response_payload);
        std::string frame = corpcron::rpc::encode(corpcron::rpc::kEchoResponseSerialId, response_payload);
        assert(send_all(fd, frame));
    }
    close(fd);
    close(listen_fd);
}

} // namespace

int main() {
    uint16_t port = 0;
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        std::cout << "Skipping RpcClient test: loopback socket is not available.\n";
        return 77;
    }
    std::thread server_thread(serve_two_requests, listen_fd);

    corpcron::rpc::EchoRequest request;
    request.set_message("ping");
    std::string request_payload;
    request.SerializeToString(&request_payload);

    corpcron::RpcClient client("127.0.0.1", port);
    uint32_t response_serial_id = 0;
    std::string response_payload;
    assert(client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                       response_serial_id, response_payload, 1000));
    assert(response_serial_id == corpcron::rpc::kEchoResponseSerialId);
    corpcron::rpc::EchoResponse echo_response;
    assert(echo_response.ParseFromString(response_payload));
    assert(echo_response.message() == "pong");

    corpcron::RpcClient error_client("127.0.0.1", port);
    assert(error_client.call(9999, request_payload, response_serial_id, response_payload, 1000));
    assert(response_serial_id == corpcron::rpc::kRpcErrorSerialId);
    corpcron::rpc::RpcError error;
    assert(error.ParseFromString(response_payload));
    assert(error.code() == corpcron::rpc::UNKNOWN_METHOD);

    server_thread.join();

    corpcron::RpcClient failed_client("127.0.0.1", port);
    assert(!failed_client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                               response_serial_id, response_payload, 200));

    uint16_t reuse_port = 0;
    int reuse_listen_fd = create_listen_socket(reuse_port);
    if (reuse_listen_fd < 0) {
        std::cout << "Skipping RpcClient reuse test: loopback socket is not available.\n";
        return 77;
    }
    std::thread reuse_server_thread(serve_two_requests_same_connection, reuse_listen_fd);
    corpcron::RpcClient reuse_client("127.0.0.1", reuse_port);
    assert(reuse_client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                             response_serial_id, response_payload, 1000));
    assert(response_serial_id == corpcron::rpc::kEchoResponseSerialId);
    assert(echo_response.ParseFromString(response_payload));
    assert(echo_response.message() == "reuse-1");
    assert(reuse_client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                             response_serial_id, response_payload, 1000));
    assert(response_serial_id == corpcron::rpc::kEchoResponseSerialId);
    assert(echo_response.ParseFromString(response_payload));
    assert(echo_response.message() == "reuse-2");
    reuse_server_thread.join();
    return 0;
}
