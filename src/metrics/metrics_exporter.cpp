#include "corpcron/metrics/metrics_exporter.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/metrics/alert_rules.hpp"
#include "corpcron/metrics/metrics.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <exception>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace corpcron {

namespace {

std::string errnoMessage(const std::string& operation) {
    return operation + ": " + std::strerror(errno);
}

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string httpResponse(int status, const std::string& status_text,
                         const std::string& content_type, const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << status_text << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

} // namespace

MetricsExporter::MetricsExporter(std::string host, int port, AlertRuleConfig alert_config)
    : host_(std::move(host)), port_(port), alert_config_(std::move(alert_config)) {}

MetricsExporter::~MetricsExporter() {
    stop();
}

bool MetricsExporter::start() {
    if (running_) return true;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        LOG_ERROR(errnoMessage("metrics socket"));
        return false;
    }

    int opt = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR(errnoMessage("metrics setsockopt SO_REUSEADDR"));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        LOG_ERROR("metrics exporter only supports IPv4 bind host: " + host_);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR(errnoMessage("metrics bind"));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 16) < 0) {
        LOG_ERROR(errnoMessage("metrics listen"));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    try {
        thread_ = std::thread(&MetricsExporter::run, this);
    } catch (const std::exception& e) {
        running_ = false;
        ::close(listen_fd_);
        listen_fd_ = -1;
        LOG_ERROR(std::string("Failed to start metrics exporter thread: ") + e.what());
        return false;
    }
    LOG_INFO("Metrics exporter listening on " + host_ + ":" + std::to_string(port_));
    return true;
}

void MetricsExporter::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

void MetricsExporter::run() {
    while (running_) {
        int client_fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (running_) LOG_ERROR(errnoMessage("metrics accept"));
            continue;
        }
        if (!running_) {
            ::close(client_fd);
            break;
        }
        timeval timeout{};
        timeout.tv_sec = 1;
        if (::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
            ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
            LOG_ERROR(errnoMessage("metrics client timeout setup"));
            ::close(client_fd);
            continue;
        }
        handleClient(client_fd);
        ::close(client_fd);
    }
}

void MetricsExporter::handleClient(int client_fd) const {
    char buffer[1024] = {};
    ssize_t n = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return;

    std::string request(buffer, static_cast<size_t>(n));
    const bool wants_metrics = request.rfind("GET /metrics ", 0) == 0;
    const bool wants_health = request.rfind("GET /health ", 0) == 0;
    const bool wants_alerts = request.rfind("GET /alerts ", 0) == 0;

    if (wants_metrics) {
        const std::string body = Metrics::instance().renderPrometheus();
        sendAll(client_fd, httpResponse(200, "OK", "text/plain; version=0.0.4", body));
        return;
    }

    if (wants_health) {
        sendAll(client_fd, httpResponse(200, "OK", "text/plain", "ok\n"));
        return;
    }

    if (wants_alerts) {
        const std::string body =
            AlertRules::renderText(Metrics::instance().snapshot(), alert_config_);
        sendAll(client_fd, httpResponse(200, "OK", "text/plain", body));
        return;
    }

    sendAll(client_fd, httpResponse(404, "Not Found", "text/plain", "not found\n"));
}

} // namespace corpcron
