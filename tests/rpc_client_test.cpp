#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client.hpp"
#include "rpc.pb.h"
#include <arpa/inet.h>
#include <cassert>
#include <atomic>
#include <chrono>
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

void serve_slow_header(int listen_fd) {
    int fd = accept(listen_fd, nullptr, nullptr);
    assert(fd >= 0);
    uint32_t request_serial_id = 0;
    (void)read_frame_payload(fd, request_serial_id);

    corpcron::rpc::EchoResponse response;
    response.set_message("too-slow");
    std::string response_payload;
    response.SerializeToString(&response_payload);
    std::string frame = corpcron::rpc::encode(corpcron::rpc::kEchoResponseSerialId, response_payload);
    for (char ch : frame) {
        if (send(fd, &ch, 1, MSG_NOSIGNAL) <= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    close(fd);
    close(listen_fd);
}

void serve_never_respond(int listen_fd) {
    int fd = accept(listen_fd, nullptr, nullptr);
    assert(fd >= 0);
    uint32_t request_serial_id = 0;
    (void)read_frame_payload(fd, request_serial_id);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    close(fd);
    close(listen_fd);
}

void serve_stream_metrics(int listen_fd) {
    int fd = accept(listen_fd, nullptr, nullptr);
    assert(fd >= 0);
    uint32_t request_serial_id = 0;
    (void)read_frame_payload(fd, request_serial_id);
    assert(request_serial_id == corpcron::rpc::kStreamMetricsRequestSerialId);

    for (int i = 0; i < 3; ++i) {
        corpcron::rpc::StreamMetricsResponse response;
        response.set_success(true);
        response.set_sequence(static_cast<uint32_t>(i));
        response.set_end_of_stream(i == 2);
        response.mutable_metrics()->set_success(true);
        response.mutable_metrics()->set_rpc_requests_total(static_cast<uint64_t>(100 + i));
        std::string payload;
        response.SerializeToString(&payload);
        assert(send_all(fd, corpcron::rpc::encode(corpcron::rpc::kStreamMetricsResponseSerialId,
                                                  payload)));
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
    response_serial_id = 12345;
    response_payload = "stale response";
    assert(!failed_client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                               response_serial_id, response_payload, 200));
    assert(response_serial_id == 0);
    assert(response_payload.empty());

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

    uint16_t slow_port = 0;
    int slow_listen_fd = create_listen_socket(slow_port);
    if (slow_listen_fd < 0) {
        std::cout << "Skipping RpcClient deadline test: loopback socket is not available.\n";
        return 77;
    }
    std::thread slow_server_thread(serve_slow_header, slow_listen_fd);
    corpcron::RpcClient slow_client("127.0.0.1", slow_port);
    auto deadline_start = std::chrono::steady_clock::now();
    assert(!slow_client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                             response_serial_id, response_payload, 180));
    auto deadline_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - deadline_start).count();
    assert(slow_client.lastStatus() == corpcron::RpcCallStatus::DeadlineExceeded);
    assert(deadline_elapsed < 1000);
    slow_server_thread.join();

    uint16_t cancel_port = 0;
    int cancel_listen_fd = create_listen_socket(cancel_port);
    if (cancel_listen_fd < 0) {
        std::cout << "Skipping RpcClient cancellation test: loopback socket is not available.\n";
        return 77;
    }
    std::thread cancel_server_thread(serve_never_respond, cancel_listen_fd);
    corpcron::RpcClient cancel_client("127.0.0.1", cancel_port);
    corpcron::CancellationSource source;
    corpcron::RpcCallOptions options = corpcron::RpcCallOptions::fromTimeout(5000);
    options.cancellation = source.token();
    std::atomic<bool> call_done{false};
    std::atomic<bool> call_ok{true};
    std::thread call_thread([&]() {
        call_ok = cancel_client.call(corpcron::rpc::kEchoRequestSerialId, request_payload,
                                     response_serial_id, response_payload, options);
        call_done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    source.cancel();
    call_thread.join();
    assert(call_done);
    assert(!call_ok);
    assert(cancel_client.lastStatus() == corpcron::RpcCallStatus::Canceled);
    cancel_server_thread.join();

    uint16_t stream_port = 0;
    int stream_listen_fd = create_listen_socket(stream_port);
    if (stream_listen_fd < 0) {
        std::cout << "Skipping RpcClient stream test: loopback socket is not available.\n";
        return 77;
    }
    std::thread stream_server_thread(serve_stream_metrics, stream_listen_fd);
    corpcron::rpc::StreamMetricsRequest stream_request;
    stream_request.set_samples(3);
    std::string stream_request_payload;
    stream_request.SerializeToString(&stream_request_payload);
    corpcron::RpcClient stream_client("127.0.0.1", stream_port);
    int stream_count = 0;
    bool saw_stream_end = false;
    assert(stream_client.callStream(corpcron::rpc::kStreamMetricsRequestSerialId,
                                    stream_request_payload,
                                    [&](uint32_t stream_serial_id, const std::string& stream_payload) {
        assert(stream_serial_id == corpcron::rpc::kStreamMetricsResponseSerialId);
        corpcron::rpc::StreamMetricsResponse item;
        assert(item.ParseFromString(stream_payload));
        assert(item.sequence() == static_cast<uint32_t>(stream_count));
        saw_stream_end = item.end_of_stream();
        ++stream_count;
        return !saw_stream_end;
    }, 1000));
    assert(stream_count == 3);
    assert(saw_stream_end);
    stream_server_thread.join();
    return 0;
}
