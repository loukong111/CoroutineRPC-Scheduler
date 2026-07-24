#pragma once
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/common/thread_pool.hpp"
#include "corpcron/common/cancellation.hpp"
#include "corpcron/rpc/rpc_client_pool.hpp"
#include <atomic>
#include <condition_variable>
#include <thread>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace corpcron {

class TaskScheduler {
public:
    TaskScheduler(std::shared_ptr<MySQLClient> db, std::shared_ptr<RedisClient> redis,
                  const std::string& node_id, std::string service_name = "rpc");
    ~TaskScheduler();

    void start();
    void stop();

private:
    struct ExecutionOutcome {
        bool success = false;
        std::string result;
        std::string error;
        std::string endpoint;
    };

    class LockRenewer {
    public:
        explicit LockRenewer(std::shared_ptr<RedisClient> redis);
        ~LockRenewer();

        void start();
        void stop();
        void add(const std::string& key, const std::string& value, int ttl_sec,
                 CancellationSource cancellation);
        void remove(const std::string& key, const std::string& value);

    private:
        struct Lease {
            std::string key;
            std::string value;
            int ttl_sec;
            std::chrono::steady_clock::time_point next_renew_at;
            CancellationSource cancellation;
        };

        void loop();
        std::chrono::steady_clock::time_point nextRenewTime(int ttl_sec) const;

        std::shared_ptr<RedisClient> redis_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::unordered_map<std::string, Lease> leases_;
        std::thread thread_;
        bool running_ = false;
    };

    void schedulerLoop();
    void scanAndDispatch();
    void recoverStaleExecutions();
    ExecutionOutcome executeTask(const TaskMeta& task, const std::string& execution_id,
                                 const CancellationToken& cancellation);

    std::shared_ptr<MySQLClient> db_;
    std::shared_ptr<RedisClient> redis_;
    std::string node_id_;
    std::string service_name_;
    std::atomic<bool> running_;
    std::mutex loop_mutex_;
    std::condition_variable loop_cv_;
    std::unique_ptr<std::thread> thread_;
    std::unique_ptr<DynamicThreadPool> thread_pool_;
    std::unique_ptr<LockRenewer> lock_renewer_;
    std::unique_ptr<RpcClientPool> rpc_client_pool_;
    CancellationSource shutdown_cancellation_;
};

} // namespace corpcron
