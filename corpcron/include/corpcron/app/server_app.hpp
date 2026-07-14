#pragma once

#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/metrics/metrics_exporter.hpp"
#include "corpcron/net/tcp_server.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/rpc/rpc_dispatcher.hpp"
#include "corpcron/scheduler/task_scheduler.hpp"
#include <condition_variable>
#include <csignal>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace corpcron {

class ServerApp {
public:
    explicit ServerApp(std::string config_path);
    ~ServerApp();

    int run();
    void stop();

private:
    bool loadConfig();
    bool maybeDaemonize();
    bool setupSignalHandling();
    bool initRedis();
    bool initMySQL();
    void registerHandlers();
    void startScheduler();
    bool startMetricsExporter();
    bool registerService();
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
    int port_ = 8080;
    bool metrics_enabled_ = true;
    std::string metrics_host_ = "127.0.0.1";
    int metrics_port_ = 9091;
    AlertRuleConfig alert_config_;
    size_t max_connections_ = 1024;

    std::shared_ptr<RedisClient> redis_;
    std::shared_ptr<MySQLClient> db_;
    std::unique_ptr<TaskScheduler> scheduler_;
    std::unique_ptr<MetricsExporter> metrics_exporter_;
    std::shared_ptr<RpcDispatcher> dispatcher_;
    std::unique_ptr<TcpServer> server_;

    std::mutex mutex_;
    std::condition_variable stop_cv_;
    bool stopping_ = false;
    bool service_registered_ = false;

    std::thread heartbeat_thread_;
    std::thread signal_thread_;
    sigset_t signal_set_{};
};

} // namespace corpcron
