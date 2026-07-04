#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/common/logger.hpp"
#include <cppconn/datatype.h>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace corpcron {

namespace {

TaskMeta task_from_result(sql::ResultSet& res) {
    TaskMeta t;
    t.id = res.getString("id");
    t.cron_expr = res.getString("cron_expr");
    t.params = res.getString("params");
    t.handler = res.getString("handler");
    t.status = res.getInt("status");
    t.next_run_at = res.getString("next_run_at");
    t.last_run_at = res.getString("last_run_at");
    t.retry_count = res.getInt("retry_count");
    t.max_retries = res.getInt("max_retries");
    return t;
}

void set_datetime_or_null(sql::PreparedStatement& statement, unsigned int index, const std::string& value) {
    if (value.empty()) {
        statement.setNull(index, sql::DataType::SQLNULL);
    } else {
        statement.setString(index, value);
    }
}

std::string local_timezone_offset() {
    std::time_t now = std::time(nullptr);
    std::tm local_tm{};
    std::tm utc_tm{};
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &utc_tm);

    long offset_seconds = static_cast<long>(std::difftime(std::mktime(&local_tm), std::mktime(&utc_tm)));
    char sign = '+';
    if (offset_seconds < 0) {
        sign = '-';
        offset_seconds = -offset_seconds;
    }

    long hours = offset_seconds / 3600;
    long minutes = (offset_seconds % 3600) / 60;
    std::ostringstream ss;
    ss << sign << std::setw(2) << std::setfill('0') << hours
       << ":" << std::setw(2) << std::setfill('0') << minutes;
    return ss.str();
}

void log_sql_error(const std::string& operation, const sql::SQLException& e) {
    LOG_ERROR(operation + " error: " + e.what());
}

} // namespace

MySQLClient::MySQLClient(const std::string& host, int port,
                         const std::string& user, const std::string& password,
                         const std::string& database)
    : host_(host), port_(port), user_(user), password_(password), database_(database),
      driver_(nullptr), conn_(nullptr) {}

MySQLClient::~MySQLClient() {
    disconnect();
}

bool MySQLClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connectUnlocked();
}

bool MySQLClient::connectUnlocked() {
    try {
        if (conn_) {
            conn_->close();
            conn_.reset();
        }
        driver_ = sql::mysql::get_mysql_driver_instance();
        //driver_->connect() 返回的是裸指针 sql::Connection*，而 conn_ 是智能指针。reset() 让 unique_ptr 接管这个裸指针的生命周期
        conn_.reset(driver_->connect(host_ + ":" + std::to_string(port_), user_, password_));
        conn_->setSchema(database_);
        executeStatement("SET time_zone = '" + local_timezone_offset() + "'");
        return ensureSchema();
    } catch (sql::SQLException &e) {
        log_sql_error("MySQL connection", e);
        return false;
    }
}

void MySQLClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (conn_) {
        conn_->close();
        conn_.reset();
    }
}

bool MySQLClient::ensureConnected() {
    try {
        if (conn_ && !conn_->isClosed()) {
            return true;
        }
    } catch (sql::SQLException& e) {
        log_sql_error("MySQL connection health check", e);
    }

    LOG_WARN("MySQL connection is closed, reconnecting");
    return connectUnlocked();
}

bool MySQLClient::addTask(const TaskMeta& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "INSERT INTO tasks (id, cron_expr, params, handler, status, next_run_at, retry_count, max_retries) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, task.id);
        pstmt->setString(2, task.cron_expr);
        pstmt->setString(3, task.params);
        pstmt->setString(4, task.handler);
        pstmt->setInt(5, task.status);
        set_datetime_or_null(*pstmt, 6, task.next_run_at);
        pstmt->setInt(7, task.retry_count);
        pstmt->setInt(8, task.max_retries);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        log_sql_error("addTask", e);
        return false;
    }
}

bool MySQLClient::updateTaskDefinition(const TaskMeta& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET cron_expr=?, params=?, handler=?, status=?, next_run_at=?, last_run_at=?, retry_count=?, max_retries=? WHERE id=?"));
        pstmt->setString(1, task.cron_expr);
        pstmt->setString(2, task.params);
        pstmt->setString(3, task.handler);
        pstmt->setInt(4, task.status);
        set_datetime_or_null(*pstmt, 5, task.next_run_at);
        set_datetime_or_null(*pstmt, 6, task.last_run_at);
        pstmt->setInt(7, task.retry_count);
        pstmt->setInt(8, task.max_retries);
        pstmt->setString(9, task.id);
        return pstmt->executeUpdate() > 0;
    } catch (sql::SQLException &e) {
        log_sql_error("updateTaskDefinition", e);
        return false;
    }
}

bool MySQLClient::deleteTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "DELETE FROM tasks WHERE id=?"));
        pstmt->setString(1, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        log_sql_error("deleteTask", e);
        return false;
    }
}

bool MySQLClient::addHistory(const TaskHistory& history) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "INSERT INTO task_history (task_id, exec_node, success, result, error, start_time, end_time) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, history.task_id);
        pstmt->setString(2, history.exec_node);
        pstmt->setInt(3, history.success ? 1 : 0);
        pstmt->setString(4, history.result);
        pstmt->setString(5, history.error);
        set_datetime_or_null(*pstmt, 6, history.start_time);
        set_datetime_or_null(*pstmt, 7, history.end_time);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        log_sql_error("addHistory", e);
        return false;
    }
}

bool MySQLClient::getTask(const std::string& id, TaskMeta& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "SELECT id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries "
            "FROM tasks WHERE id=?"));
        pstmt->setString(1, id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) return false;
        task = task_from_result(*res);
        return true;
    } catch (sql::SQLException &e) {
        log_sql_error("getTask", e);
        return false;
    }
}

bool MySQLClient::getLatestHistory(const std::string& task_id, TaskHistory& history) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "SELECT task_id, exec_node, success, result, error, start_time, end_time "
            "FROM task_history WHERE task_id=? ORDER BY id DESC LIMIT 1"));
        pstmt->setString(1, task_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) return false;
        history.task_id = res->getString("task_id");
        history.exec_node = res->getString("exec_node");
        history.success = res->getInt("success") == 1;
        history.result = res->getString("result");
        history.error = res->getString("error");
        history.start_time = res->getString("start_time");
        history.end_time = res->getString("end_time");
        return true;
    } catch (sql::SQLException &e) {
        log_sql_error("getLatestHistory", e);
        return false;
    }
}

std::vector<TaskHistory> MySQLClient::getHistory(const std::string& task_id, size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskHistory> history_items;
    if (!ensureConnected()) return history_items;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt;
        if (task_id.empty()) {
            pstmt.reset(conn_->prepareStatement(
                "SELECT task_id, exec_node, success, result, error, start_time, end_time "
                "FROM task_history ORDER BY id DESC LIMIT ?"));
            pstmt->setUInt(1, static_cast<unsigned int>(limit));
        } else {
            pstmt.reset(conn_->prepareStatement(
                "SELECT task_id, exec_node, success, result, error, start_time, end_time "
                "FROM task_history WHERE task_id=? ORDER BY id DESC LIMIT ?"));
            pstmt->setString(1, task_id);
            pstmt->setUInt(2, static_cast<unsigned int>(limit));
        }
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            TaskHistory history;
            history.task_id = res->getString("task_id");
            history.exec_node = res->getString("exec_node");
            history.success = res->getInt("success") == 1;
            history.result = res->getString("result");
            history.error = res->getString("error");
            history.start_time = res->getString("start_time");
            history.end_time = res->getString("end_time");
            history_items.push_back(history);
        }
    } catch (sql::SQLException &e) {
        log_sql_error("getHistory", e);
    }
    return history_items;
}

std::vector<TaskMeta> MySQLClient::getAllTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskMeta> tasks;
    if (!ensureConnected()) return tasks;
    try {
        std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(
            "SELECT id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries FROM tasks"));
        while (res->next()) {
            tasks.push_back(task_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        log_sql_error("getAllTasks", e);
    }
    return tasks;
}

std::vector<TaskMeta> MySQLClient::getEnabledTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskMeta> tasks;
    if (!ensureConnected()) return tasks;
    try {
        std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(
            "SELECT id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries "
            "FROM tasks WHERE status=1"));
        while (res->next()) {
            tasks.push_back(task_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        log_sql_error("getEnabledTasks", e);
    }
    return tasks;
}

std::vector<TaskMeta> MySQLClient::getDueTasks(size_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskMeta> tasks;
    if (!ensureConnected()) return tasks;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "SELECT id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries "
            "FROM tasks WHERE status=1 AND (next_run_at IS NULL OR next_run_at <= NOW()) "
            "ORDER BY next_run_at ASC, created_at ASC LIMIT ?"));
        pstmt->setUInt(1, static_cast<unsigned int>(limit));
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            tasks.push_back(task_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        log_sql_error("getDueTasks", e);
    }
    return tasks;
}

bool MySQLClient::updateTaskSchedule(const std::string& id, const std::string& next_run_at,
                                     const std::string& last_run_at, int retry_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET next_run_at=?, last_run_at=?, retry_count=? WHERE id=?"));
        set_datetime_or_null(*pstmt, 1, next_run_at);
        set_datetime_or_null(*pstmt, 2, last_run_at);
        pstmt->setInt(3, retry_count);
        pstmt->setString(4, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        log_sql_error("updateTaskSchedule", e);
        return false;
    }
}

bool MySQLClient::updateTaskRuntime(const std::string& id, int status, const std::string& next_run_at,
                                    const std::string& last_run_at, int retry_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET status=?, next_run_at=?, last_run_at=?, retry_count=? WHERE id=?"));
        pstmt->setInt(1, status);
        set_datetime_or_null(*pstmt, 2, next_run_at);
        set_datetime_or_null(*pstmt, 3, last_run_at);
        pstmt->setInt(4, retry_count);
        pstmt->setString(5, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        log_sql_error("updateTaskRuntime", e);
        return false;
    }
}

bool MySQLClient::cancelTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "UPDATE tasks SET status=0 WHERE id=?"));
        pstmt->setString(1, id);
        if (pstmt->executeUpdate() > 0) return true;

        std::unique_ptr<sql::PreparedStatement> exists_stmt(conn_->prepareStatement(
            "SELECT COUNT(*) AS cnt FROM tasks WHERE id=?"));
        exists_stmt->setString(1, id);
        std::unique_ptr<sql::ResultSet> res(exists_stmt->executeQuery());
        return res->next() && res->getInt("cnt") > 0;
    } catch (sql::SQLException &e) {
        log_sql_error("cancelTask", e);
        return false;
    }
}

int MySQLClient::historyCount(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureConnected()) return 0;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn_->prepareStatement(
            "SELECT COUNT(*) AS cnt FROM task_history WHERE task_id=?"));
        pstmt->setString(1, task_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (res->next()) return res->getInt("cnt");
    } catch (sql::SQLException &e) {
        log_sql_error("historyCount", e);
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
        log_sql_error("ensureSchema", e);
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
