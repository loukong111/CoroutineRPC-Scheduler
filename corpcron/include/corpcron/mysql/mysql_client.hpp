#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

namespace corpcron {

struct TaskMeta {
    std::string id;
    std::string cron_expr;
    std::string params;
    std::string handler;
    int status;
    std::string next_run_at;
    std::string last_run_at;
    int retry_count = 0;
    int max_retries = 3;
};

struct TaskHistory {
    std::string task_id;
    std::string exec_node;
    bool success = false;
    std::string result;
    std::string error;
    std::string start_time;
    std::string end_time;
};

class MySQLClient {
public:
    MySQLClient(const std::string& host, int port,
                const std::string& user, const std::string& password,
                const std::string& database);
    ~MySQLClient();

    bool connect();
    void disconnect();

    bool addTask(const TaskMeta& task);
    bool updateTask(const TaskMeta& task);
    bool deleteTask(const std::string& id);
    bool addHistory(const TaskHistory& history);
    std::vector<TaskMeta> getAllTasks();
    std::vector<TaskMeta> getEnabledTasks();
    std::vector<TaskMeta> getDueTasks(size_t limit);
    bool updateTaskSchedule(const std::string& id, const std::string& next_run_at,
                            const std::string& last_run_at, int retry_count);
    bool updateTaskRuntime(const std::string& id, int status, const std::string& next_run_at,
                           const std::string& last_run_at, int retry_count);
    bool cancelTask(const std::string& id);
    int historyCount(const std::string& task_id);

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::string database_;
    sql::mysql::MySQL_Driver* driver_;
    std::unique_ptr<sql::Connection> conn_;
    std::mutex mutex_;

    bool ensureSchema();
    bool columnExists(const std::string& table, const std::string& column);
    bool executeStatement(const std::string& sql);
};

} // namespace corpcron
