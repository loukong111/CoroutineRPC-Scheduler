#include "corpcron/common/config.hpp"
#include "corpcron/common/memory_pool.hpp"
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
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);
    return true;
}

int main(int argc, char* argv[]) {
    printf("=== main started ===\n");

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

    // 注册处理器
    HandlerRegistry::instance().registerHandler("Echo", [](const std::string& params) {
        return "Echo: " + params;
    });

    // 内存池测试
    MemoryPool pool(64, 10);
    void* p1 = pool.allocate();
    void* p2 = pool.allocate();
    printf("Memory pool test: allocated two objects\n");
    pool.deallocate(p1);
    pool.deallocate(p2);

    int port = Config::instance().getInt("server.listen_port", 8080);
    printf("Starting TCP server on port %d\n", port);

    // Redis 连接
    std::string redis_host = Config::instance().get("redis.host", "127.0.0.1");
    int redis_port = Config::instance().getInt("redis.port", 6379);
    auto redis = std::make_shared<RedisClient>(redis_host, redis_port);
    if (!redis->connect()) {
        fprintf(stderr, "Failed to connect to Redis\n");
        return 1;
    }
    printf("Connected to Redis\n");

    std::string endpoint = "127.0.0.1:" + std::to_string(port);
    redis->registerService("rpc", endpoint, 30);
    std::thread heartbeat_thread([&]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            redis->heartbeat("rpc", endpoint, 30);
        }
    });

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
    g_scheduler = std::make_unique<TaskScheduler>(db, redis, endpoint);
    g_scheduler->start();
    printf("Task scheduler started\n");

    // 创建 TCP 服务器（传入 db 和 redis）
    TcpServer server("0.0.0.0", port, db, redis);
    g_server = &server;

    server.start();

    printf("Server stopped\n");
    heartbeat_thread.join();
    return 0;
}