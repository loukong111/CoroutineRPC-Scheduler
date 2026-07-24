#include "corpcron/app/worker_app.hpp"
#include "corpcron/common/config.hpp"
#include "corpcron/common/logger.hpp"
#include "corpcron/common/storage_error.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include "corpcron/rpc/rpc_interceptor.hpp"
#include "corpcron/worker/builtin_handlers.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace corpcron {

namespace {

size_t positiveSizeConfig(const std::string& key, size_t fallback) {
    const int value = Config::instance().getInt(key, static_cast<int>(fallback));
    return value > 0 ? static_cast<size_t>(value) : fallback;
}

void configureLogLevel(const std::string& value, const std::string& key) {
    std::string level = value;
    std::transform(level.begin(), level.end(), level.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (level == "debug") {
        Logger::instance().setLevel(DEBUG);
    } else if (level == "warn") {
        Logger::instance().setLevel(WARN);
    } else if (level == "error") {
        Logger::instance().setLevel(ERROR);
    } else {
        Logger::instance().setLevel(INFO);
        if (level != "info") {
            LOG_WARN("Unknown " + key + " '" + level + "', using info");
        }
    }
}

} // namespace

WorkerApp::WorkerApp(std::string config_path)
    : config_path_(std::move(config_path)) {}

WorkerApp::~WorkerApp() {
    stop();
    cleanup();
}

int WorkerApp::run() {
    if (!loadConfig()) return 1;
    if (!maybeDaemonize()) return 1;

    signal(SIGPIPE, SIG_IGN);
    if (!setupSignalHandling()) return 1;

    registerBuiltinTaskHandlers();
    if (!initRedis()) return 1;
    if (!startMetricsExporter()) return 1;

    RpcDispatcherOptions dispatcher_options;
    dispatcher_options.role = RpcNodeRole::Worker;
    dispatcher_options.service_name = service_name_;
    dispatcher_options.worker_service_name = service_name_;
    dispatcher_ = std::make_shared<RpcDispatcher>(
        nullptr, redis_, auth_token_, node_id_, std::move(dispatcher_options));
    dispatcher_->setInterceptors(makeDefaultRpcInterceptorChain());
    server_ = std::make_unique<TcpServer>(
        bind_host_, port_, dispatcher_, max_connections_, executor_options_);

    const bool ok = server_->start([this]() {
        if (!registerServices()) return false;
        startHeartbeat();
        startSignalThread();
        return true;
    });
    if (!ok) stop();

    cleanup();
    return ok ? 0 : 1;
}

void WorkerApp::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    stop_cv_.notify_all();
    if (metrics_exporter_) metrics_exporter_->stop();
    if (server_) server_->stop();
}

bool WorkerApp::loadConfig() {
    if (!Config::instance().load(config_path_)) {
        LOG_ERROR("Failed to load config: " + config_path_);
        return false;
    }

    configureLogLevel(Config::instance().get("worker.log_level", "info"),
                      "worker.log_level");
    port_ = Config::instance().getInt("worker.listen_port", 8181);
    if (port_ <= 0 || port_ > 65535) {
        LOG_ERROR("worker.listen_port must be between 1 and 65535");
        return false;
    }
    bind_host_ = Config::instance().get("worker.bind_host", "0.0.0.0");
    const std::string advertise_host =
        Config::instance().get("worker.advertise_host", "127.0.0.1");
    service_name_ = Config::instance().get("worker.service_name", "worker");
    if (service_name_.empty()) {
        LOG_ERROR("worker.service_name must not be empty");
        return false;
    }
    max_connections_ =
        positiveSizeConfig("worker.max_connections", max_connections_);
    executor_options_.min_threads =
        positiveSizeConfig("worker.min_threads", executor_options_.min_threads);
    executor_options_.max_threads =
        positiveSizeConfig("worker.max_threads", executor_options_.max_threads);
    if (executor_options_.max_threads < executor_options_.min_threads) {
        executor_options_.max_threads = executor_options_.min_threads;
    }
    executor_options_.backlog_threshold = positiveSizeConfig(
        "worker.backlog_threshold", executor_options_.backlog_threshold);
    executor_options_.max_pending_requests = positiveSizeConfig(
        "worker.max_pending_requests", executor_options_.max_pending_requests);
    executor_options_.idle_timeout_sec =
        Config::instance().getInt("worker.idle_timeout_sec", 5);
    if (executor_options_.idle_timeout_sec <= 0) {
        executor_options_.idle_timeout_sec = 5;
    }

    auth_token_ = Config::instance().get("rpc.auth_token", "");
    metrics_enabled_ = Config::instance().getInt("metrics.enabled", 1) == 1;
    metrics_host_ = Config::instance().get("metrics.host", "127.0.0.1");
    metrics_port_ = Config::instance().getInt("metrics.port", 9191);
    if (metrics_enabled_ && (metrics_port_ <= 0 || metrics_port_ > 65535)) {
        LOG_ERROR("metrics.port must be between 1 and 65535");
        return false;
    }

    endpoint_ = advertise_host + ":" + std::to_string(port_);
    node_id_ = Config::instance().get("worker.node_id", "");
    if (node_id_.empty()) node_id_ = endpoint_;
    return true;
}

bool WorkerApp::maybeDaemonize() {
    if (Config::instance().getInt("worker.daemon", 0) != 1) return true;
    if (daemonize()) return true;
    LOG_ERROR("Failed to daemonize worker");
    return false;
}

bool WorkerApp::setupSignalHandling() {
    sigemptyset(&signal_set_);
    sigaddset(&signal_set_, SIGINT);
    sigaddset(&signal_set_, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signal_set_, nullptr) != 0) {
        LOG_ERROR("Failed to block worker signals");
        return false;
    }
    return true;
}

bool WorkerApp::initRedis() {
    const std::string host = Config::instance().get("redis.host", "127.0.0.1");
    const int port = Config::instance().getInt("redis.port", 6380);
    if (port <= 0 || port > 65535) {
        LOG_ERROR("redis.port must be between 1 and 65535");
        return false;
    }
    RedisClientOptions options;
    options.pool_size = positiveSizeConfig("redis.pool_size", 4);
    options.connect_timeout_ms =
        Config::instance().getInt("redis.connect_timeout_ms", 1000);
    options.command_timeout_ms =
        Config::instance().getInt("redis.command_timeout_ms", 1000);
    redis_ = std::make_shared<RedisClient>(host, port, options);
    if (redis_->connect()) return true;

    const auto error = redis_->lastError();
    LOG_ERROR("Failed to connect worker to Redis kind=" +
              std::string(storageErrorKindName(error.kind)) +
              " message=" + error.message);
    return false;
}

bool WorkerApp::startMetricsExporter() {
    if (!metrics_enabled_) return true;
    metrics_exporter_ =
        std::make_unique<MetricsExporter>(metrics_host_, metrics_port_, alert_config_);
    return metrics_exporter_->start();
}

bool WorkerApp::registerServices() {
    std::vector<std::string> services{service_name_};
    for (const auto& handler : HandlerRegistry::instance().handlerNames()) {
        services.push_back(service_name_ + ":" + handler);
    }

    for (const auto& service : services) {
        if (!redis_->registerService(service, endpoint_, 30)) {
            LOG_ERROR_EVENT("worker_service_registration_failed", {
                {"service", service}, {"endpoint", endpoint_}
            });
            for (const auto& registered : registered_services_) {
                redis_->unregisterService(registered, endpoint_);
            }
            registered_services_.clear();
            return false;
        }
        registered_services_.push_back(service);
    }
    LOG_INFO_EVENT("worker_registered", {
        {"node_id", node_id_},
        {"endpoint", endpoint_},
        {"handlers", std::to_string(registered_services_.size() - 1)}
    });
    return true;
}

void WorkerApp::startHeartbeat() {
    heartbeat_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopping_) {
            if (stop_cv_.wait_for(lock, std::chrono::seconds(5),
                                  [this]() { return stopping_; })) {
                break;
            }
            const auto services = registered_services_;
            lock.unlock();
            for (const auto& service : services) {
                if (!redis_->heartbeat(service, endpoint_, 30)) {
                    const auto error = redis_->lastError();
                    LOG_WARN_EVENT("worker_heartbeat_failed", {
                        {"service", service},
                        {"endpoint", endpoint_},
                        {"error_kind", storageErrorKindName(error.kind)},
                        {"error", error.message}
                    });
                }
            }
            lock.lock();
        }
    });
}

void WorkerApp::startSignalThread() {
    signal_thread_ = std::thread([this]() {
        int sig = 0;
        if (sigwait(&signal_set_, &sig) == 0) stop();
    });
}

void WorkerApp::cleanup() {
    stop();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (redis_) {
        for (const auto& service : registered_services_) {
            redis_->unregisterService(service, endpoint_);
        }
    }
    registered_services_.clear();
    stopSignalThread();
}

void WorkerApp::stopSignalThread() {
    if (!signal_thread_.joinable()) return;
    pthread_kill(signal_thread_.native_handle(), SIGTERM);
    signal_thread_.join();
}

bool WorkerApp::daemonize() {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);
    if (setsid() < 0) return false;

    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);

    if (chdir("/") < 0) return false;
    const int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return false;
    if (dup2(fd, STDIN_FILENO) < 0) return false;
    if (dup2(fd, STDOUT_FILENO) < 0) return false;
    if (dup2(fd, STDERR_FILENO) < 0) return false;
    if (fd > STDERR_FILENO) close(fd);
    return true;
}

} // namespace corpcron
