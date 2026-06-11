#include "corpcron/mysql/mysql_client.hpp"
#include <iostream>
#include <sstream>

namespace corpcron {

MySQLClient::MySQLClient(const std::string& host, int port,
                         const std::string& user, const std::string& password,
                         const std::string& database)
    : host_(host), port_(port), user_(user), password_(password), database_(database),
      driver_(nullptr), conn_(nullptr) {}

MySQLClient::~MySQLClient() {
    disconnect();
}

bool MySQLClient::connect() {
    try {
        driver_ = sql::mysql::get_mysql_driver_instance();
        //driver_->connect() 返回的是裸指针 sql::Connection*，而 conn_ 是智能指针。reset() 让 unique_ptr 接管这个裸指针的生命周期
        conn_.reset(driver_->connect(host_ + ":" + std::to_string(port_), user_, password_));
        conn_->setSchema(database_);
        return ensureSchema();
    } catch (sql::SQLException &e) {
        std::cerr << "MySQL connection error: " << e.what() << std::endl;
        return false;
    }
}

void MySQLClient::disconnect() {
    if (conn_) {
        conn_->close();
        conn_.reset();
    }
}

bool MySQLClient::addTask(const TaskMeta& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "INSERT INTO tasks (id, cron_expr, params, handler, status, next_run_at, retry_count, max_retries) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, task.id);
        pstmt->setString(2, task.cron_expr);
        pstmt->setString(3, task.params);
        pstmt->setString(4, task.handler);
        pstmt->setInt(5, task.status);
        pstmt->setString(6, task.next_run_at);
        pstmt->setInt(7, task.retry_count);
        pstmt->setInt(8, task.max_retries);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        std::cerr << "addTask error: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLClient::updateTask(const TaskMeta& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET cron_expr=?, params=?, handler=?, status=? WHERE id=?"));
        pstmt->setString(1, task.cron_expr);
        pstmt->setString(2, task.params);
        pstmt->setString(3, task.handler);
        pstmt->setInt(4, task.status);
        pstmt->setString(5, task.id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        std::cerr << "updateTask error: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLClient::deleteTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "DELETE FROM tasks WHERE id=?"));
        pstmt->setString(1, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        std::cerr << "deleteTask error: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLClient::addHistory(const TaskHistory& history) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "INSERT INTO task_history (task_id, exec_node, success, result, error, start_time, end_time) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, history.task_id);
        pstmt->setString(2, history.exec_node);
        pstmt->setInt(3, history.success ? 1 : 0);
        pstmt->setString(4, history.result);
        pstmt->setString(5, history.error);
        pstmt->setString(6, history.start_time);
        pstmt->setString(7, history.end_time);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        std::cerr << "addHistory error: " << e.what() << std::endl;
        return false;
    }
}

std::vector<TaskMeta> MySQLClient::getAllTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskMeta> tasks;
    try {
        std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(
            "SELECT id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries FROM tasks"));
        while (res->next()) {
            TaskMeta t;
            t.id = res->getString("id");
            t.cron_expr = res->getString("cron_expr");
            t.params = res->getString("params");
            t.handler = res->getString("handler");
            t.status = res->getInt("status");
            t.next_run_at = res->getString("next_run_at");
            t.last_run_at = res->getString("last_run_at");
            t.retry_count = res->getInt("retry_count");
            t.max_retries = res->getInt("max_retries");
            tasks.push_back(t);
        }
    } catch (sql::SQLException &e) {
        std::cerr << "getAllTasks error: " << e.what() << std::endl;
    }
    return tasks;
}

std::vector<TaskMeta> MySQLClient::getEnabledTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskMeta> tasks;
    try {
        std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(
            "SELECT id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries "
            "FROM tasks WHERE status=1"));
        while (res->next()) {
            TaskMeta t;
            t.id = res->getString("id");
            t.cron_expr = res->getString("cron_expr");
            t.params = res->getString("params");
            t.handler = res->getString("handler");
            t.status = res->getInt("status");
            t.next_run_at = res->getString("next_run_at");
            t.last_run_at = res->getString("last_run_at");
            t.retry_count = res->getInt("retry_count");
            t.max_retries = res->getInt("max_retries");
            tasks.push_back(t);
        }
    } catch (sql::SQLException &e) {
        std::cerr << "getEnabledTasks error: " << e.what() << std::endl;
    }
    return tasks;
}

std::vector<TaskMeta> MySQLClient::getDueTasks(size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskMeta> tasks;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "SELECT id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries "
            "FROM tasks WHERE status=1 AND (next_run_at IS NULL OR next_run_at <= NOW()) "
            "ORDER BY next_run_at ASC, created_at ASC LIMIT ?"));
        pstmt->setUInt(1, static_cast<unsigned int>(limit));
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            TaskMeta t;
            t.id = res->getString("id");
            t.cron_expr = res->getString("cron_expr");
            t.params = res->getString("params");
            t.handler = res->getString("handler");
            t.status = res->getInt("status");
            t.next_run_at = res->getString("next_run_at");
            t.last_run_at = res->getString("last_run_at");
            t.retry_count = res->getInt("retry_count");
            t.max_retries = res->getInt("max_retries");
            tasks.push_back(t);
        }
    } catch (sql::SQLException &e) {
        std::cerr << "getDueTasks error: " << e.what() << std::endl;
    }
    return tasks;
}

bool MySQLClient::updateTaskSchedule(const std::string& id, const std::string& next_run_at,
                                     const std::string& last_run_at, int retry_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET next_run_at=?, last_run_at=?, retry_count=? WHERE id=?"));
        pstmt->setString(1, next_run_at);
        pstmt->setString(2, last_run_at);
        pstmt->setInt(3, retry_count);
        pstmt->setString(4, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        std::cerr << "updateTaskSchedule error: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLClient::updateTaskRuntime(const std::string& id, int status, const std::string& next_run_at,
                                    const std::string& last_run_at, int retry_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET status=?, next_run_at=?, last_run_at=?, retry_count=? WHERE id=?"));
        pstmt->setInt(1, status);
        pstmt->setString(2, next_run_at);
        pstmt->setString(3, last_run_at);
        pstmt->setInt(4, retry_count);
        pstmt->setString(5, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        std::cerr << "updateTaskRuntime error: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLClient::cancelTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET status=0 WHERE id=?"));
        pstmt->setString(1, id);
        pstmt->execute();
        return pstmt->getUpdateCount() > 0;
    } catch (sql::SQLException &e) {
        std::cerr << "cancelTask error: " << e.what() << std::endl;
        return false;
    }
}

int MySQLClient::historyCount(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "SELECT COUNT(*) AS cnt FROM task_history WHERE task_id=?"));
        pstmt->setString(1, task_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (res->next()) return res->getInt("cnt");
    } catch (sql::SQLException &e) {
        std::cerr << "historyCount error: " << e.what() << std::endl;
    }
    return 0;
}

bool MySQLClient::ensureSchema() {
    try {
        executeStatement(
            "CREATE TABLE IF NOT EXISTS tasks ("
            "id VARCHAR(64) PRIMARY KEY,"
            "cron_expr VARCHAR(100) NOT NULL,"
            "params TEXT,"
            "handler VARCHAR(100) NOT NULL,"
            "status TINYINT NOT NULL DEFAULT 1,"
            "next_run_at DATETIME NULL,"
            "last_run_at DATETIME NULL,"
            "retry_count INT NOT NULL DEFAULT 0,"
            "max_retries INT NOT NULL DEFAULT 3,"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "INDEX idx_tasks_status_next_run (status, next_run_at)"
            ")");
        executeStatement(
            "CREATE TABLE IF NOT EXISTS task_history ("
            "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            "task_id VARCHAR(64) NOT NULL,"
            "exec_node VARCHAR(128),"
            "success TINYINT NOT NULL DEFAULT 0,"
            "result TEXT,"
            "error TEXT,"
            "start_time DATETIME,"
            "end_time DATETIME,"
            "INDEX idx_task_history_task_id (task_id),"
            "INDEX idx_task_history_start_time (start_time)"
            ")");

        if (!columnExists("tasks", "next_run_at")) executeStatement("ALTER TABLE tasks ADD COLUMN next_run_at DATETIME NULL");
        if (!columnExists("tasks", "last_run_at")) executeStatement("ALTER TABLE tasks ADD COLUMN last_run_at DATETIME NULL");
        if (!columnExists("tasks", "retry_count")) executeStatement("ALTER TABLE tasks ADD COLUMN retry_count INT NOT NULL DEFAULT 0");
        if (!columnExists("tasks", "max_retries")) executeStatement("ALTER TABLE tasks ADD COLUMN max_retries INT NOT NULL DEFAULT 3");
        if (!columnExists("task_history", "success")) executeStatement("ALTER TABLE task_history ADD COLUMN success TINYINT NOT NULL DEFAULT 0 AFTER exec_node");
        return true;
    } catch (sql::SQLException &e) {
        std::cerr << "ensureSchema error: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLClient::columnExists(const std::string& table, const std::string& column) {
    std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
        "SELECT COUNT(*) AS cnt FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA=? AND TABLE_NAME=? AND COLUMN_NAME=?"));
    pstmt->setString(1, database_);
    pstmt->setString(2, table);
    pstmt->setString(3, column);
    std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
    return res->next() && res->getInt("cnt") > 0;
}

bool MySQLClient::executeStatement(const std::string& sql_text) {
    std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
    stmt->execute(sql_text);
    return true;
}

} // namespace corpcron
