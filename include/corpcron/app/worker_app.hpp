#pragma once

#include "corpcron/metrics/metrics_exporter.hpp"
#include "corpcron/net/tcp_server.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include <condition_variable>
#include <csignal>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace corpcron {

class WorkerApp {
public:
    explicit WorkerApp(std::string config_path);
    ~WorkerApp();

    int run();
    void stop();

private:
    bool loadConfig();
    bool maybeDaemonize();
    bool setupSignalHandling();
    bool initRedis();
    bool startMetricsExporter();
    bool registerServices();
    void startHeartbeat();
    void startSignalThread();
    void cleanup();
    void stopSignalThread();

    static bool daemonize();

    std::string config_path_;
    std::string endpoint_;
    std::string node_id_;
    std::string bind_host_;
    std::string auth_token_;
    std::string service_name_ = "worker";
    int port_ = 8181;
    bool metrics_enabled_ = true;
    std::string metrics_host_ = "127.0.0.1";
    int metrics_port_ = 9191;
    AlertRuleConfig alert_config_;
    size_t max_connections_ = 1024;
    RpcExecutorOptions executor_options_;

    std::shared_ptr<RedisClient> redis_;
    std::unique_ptr<MetricsExporter> metrics_exporter_;
    std::shared_ptr<RpcDispatcher> dispatcher_;
    std::unique_ptr<TcpServer> server_;
    std::vector<std::string> registered_services_;

    std::mutex mutex_;
    std::condition_variable stop_cv_;
    bool stopping_ = false;

    std::thread heartbeat_thread_;
    std::thread signal_thread_;
    sigset_t signal_set_{};
};

} // namespace corpcron
