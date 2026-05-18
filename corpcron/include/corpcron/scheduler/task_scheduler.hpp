#pragma once
#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/redis/redis_client.hpp"
#include "corpcron/common/thread_pool.hpp"
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <random> 

namespace corpcron {

class TaskScheduler {
public:
    TaskScheduler(std::shared_ptr<MySQLClient> db, std::shared_ptr<RedisClient> redis,
                  const std::string& node_id);
    ~TaskScheduler();

    void start();
    void stop();

private:
    void schedulerLoop();
    void scanAndDispatch();
    void executeTask(const TaskMeta& task);

    std::shared_ptr<MySQLClient> db_;
    std::shared_ptr<RedisClient> redis_;
    std::string node_id_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> thread_;
    std::unique_ptr<DynamicThreadPool> thread_pool_;
    std::unordered_map<std::string, uint64_t> last_fire_ms_;
    std::mutex last_fire_mutex_;
    std::vector<std::string> nodes_;
    std::mt19937 rng_;
};

} // namespace corpcron