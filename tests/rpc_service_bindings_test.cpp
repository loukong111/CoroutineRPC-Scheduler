#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client.hpp"
#include "rpc_service.hpp"
#include <arpa/inet.h>
#include <cassert>
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
    if (listen(listen_fd, 4) != 0) {
        close(listen_fd);
        return -1;
    }

    socklen_t len = sizeof(addr);
    assert(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    port = ntohs(addr.sin_port);
    return listen_fd;
}

void serve_stub_request(int listen_fd) {
    int fd = accept(listen_fd, nullptr, nullptr);
    assert(fd >= 0);

    uint32_t request_serial_id = 0;
    std::string request_payload = read_frame_payload(fd, request_serial_id);
    assert(request_serial_id == corpcron::rpc::kEchoRequestSerialId);

    corpcron::rpc::EchoRequest request;
    assert(request.ParseFromString(request_payload));
    assert(request.message() == "hello-stub");
    assert(request.auth_token() == "secret");

    corpcron::rpc::EchoResponse response;
    response.set_message("typed:" + request.message());
    std::string response_payload;
    response.SerializeToString(&response_payload);
    std::string frame = corpcron::rpc::encode(corpcron::rpc::kEchoResponseSerialId, response_payload);
    assert(send_all(fd, frame));
    close(fd);
    close(listen_fd);
}

void serve_stream_stub_request(int listen_fd) {
    int fd = accept(listen_fd, nullptr, nullptr);
    assert(fd >= 0);

    uint32_t request_serial_id = 0;
    std::string request_payload = read_frame_payload(fd, request_serial_id);
    assert(request_serial_id == corpcron::rpc::kStreamMetricsRequestSerialId);

    corpcron::rpc::StreamMetricsRequest request;
    assert(request.ParseFromString(request_payload));
    assert(request.auth_token() == "secret");

    for (int i = 0; i < 3; ++i) {
        corpcron::rpc::StreamMetricsResponse response;
        response.set_success(true);
        response.set_sequence(static_cast<uint32_t>(i));
        response.set_end_of_stream(i == 2);
        response.mutable_metrics()->set_success(true);
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
    const auto* echo = corpcron::rpc::findMethodByName("Echo");
    assert(echo != nullptr);
    assert(echo->request_serial_id == corpcron::rpc::kEchoRequestSerialId);
    assert(echo->response_serial_id == corpcron::rpc::kEchoResponseSerialId);
    assert(std::string(echo->request_type) == "EchoRequest");
    assert(corpcron::rpc::findMethodByRequestSerialId(corpcron::rpc::kRunTaskNowRequestSerialId) != nullptr);
    assert(corpcron::rpc::findMethodByName("Missing") == nullptr);

    corpcron::rpc::CorpCronRpcSkeleton skeleton;
    skeleton.registerEcho([](const corpcron::rpc::EchoRequest& request) {
        corpcron::rpc::EchoResponse response;
        response.set_message("skeleton:" + request.message());
        return response;
    });

    corpcron::rpc::EchoRequest request;
    request.set_message("hello");
    std::string payload;
    request.SerializeToString(&payload);
    auto response = skeleton.dispatch(corpcron::rpc::kEchoRequestSerialId, payload);
    assert(response.serial_id == corpcron::rpc::kEchoResponseSerialId);
    corpcron::rpc::EchoResponse echo_response;
    assert(echo_response.ParseFromString(response.payload));
    assert(echo_response.message() == "skeleton:hello");

    skeleton.registerStreamMetrics([](const corpcron::rpc::StreamMetricsRequest&) {
        std::vector<corpcron::rpc::StreamMetricsResponse> responses(3);
        for (size_t i = 0; i < responses.size(); ++i) {
            responses[i].set_success(true);
            responses[i].set_sequence(static_cast<uint32_t>(i));
        }
        return responses;
    });
    corpcron::rpc::StreamMetricsRequest skeleton_stream_request;
    std::string skeleton_stream_payload;
    assert(skeleton_stream_request.SerializeToString(&skeleton_stream_payload));
    auto skeleton_stream = skeleton.dispatchStream(corpcron::rpc::kStreamMetricsRequestSerialId,
                                                   skeleton_stream_payload);
    assert(skeleton_stream.responses.size() == 3);
    for (size_t i = 0; i < skeleton_stream.responses.size(); ++i) {
        assert(skeleton_stream.responses[i].serial_id ==
               corpcron::rpc::kStreamMetricsResponseSerialId);
        corpcron::rpc::StreamMetricsResponse item;
        assert(item.ParseFromString(skeleton_stream.responses[i].payload));
        assert(item.sequence() == i);
        assert(item.end_of_stream() == (i + 1 == skeleton_stream.responses.size()));
    }

    auto error_response = skeleton.dispatch(999999, payload);
    assert(error_response.serial_id == corpcron::rpc::kRpcErrorSerialId);
    corpcron::rpc::RpcError rpc_error;
    assert(rpc_error.ParseFromString(error_response.payload));
    assert(rpc_error.code() == corpcron::rpc::UNKNOWN_METHOD);

    uint16_t port = 0;
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        std::cout << "Skipping RpcServiceBindings test: loopback socket is not available.\n";
        return 77;
    }
    std::thread server_thread(serve_stub_request, listen_fd);

    corpcron::RpcClient client("127.0.0.1", port);
    corpcron::rpc::CorpCronRpcStub stub(client, "secret");
    corpcron::rpc::EchoRequest stub_request;
    stub_request.set_message("hello-stub");
    corpcron::rpc::EchoResponse stub_response;
    std::string error;
    assert(stub.Echo(stub_request, stub_response, &error, 1000));
    assert(error.empty());
    assert(stub_response.message() == "typed:hello-stub");

    server_thread.join();

    uint16_t stream_port = 0;
    int stream_listen_fd = create_listen_socket(stream_port);
    if (stream_listen_fd < 0) {
        std::cout << "Skipping RpcServiceBindings stream test: loopback socket is not available.\n";
        return 77;
    }
    std::thread stream_server_thread(serve_stream_stub_request, stream_listen_fd);

    corpcron::RpcClient stream_client("127.0.0.1", stream_port);
    corpcron::rpc::CorpCronRpcStub stream_stub(stream_client, "secret");
    corpcron::rpc::StreamMetricsRequest stream_request;
    stream_request.set_samples(3);
    int stream_count = 0;
    bool saw_end = false;
    assert(stream_stub.StreamMetrics(stream_request, [&](const corpcron::rpc::StreamMetricsResponse& item) {
        assert(item.sequence() == static_cast<uint32_t>(stream_count));
        saw_end = item.end_of_stream();
        ++stream_count;
        return true;
    }, &error, 1000));
    assert(error.empty());
    assert(stream_count == 3);
    assert(saw_end);
    stream_server_thread.join();
    return 0;
}
