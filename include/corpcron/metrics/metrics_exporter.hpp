#pragma once

#include "corpcron/metrics/alert_rules.hpp"
#include <atomic>
#include <string>
#include <thread>

namespace corpcron {

class MetricsExporter {
public:
    MetricsExporter(std::string host, int port, AlertRuleConfig alert_config = {});
    ~MetricsExporter();

    bool start();
    void stop();

private:
    void run();
    void handleClient(int client_fd) const;

    std::string host_;
    int port_ = 0;
    AlertRuleConfig alert_config_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace corpcron
