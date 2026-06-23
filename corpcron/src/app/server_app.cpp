#include "corpcron/app/server_app.hpp"
#include "corpcron/common/config.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include <fcntl.h>
#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace corpcron {

ServerApp::ServerApp(std::string config_path)
    : config_path_(std::move(config_path)) {}

ServerApp::~ServerApp() {
    stop();
    cleanup();
}

int ServerApp::run() {
    if (!loadConfig()) return 1;
    if (!maybeDaemonize()) return 1;

    signal(SIGPIPE, SIG_IGN);
    if (!setupSignalHandling()) return 1;

    registerHandlers();

    if (!initRedis()) return 1;
    if (!initMySQL()) return 1;

    startScheduler();
    if (!registerService()) return 1;
    startHeartbeat();
    startSignalThread();

    dispatcher_ = std::make_shared<RpcDispatcher>(db_, auth_token_);
    server_ = std::make_unique<TcpServer>(bind_host_, port_, dispatcher_, max_connections_);

    bool ok = server_->start();
    if (!ok) {
        stop();
    }

    cleanup();
    return ok ? 0 : 1;
}

void ServerApp::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    stop_cv_.notify_all();

    if (scheduler_) scheduler_->stop();
    if (server_) server_->stop();
}

bool ServerApp::loadConfig() {
    if (!Config::instance().load(config_path_)) {
        std::cerr << "Failed to load config: " << config_path_ << std::endl;
        return false;
    }

    port_ = Config::instance().getInt("server.listen_port", 8080);
    bind_host_ = Config::instance().get("server.bind_host", "0.0.0.0");
    std::string advertise_host = Config::instance().get("server.advertise_host", "127.0.0.1");
    max_connections_ = static_cast<size_t>(Config::instance().getInt("server.max_connections", 1024));
    auth_token_ = Config::instance().get("rpc.auth_token", "");
    endpoint_ = advertise_host + ":" + std::to_string(port_);
    node_id_ = Config::instance().get("server.node_id", "");
    if (node_id_.empty()) node_id_ = endpoint_;
    return true;
}

bool ServerApp::maybeDaemonize() {
    bool daemon = Config::instance().getInt("server.daemon", 1) == 1;
    if (!daemon) return true;
    if (!daemonize()) {
        std::cerr << "Failed to daemonize" << std::endl;
        return false;
    }
    return true;
}

bool ServerApp::setupSignalHandling() {
    sigemptyset(&signal_set_);
    sigaddset(&signal_set_, SIGINT);
    sigaddset(&signal_set_, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signal_set_, nullptr) != 0) {
        std::cerr << "Failed to block signals" << std::endl;
        return false;
    }
    return true;
}

bool ServerApp::initRedis() {
    std::string redis_host = Config::instance().get("redis.host", "127.0.0.1");
    int redis_port = Config::instance().getInt("redis.port", 6380);
    redis_ = std::make_shared<RedisClient>(redis_host, redis_port);
    if (!redis_->connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        return false;
    }
    return true;
}

bool ServerApp::initMySQL() {
    std::string mysql_host = Config::instance().get("mysql.host", "127.0.0.1");
    int mysql_port = Config::instance().getInt("mysql.port", 3307);
    std::string mysql_user = Config::instance().get("mysql.user", "root");
    std::string mysql_pass = Config::instance().get("mysql.password", "");
    std::string mysql_db = Config::instance().get("mysql.database", "corpcron");
    db_ = std::make_shared<MySQLClient>(mysql_host, mysql_port, mysql_user, mysql_pass, mysql_db);
    if (!db_->connect()) {
        std::cerr << "Failed to connect to MySQL" << std::endl;
        return false;
    }
    return true;
}

void ServerApp::registerHandlers() {
    HandlerRegistry::instance().registerHandler("Echo", [](const std::string& params) {
        return "Echo: " + params;
    });
}

void ServerApp::startScheduler() {
    scheduler_ = std::make_unique<TaskScheduler>(db_, redis_, node_id_);
    scheduler_->start();
}

bool ServerApp::registerService() {
    service_registered_ = redis_->registerService("rpc", endpoint_, 30);
    if (!service_registered_) {
        std::cerr << "Failed to register RPC service" << std::endl;
    }
    return service_registered_;
}

void ServerApp::startHeartbeat() {
    heartbeat_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopping_) {
            if (stop_cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return stopping_; })) {
                break;
            }
            // heartbeat 可能阻塞，先释放应用生命周期锁；RedisClient 内部负责命令级互斥。
            lock.unlock();
            redis_->heartbeat("rpc", endpoint_, 30);
            lock.lock();
        }
    });
}

void ServerApp::startSignalThread() {
    signal_thread_ = std::thread([this]() {
        int sig = 0;
        if (sigwait(&signal_set_, &sig) == 0) {
            stop();
        }
    });
}

void ServerApp::cleanup() {
    stop();

    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    if (service_registered_ && redis_) {
        redis_->unregisterService("rpc", endpoint_);
        service_registered_ = false;
    }

    stopSignalThread();
}

void ServerApp::stopSignalThread() {
    if (!signal_thread_.joinable()) return;
    pthread_kill(signal_thread_.native_handle(), SIGTERM);
    signal_thread_.join();
}

bool ServerApp::daemonize() {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);
    if (setsid() < 0) return false;

    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);

    if (chdir("/") < 0) return false;
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return false;
    if (dup2(fd, STDIN_FILENO) < 0) return false;
    if (dup2(fd, STDOUT_FILENO) < 0) return false;
    if (dup2(fd, STDERR_FILENO) < 0) return false;
    if (fd > STDERR_FILENO) close(fd);
    return true;
}

} // namespace corpcron
