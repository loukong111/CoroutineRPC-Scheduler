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
};

struct TaskHistory {
    std::string task_id;
    std::string exec_node;
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

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::string database_;
    sql::mysql::MySQL_Driver* driver_;
    std::unique_ptr<sql::Connection> conn_;
    std::mutex mutex_;
};

} // namespace corpcron