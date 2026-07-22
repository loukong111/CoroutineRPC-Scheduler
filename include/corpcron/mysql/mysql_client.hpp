#pragma once
#include "corpcron/common/storage_error.hpp"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

namespace corpcron {

struct MySQLClientOptions {
    size_t pool_size = 1;
    int connect_timeout_sec = 3;
    int read_timeout_sec = 5;
    int write_timeout_sec = 5;
    bool reconnect = true;
};

enum TaskStatus {
    TASK_DISABLED = 0,
    TASK_SCHEDULED = 1,
    TASK_RUNNING = 2,
};

struct TaskMeta {
    std::string id;
    std::string cron_expr;
    std::string params;
    std::string handler;
    int status = TASK_DISABLED;
    std::string next_run_at;
    std::string last_run_at;
    int retry_count = 0;
    int max_retries = 3;
    std::string current_execution_id;
    std::string running_node;
    std::string started_at;
};

struct TaskHistory {
    std::string execution_id;
    std::string task_id;
    std::string exec_node;
    bool success = false;
    std::string result;
    std::string error;
    std::string start_time;
    std::string end_time;
};

struct TaskQuery {
    int status_filter = -1;
    std::string keyword;
    size_t limit = 100;
    size_t offset = 0;
};

struct TaskPage {
    std::vector<TaskMeta> items;
    size_t total = 0;
};

struct HistoryQuery {
    std::string task_id;
    int success_filter = -1;
    std::string keyword;
    size_t limit = 100;
    size_t offset = 0;
};

struct HistoryPage {
    std::vector<TaskHistory> items;
    size_t total = 0;
};

class MySQLClient {
public:
    MySQLClient(const std::string& host, int port,
                const std::string& user, const std::string& password,
                const std::string& database);
    MySQLClient(const std::string& host, int port,
                const std::string& user, const std::string& password,
                const std::string& database, MySQLClientOptions options);
    ~MySQLClient();

    bool connect();
    void disconnect();
    StorageError lastError() const;

    bool addTask(const TaskMeta& task);
    bool updateTaskDefinition(const TaskMeta& task);
    bool deleteTask(const std::string& id);
    bool addHistory(const TaskHistory& history);
    bool getTask(const std::string& id, TaskMeta& task);
    bool getLatestHistory(const std::string& task_id, TaskHistory& history);
    TaskPage listTasks(const TaskQuery& query);
    HistoryPage listHistory(const HistoryQuery& query);
    std::vector<TaskHistory> getHistory(const std::string& task_id, size_t limit);
    std::vector<TaskMeta> getAllTasks();
    std::vector<TaskMeta> getEnabledTasks();
    std::vector<TaskMeta> getDueTasks(size_t limit);
    std::vector<TaskMeta> getStaleRunningTasks(int stale_after_sec, size_t limit);
    bool claimTaskExecution(const std::string& id, const std::string& execution_id,
                            const std::string& node_id,
                            int expected_status = TASK_SCHEDULED);
    bool recoverTaskExecution(const std::string& id, const std::string& execution_id);
    bool completeTaskExecution(const std::string& id, const std::string& execution_id,
                               int status, const std::string& next_run_at,
                               const std::string& last_run_at, int retry_count);
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
    MySQLClientOptions options_;
    sql::mysql::MySQL_Driver* driver_;
    struct ConnectionSlot;
    struct ConnectionLease;
    std::vector<std::unique_ptr<ConnectionSlot>> slots_;
    std::atomic<size_t> next_slot_{0};
    std::mutex lifecycle_mutex_;
    mutable std::mutex error_mutex_;
    mutable StorageError last_error_;

    bool connectUnlocked();
    bool connectSlotLocked(ConnectionSlot& slot);
    ConnectionLease acquireConnection();
    bool ensureConnected(ConnectionSlot& slot);
    bool ensureSchema(sql::Connection& conn);
    bool columnExists(sql::Connection& conn, const std::string& table, const std::string& column);
    bool indexExists(sql::Connection& conn, const std::string& table, const std::string& index);
    bool executeStatement(sql::Connection& conn, const std::string& sql);
    void setLastError(StorageErrorKind kind, int code, const std::string& message) const;
    void recordSqlError(const std::string& operation, const sql::SQLException& e) const;
};

} // namespace corpcron
