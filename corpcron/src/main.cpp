#include "corpcron/common/config.hpp"
#include "corpcron/net/tcp_server.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/scheduler/task_scheduler.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <thread>
#include <atomic>

using namespace corpcron;

static TcpServer* g_server = nullptr;
static std::atomic<bool> running(true);
static std::unique_ptr<TaskScheduler> g_scheduler;

void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("Received signal, shutting down...\n");
        running = false;
        if (g_scheduler) g_scheduler->stop();
        if (g_server) g_server->stop();
    }
}

bool daemonize() {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid > 0) exit(0);
    if (setsid() < 0) return false;
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0) return false;
    if (pid > 0) exit(0);
    if (chdir("/") < 0) return false;
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return false;
    if (dup2(fd, STDIN_FILENO) < 0) return false;
    if (dup2(fd, STDOUT_FILENO) < 0) return false;
    if (dup2(fd, STDERR_FILENO) < 0) return false;
    if (fd > STDERR_FILENO) close(fd);
    return true;
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/server.conf";
    if (argc >= 2 && std::string(argv[1]) == "--config" && argc >= 3) {
        config_path = argv[2];
    }
    printf("Loading config from: %s\n", config_path.c_str());

    if (!Config::instance().load(config_path)) {
        fprintf(stderr, "Failed to load config: %s\n", config_path.c_str());
        return 1;
    }
    printf("Config loaded successfully\n");

    bool daemon = Config::instance().getInt("server.daemon", 1) == 1;
    printf("daemon = %d\n", daemon);
    if (daemon) {
        if (!daemonize()) {
            fprintf(stderr, "Failed to daemonize\n");
            return 1;
        }
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN);

    // 注册处理器
    HandlerRegistry::instance().registerHandler("Echo", [](const std::string& params) {
        return "Echo: " + params;
    });

    int port = Config::instance().getInt("server.listen_port", 8080);
    std::string bind_host = Config::instance().get("server.bind_host", "0.0.0.0");
    std::string advertise_host = Config::instance().get("server.advertise_host", "127.0.0.1");
    size_t max_connections = static_cast<size_t>(Config::instance().getInt("server.max_connections", 1024));
    std::string auth_token = Config::instance().get("rpc.auth_token", "");
    std::string endpoint = advertise_host + ":" + std::to_string(port);
    std::string node_id = Config::instance().get("server.node_id", "");
    if (node_id.empty()) node_id = endpoint;
    printf("Starting TCP server on %s:%d, advertise endpoint %s\n",
           bind_host.c_str(), port, endpoint.c_str());

    // Redis 连接
    std::string redis_host = Config::instance().get("redis.host", "127.0.0.1");
    int redis_port = Config::instance().getInt("redis.port", 6379);
    auto redis = std::make_shared<RedisClient>(redis_host, redis_port);
    if (!redis->connect()) {
        fprintf(stderr, "Failed to connect to Redis\n");
        return 1;
    }
    printf("Connected to Redis\n");

    // MySQL 连接
    std::string mysql_host = Config::instance().get("mysql.host", "127.0.0.1");
    int mysql_port = Config::instance().getInt("mysql.port", 3306);
    std::string mysql_user = Config::instance().get("mysql.user", "root");
    std::string mysql_pass = Config::instance().get("mysql.password", "");
    std::string mysql_db = Config::instance().get("mysql.database", "corpcron");
    auto db = std::make_shared<MySQLClient>(mysql_host, mysql_port, mysql_user, mysql_pass, mysql_db);
    if (!db->connect()) {
        fprintf(stderr, "Failed to connect to MySQL\n");
        return 1;
    }
    printf("Connected to MySQL\n");

    // 启动调度器
    g_scheduler = std::make_unique<TaskScheduler>(db, redis, node_id);
    g_scheduler->start();
    printf("Task scheduler started\n");

    redis->registerService("rpc", endpoint, 30);
    std::thread heartbeat_thread([&]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (running) redis->heartbeat("rpc", endpoint, 30);
        }
    });

    // 创建 TCP 服务器（传入 db 和 redis）
    TcpServer server(bind_host, port, db, redis, max_connections, auth_token);
    g_server = &server;

    if (!server.start()) {
        running = false;
        g_scheduler->stop();
    }

    printf("Server stopped\n");
    redis->unregisterService("rpc", endpoint);
    heartbeat_thread.join();
    return 0;
}
