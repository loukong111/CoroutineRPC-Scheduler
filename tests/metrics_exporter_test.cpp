#include "corpcron/metrics/metrics_exporter.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <netinet/in.h>
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
        std::cout << "Skipping MetricsExporter test: loopback socket is not available.\n";
        return 77;
    }
    close(reservation);

    corpcron::MetricsExporter exporter("127.0.0.1", port);
    assert(exporter.start());

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(client_fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    assert(connect(client_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto started = std::chrono::steady_clock::now();
    exporter.stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    close(client_fd);

    assert(elapsed < std::chrono::milliseconds(2000));
    return 0;
}
