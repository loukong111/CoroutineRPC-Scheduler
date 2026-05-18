#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "rpc.pb.h"
#include "corpcron/rpc/protocol.hpp"

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8081);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return 1;
    }

    // 构造 SubmitTaskRequest
    corpcron::rpc::SubmitTaskRequest req;
    req.set_cron_expr("* * * * * ?");
    req.set_params("Hello from test client");
    req.set_handler("Echo");

    std::string payload;
    req.SerializeToString(&payload);
    std::string data = corpcron::rpc::encode(3, payload);
    send(sock, data.data(), data.size(), 0);

    char buffer[4096];
    ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
    if (n > 0) {
        uint32_t serial_id;
        std::string resp_payload;
        if (corpcron::rpc::decode(buffer, n, serial_id, resp_payload)) {
            corpcron::rpc::SubmitTaskResponse resp;
            if (resp.ParseFromString(resp_payload)) {
                std::cout << "Success=" << resp.success()
                          << ", TaskID=" << resp.task_id()
                          << ", Error=" << resp.error() << std::endl;
            }
        }
    }
    close(sock);
    return 0;
}