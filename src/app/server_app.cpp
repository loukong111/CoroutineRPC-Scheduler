#include "corpcron/app/server_app.hpp"
#include "corpcron/common/config.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/rpc_interceptor.hpp"
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
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

    if (!startMetricsExporter()) return 1;

    dispatcher_ = std::make_shared<RpcDispatcher>(db_, redis_, auth_token_, node_id_);
    dispatcher_->setInterceptors(makeDefaultRpcInterceptorChain());
    server_ = std::make_unique<TcpServer>(bind_host_, port_, dispatcher_, max_connections_);

    bool ok = server_->start([this]() {
        // The listen socket is active at this point. Registering and starting the
        // scheduler earlier can make due tasks call a server that is not listening yet.
        if (!registerService()) return false;
        startHeartbeat();
        startScheduler();
        startSignalThread();
        return true;
    });
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
    if (metrics_exporter_) metrics_exporter_->stop();
    if (server_) server_->stop();
}

bool ServerApp::loadConfig() {
    if (!Config::instance().load(config_path_)) {
        LOG_ERROR("Failed to load config: " + config_path_);
        return false;
    }

    std::string log_level = Config::instance().get("server.log_level", "info");
    std::transform(log_level.begin(), log_level.end(), log_level.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (log_level == "debug") {
        Logger::instance().setLevel(DEBUG);
    } else if (log_level == "warn") {
        Logger::instance().setLevel(WARN);
    } else if (log_level == "error") {
        Logger::instance().setLevel(ERROR);
    } else {
        Logger::instance().setLevel(INFO);
        if (log_level != "info") {
            LOG_WARN("Unknown server.log_level '" + log_level + "', using info");
        }
    }

    port_ = Config::instance().getInt("server.listen_port", 8080);
    if (port_ <= 0 || port_ > 65535) {
        LOG_ERROR("server.listen_port must be between 1 and 65535");
        return false;
    }
    bind_host_ = Config::instance().get("server.bind_host", "0.0.0.0");
    std::string advertise_host = Config::instance().get("server.advertise_host", "127.0.0.1");
    int max_connections = Config::instance().getInt("server.max_connections", 1024);
    if (max_connections <= 0) {
        LOG_ERROR("server.max_connections must be greater than zero");
        return false;
    }
    max_connections_ = static_cast<size_t>(max_connections);
    auth_token_ = Config::instance().get("rpc.auth_token", "");
    metrics_enabled_ = Config::instance().getInt("metrics.enabled", 1) == 1;
    metrics_host_ = Config::instance().get("metrics.host", "127.0.0.1");
    metrics_port_ = Config::instance().getInt("metrics.port", 9091);
    if (metrics_enabled_ && (metrics_port_ <= 0 || metrics_port_ > 65535)) {
        LOG_ERROR("metrics.port must be between 1 and 65535");
        return false;
    }

    auto getAlertUint = [](const std::string& key, uint64_t default_value) {
        const int value = Config::instance().getInt(key, static_cast<int>(default_value));
        return value < 0 ? default_value : static_cast<uint64_t>(value);
    };
    alert_config_.min_rpc_requests =
        getAlertUint("alerts.min_rpc_requests", alert_config_.min_rpc_requests);
    alert_config_.rpc_error_rate_percent =
        getAlertUint("alerts.rpc_error_rate_percent", alert_config_.rpc_error_rate_percent);
    alert_config_.min_task_executions =
        getAlertUint("alerts.min_task_executions", alert_config_.min_task_executions);
    alert_config_.task_failure_rate_percent =
        getAlertUint("alerts.task_failure_rate_percent", alert_config_.task_failure_rate_percent);
    alert_config_.min_lock_attempts =
        getAlertUint("alerts.min_lock_attempts", alert_config_.min_lock_attempts);
    alert_config_.lock_failure_rate_percent =
        getAlertUint("alerts.lock_failure_rate_percent", alert_config_.lock_failure_rate_percent);
    alert_config_.schedule_delay_p99_ms =
        getAlertUint("alerts.schedule_delay_p99_ms", alert_config_.schedule_delay_p99_ms);
    alert_config_.task_duration_p99_ms =
        getAlertUint("alerts.task_duration_p99_ms", alert_config_.task_duration_p99_ms);

    endpoint_ = advertise_host + ":" + std::to_string(port_);
    node_id_ = Config::instance().get("server.node_id", "");
    if (node_id_.empty()) node_id_ = endpoint_;
    return true;
}

bool ServerApp::maybeDaemonize() {
    bool daemon = Config::instance().getInt("server.daemon", 1) == 1;
    if (!daemon) return true;
    if (!daemonize()) {
        LOG_ERROR("Failed to daemonize");
        return false;
    }
    return true;
}

bool ServerApp::setupSignalHandling() {
    sigemptyset(&signal_set_);
    sigaddset(&signal_set_, SIGINT);
    sigaddset(&signal_set_, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signal_set_, nullptr) != 0) {
        LOG_ERROR("Failed to block signals");
        return false;
    }
    return true;
}

bool ServerApp::initRedis() {
    std::string redis_host = Config::instance().get("redis.host", "127.0.0.1");
    int redis_port = Config::instance().getInt("redis.port", 6380);
    if (redis_port <= 0 || redis_port > 65535) {
        LOG_ERROR("redis.port must be between 1 and 65535");
        return false;
    }
    RedisClientOptions options;
    int redis_pool_size = Config::instance().getInt("redis.pool_size", 4);
    if (redis_pool_size <= 0) redis_pool_size = 1;
    options.pool_size = static_cast<size_t>(redis_pool_size);
    options.connect_timeout_ms = Config::instance().getInt("redis.connect_timeout_ms", 1000);
    options.command_timeout_ms = Config::instance().getInt("redis.command_timeout_ms", 1000);
    redis_ = std::make_shared<RedisClient>(redis_host, redis_port, options);
    if (!redis_->connect()) {
        auto error = redis_->lastError();
        LOG_ERROR("Failed to connect to Redis kind=" +
                  std::string(storageErrorKindName(error.kind)) + " message=" + error.message);
        return false;
    }
    return true;
}

bool ServerApp::initMySQL() {
    std::string mysql_host = Config::instance().get("mysql.host", "127.0.0.1");
    int mysql_port = Config::instance().getInt("mysql.port", 3307);
    if (mysql_port <= 0 || mysql_port > 65535) {
        LOG_ERROR("mysql.port must be between 1 and 65535");
        return false;
    }
    std::string mysql_user = Config::instance().get("mysql.user", "root");
    std::string mysql_pass = Config::instance().get("mysql.password", "");
    std::string mysql_db = Config::instance().get("mysql.database", "corpcron");
    MySQLClientOptions options;
    int mysql_pool_size = Config::instance().getInt("mysql.pool_size", 4);
    if (mysql_pool_size <= 0) mysql_pool_size = 1;
    options.pool_size = static_cast<size_t>(mysql_pool_size);
    options.connect_timeout_sec = Config::instance().getInt("mysql.connect_timeout_sec", 3);
    options.read_timeout_sec = Config::instance().getInt("mysql.read_timeout_sec", 5);
    options.write_timeout_sec = Config::instance().getInt("mysql.write_timeout_sec", 5);
    options.reconnect = Config::instance().getInt("mysql.reconnect", 1) == 1;
    db_ = std::make_shared<MySQLClient>(mysql_host, mysql_port, mysql_user, mysql_pass, mysql_db,
                                        options);
    if (!db_->connect()) {
        auto error = db_->lastError();
        LOG_ERROR("Failed to connect to MySQL kind=" +
                  std::string(storageErrorKindName(error.kind)) + " message=" + error.message);
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

bool ServerApp::startMetricsExporter() {
    if (!metrics_enabled_) return true;
    metrics_exporter_ =
        std::make_unique<MetricsExporter>(metrics_host_, metrics_port_, alert_config_);
    return metrics_exporter_->start();
}

bool ServerApp::registerService() {
    service_registered_ = redis_->registerService("rpc", endpoint_, 30);
    if (!service_registered_) {
        LOG_ERROR("Failed to register RPC service");
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
            if (!redis_->heartbeat("rpc", endpoint_, 30)) {
                const auto error = redis_->lastError();
                LOG_WARN_EVENT("service_heartbeat_failed", {
                    {"endpoint", endpoint_},
                    {"error_kind", storageErrorKindName(error.kind)},
                    {"error", error.message}
                });
            }
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
