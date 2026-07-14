#include "corpcron/mysql/mysql_client.hpp"
#include "corpcron/common/logger.hpp"
#include <algorithm>
#include <cctype>
#include <cppconn/datatype.h>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace corpcron {

struct MySQLClient::ConnectionSlot {
    std::unique_ptr<sql::Connection> conn;
    std::mutex mutex;
};

struct MySQLClient::ConnectionLease {
    ConnectionSlot* slot = nullptr;
    std::unique_lock<std::mutex> lock;
    sql::Connection* conn = nullptr;

    bool valid() const {
        return conn != nullptr;
    }
};

namespace {

constexpr const char* kTaskSelectFields =
    "id, cron_expr, params, handler, status, next_run_at, last_run_at, retry_count, max_retries, "
    "current_execution_id, running_node, started_at";

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
    t.current_execution_id = res.getString("current_execution_id");
    t.running_node = res.getString("running_node");
    t.started_at = res.getString("started_at");
    return t;
}

TaskHistory history_from_result(sql::ResultSet& res) {
    TaskHistory history;
    history.execution_id = res.getString("execution_id");
    history.task_id = res.getString("task_id");
    history.exec_node = res.getString("exec_node");
    history.success = res.getInt("success") == 1;
    history.result = res.getString("result");
    history.error = res.getString("error");
    history.start_time = res.getString("start_time");
    history.end_time = res.getString("end_time");
    return history;
}

void set_string_or_null(sql::PreparedStatement& statement, unsigned int index, const std::string& value) {
    if (value.empty()) {
        statement.setNull(index, sql::DataType::SQLNULL);
    } else {
        statement.setString(index, value);
    }
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

StorageErrorKind classify_sql_error(const sql::SQLException& e) {
    const int code = e.getErrorCode();
    std::string state = e.getSQLState();
    if (code == 1045 || state == "28000") return StorageErrorKind::Authentication;
    if (code == 1062 || state == "23000") return StorageErrorKind::DuplicateKey;
    if (code == 2002 || code == 2003 || code == 2006 || code == 2013 ||
        state.rfind("08", 0) == 0) {
        return StorageErrorKind::Connection;
    }
    std::string message = e.what();
    std::transform(message.begin(), message.end(), message.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (message.find("timeout") != std::string::npos ||
        message.find("timed out") != std::string::npos) {
        return StorageErrorKind::Timeout;
    }
    return StorageErrorKind::Query;
}

} // namespace

MySQLClient::MySQLClient(const std::string& host, int port,
                         const std::string& user, const std::string& password,
                         const std::string& database)
    : MySQLClient(host, port, user, password, database, MySQLClientOptions{}) {}

MySQLClient::MySQLClient(const std::string& host, int port,
                         const std::string& user, const std::string& password,
                         const std::string& database, MySQLClientOptions options)
    : host_(host), port_(port), user_(user), password_(password), database_(database),
      options_(options),
      driver_(nullptr) {
    if (options_.pool_size == 0) options_.pool_size = 1;
}

MySQLClient::~MySQLClient() {
    disconnect();
}

bool MySQLClient::connect() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return connectUnlocked();
}

bool MySQLClient::connectUnlocked() {
    try {
        slots_.clear();
        slots_.reserve(options_.pool_size);
        driver_ = sql::mysql::get_mysql_driver_instance();

        for (size_t i = 0; i < options_.pool_size; ++i) {
            auto slot = std::make_unique<ConnectionSlot>();
            std::lock_guard<std::mutex> slot_lock(slot->mutex);
            if (!connectSlotLocked(*slot)) return false;
            slots_.push_back(std::move(slot));
        }

        if (!slots_.empty() && slots_[0]->conn) {
            bool ok = ensureSchema(*slots_[0]->conn);
            if (ok) setLastError(StorageErrorKind::None, 0, "");
            return ok;
        }
        return false;
    } catch (sql::SQLException &e) {
        recordSqlError("MySQL connection", e);
        return false;
    }
}

bool MySQLClient::connectSlotLocked(ConnectionSlot& slot) {
    try {
        if (slot.conn) {
            slot.conn->close();
            slot.conn.reset();
        }
        sql::ConnectOptionsMap options;
        options["hostName"] = host_;
        options["port"] = port_;
        options["userName"] = user_;
        options["password"] = password_;
        options["schema"] = database_;
        options["OPT_CONNECT_TIMEOUT"] = options_.connect_timeout_sec;
        options["OPT_READ_TIMEOUT"] = options_.read_timeout_sec;
        options["OPT_WRITE_TIMEOUT"] = options_.write_timeout_sec;
        options["OPT_RECONNECT"] = options_.reconnect;
        slot.conn.reset(driver_->connect(options));
        executeStatement(*slot.conn, "SET time_zone = '" + local_timezone_offset() + "'");
        setLastError(StorageErrorKind::None, 0, "");
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("MySQL connection", e);
        return false;
    }
}

void MySQLClient::disconnect() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    for (auto& slot : slots_) {
        std::lock_guard<std::mutex> slot_lock(slot->mutex);
        if (slot->conn) {
            slot->conn->close();
            slot->conn.reset();
        }
    }
}

StorageError MySQLClient::lastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void MySQLClient::setLastError(StorageErrorKind kind, int code, const std::string& message) const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = StorageError{kind, code, message};
}

void MySQLClient::recordSqlError(const std::string& operation, const sql::SQLException& e) const {
    setLastError(classify_sql_error(e), e.getErrorCode(), e.what());
    StorageError error = lastError();
    LOG_ERROR(operation + " error kind=" + storageErrorKindName(error.kind) +
              " code=" + std::to_string(error.code) + " message=" + e.what());
}

MySQLClient::ConnectionLease MySQLClient::acquireConnection() {
    if (slots_.empty()) {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (slots_.empty() && !connectUnlocked()) {
            return {};
        }
    }
    const size_t index = next_slot_.fetch_add(1, std::memory_order_relaxed) % slots_.size();
    ConnectionSlot* slot = slots_[index].get();
    ConnectionLease lease;
    lease.slot = slot;
    lease.lock = std::unique_lock<std::mutex>(slot->mutex);
    if (!ensureConnected(*slot)) return lease;
    lease.conn = slot->conn.get();
    return lease;
}

bool MySQLClient::ensureConnected(ConnectionSlot& slot) {
    try {
        if (slot.conn && !slot.conn->isClosed()) {
            return true;
        }
    } catch (sql::SQLException& e) {
        recordSqlError("MySQL connection health check", e);
    }

    LOG_WARN("MySQL connection is closed, reconnecting");
    return connectSlotLocked(slot);
}

bool MySQLClient::addTask(const TaskMeta& task) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
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
        recordSqlError("addTask", e);
        return false;
    }
}

bool MySQLClient::updateTaskDefinition(const TaskMeta& task) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
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
        recordSqlError("updateTaskDefinition", e);
        return false;
    }
}

bool MySQLClient::deleteTask(const std::string& id) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "DELETE FROM tasks WHERE id=?"));
        pstmt->setString(1, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("deleteTask", e);
        return false;
    }
}

bool MySQLClient::addHistory(const TaskHistory& history) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "INSERT INTO task_history (execution_id, task_id, exec_node, success, result, error, start_time, end_time) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
            "ON DUPLICATE KEY UPDATE exec_node=VALUES(exec_node), success=VALUES(success), "
            "result=VALUES(result), error=VALUES(error), start_time=VALUES(start_time), end_time=VALUES(end_time)"));
        set_string_or_null(*pstmt, 1, history.execution_id);
        pstmt->setString(2, history.task_id);
        pstmt->setString(3, history.exec_node);
        pstmt->setInt(4, history.success ? 1 : 0);
        pstmt->setString(5, history.result);
        pstmt->setString(6, history.error);
        set_datetime_or_null(*pstmt, 7, history.start_time);
        set_datetime_or_null(*pstmt, 8, history.end_time);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("addHistory", e);
        return false;
    }
}

bool MySQLClient::getTask(const std::string& id, TaskMeta& task) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            std::string("SELECT ") + kTaskSelectFields + " FROM tasks WHERE id=?"));
        pstmt->setString(1, id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) return false;
        task = task_from_result(*res);
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("getTask", e);
        return false;
    }
}

bool MySQLClient::getLatestHistory(const std::string& task_id, TaskHistory& history) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "SELECT execution_id, task_id, exec_node, success, result, error, start_time, end_time "
            "FROM task_history WHERE task_id=? ORDER BY id DESC LIMIT 1"));
        pstmt->setString(1, task_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) return false;
        history = history_from_result(*res);
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("getLatestHistory", e);
        return false;
    }
}

TaskPage MySQLClient::listTasks(const TaskQuery& query) {
    auto lease = acquireConnection();
    TaskPage page;
    if (!lease.valid()) return page;

    const int status_filter = query.status_filter;
    const std::string keyword = query.keyword;
    const std::string like_keyword = "%" + keyword + "%";
    const size_t limit = query.limit == 0 ? 100 : query.limit;

    try {
        const std::string where =
            " WHERE (? = -1 OR status = ?) "
            "AND (? = '' OR id LIKE ? OR handler LIKE ? OR cron_expr LIKE ? OR params LIKE ?)";

        std::unique_ptr<sql::PreparedStatement> count_stmt(lease.conn->prepareStatement(
            "SELECT COUNT(*) AS cnt FROM tasks" + where));
        count_stmt->setInt(1, status_filter);
        count_stmt->setInt(2, status_filter);
        count_stmt->setString(3, keyword);
        count_stmt->setString(4, like_keyword);
        count_stmt->setString(5, like_keyword);
        count_stmt->setString(6, like_keyword);
        count_stmt->setString(7, like_keyword);
        std::unique_ptr<sql::ResultSet> count_res(count_stmt->executeQuery());
        if (count_res->next()) page.total = static_cast<size_t>(count_res->getUInt64("cnt"));

        std::unique_ptr<sql::PreparedStatement> list_stmt(lease.conn->prepareStatement(
            std::string("SELECT ") + kTaskSelectFields + " FROM tasks" + where +
            " ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?"));
        list_stmt->setInt(1, status_filter);
        list_stmt->setInt(2, status_filter);
        list_stmt->setString(3, keyword);
        list_stmt->setString(4, like_keyword);
        list_stmt->setString(5, like_keyword);
        list_stmt->setString(6, like_keyword);
        list_stmt->setString(7, like_keyword);
        list_stmt->setUInt(8, static_cast<unsigned int>(limit));
        list_stmt->setUInt(9, static_cast<unsigned int>(query.offset));

        std::unique_ptr<sql::ResultSet> res(list_stmt->executeQuery());
        while (res->next()) {
            page.items.push_back(task_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        recordSqlError("listTasks", e);
    }
    return page;
}

HistoryPage MySQLClient::listHistory(const HistoryQuery& query) {
    auto lease = acquireConnection();
    HistoryPage page;
    if (!lease.valid()) return page;

    const int success_filter = query.success_filter;
    const std::string keyword = query.keyword;
    const std::string like_keyword = "%" + keyword + "%";
    const size_t limit = query.limit == 0 ? 100 : query.limit;

    try {
        const std::string where =
            " WHERE (? = '' OR task_id = ?) "
            "AND (? = -1 OR success = ?) "
            "AND (? = '' OR execution_id LIKE ? OR task_id LIKE ? OR exec_node LIKE ? "
            "OR result LIKE ? OR error LIKE ?)";

        std::unique_ptr<sql::PreparedStatement> count_stmt(lease.conn->prepareStatement(
            "SELECT COUNT(*) AS cnt FROM task_history" + where));
        count_stmt->setString(1, query.task_id);
        count_stmt->setString(2, query.task_id);
        count_stmt->setInt(3, success_filter);
        count_stmt->setInt(4, success_filter);
        count_stmt->setString(5, keyword);
        count_stmt->setString(6, like_keyword);
        count_stmt->setString(7, like_keyword);
        count_stmt->setString(8, like_keyword);
        count_stmt->setString(9, like_keyword);
        count_stmt->setString(10, like_keyword);
        std::unique_ptr<sql::ResultSet> count_res(count_stmt->executeQuery());
        if (count_res->next()) page.total = static_cast<size_t>(count_res->getUInt64("cnt"));

        std::unique_ptr<sql::PreparedStatement> list_stmt(lease.conn->prepareStatement(
            "SELECT execution_id, task_id, exec_node, success, result, error, start_time, end_time "
            "FROM task_history" + where + " ORDER BY id DESC LIMIT ? OFFSET ?"));
        list_stmt->setString(1, query.task_id);
        list_stmt->setString(2, query.task_id);
        list_stmt->setInt(3, success_filter);
        list_stmt->setInt(4, success_filter);
        list_stmt->setString(5, keyword);
        list_stmt->setString(6, like_keyword);
        list_stmt->setString(7, like_keyword);
        list_stmt->setString(8, like_keyword);
        list_stmt->setString(9, like_keyword);
        list_stmt->setString(10, like_keyword);
        list_stmt->setUInt(11, static_cast<unsigned int>(limit));
        list_stmt->setUInt(12, static_cast<unsigned int>(query.offset));

        std::unique_ptr<sql::ResultSet> res(list_stmt->executeQuery());
        while (res->next()) {
            page.items.push_back(history_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        recordSqlError("listHistory", e);
    }
    return page;
}

std::vector<TaskHistory> MySQLClient::getHistory(const std::string& task_id, size_t limit) {
    auto lease = acquireConnection();
    std::vector<TaskHistory> history_items;
    if (!lease.valid()) return history_items;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt;
        if (task_id.empty()) {
            pstmt.reset(lease.conn->prepareStatement(
                "SELECT execution_id, task_id, exec_node, success, result, error, start_time, end_time "
                "FROM task_history ORDER BY id DESC LIMIT ?"));
            pstmt->setUInt(1, static_cast<unsigned int>(limit));
        } else {
            pstmt.reset(lease.conn->prepareStatement(
                "SELECT execution_id, task_id, exec_node, success, result, error, start_time, end_time "
                "FROM task_history WHERE task_id=? ORDER BY id DESC LIMIT ?"));
            pstmt->setString(1, task_id);
            pstmt->setUInt(2, static_cast<unsigned int>(limit));
        }
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            history_items.push_back(history_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        recordSqlError("getHistory", e);
    }
    return history_items;
}

std::vector<TaskMeta> MySQLClient::getAllTasks() {
    auto lease = acquireConnection();
    std::vector<TaskMeta> tasks;
    if (!lease.valid()) return tasks;
    try {
        std::unique_ptr<sql::Statement> stmt(lease.conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(
            std::string("SELECT ") + kTaskSelectFields + " FROM tasks"));
        while (res->next()) {
            tasks.push_back(task_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        recordSqlError("getAllTasks", e);
    }
    return tasks;
}

std::vector<TaskMeta> MySQLClient::getEnabledTasks() {
    auto lease = acquireConnection();
    std::vector<TaskMeta> tasks;
    if (!lease.valid()) return tasks;
    try {
        std::unique_ptr<sql::Statement> stmt(lease.conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(
            std::string("SELECT ") + kTaskSelectFields + " FROM tasks WHERE status=1"));
        while (res->next()) {
            tasks.push_back(task_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        recordSqlError("getEnabledTasks", e);
    }
    return tasks;
}

std::vector<TaskMeta> MySQLClient::getDueTasks(size_t limit) {
    auto lease = acquireConnection();
    std::vector<TaskMeta> tasks;
    if (!lease.valid()) return tasks;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            std::string("SELECT ") + kTaskSelectFields +
            " FROM tasks WHERE status=1 AND (next_run_at IS NULL OR next_run_at <= NOW()) "
            "ORDER BY next_run_at ASC, created_at ASC LIMIT ?"));
        pstmt->setUInt(1, static_cast<unsigned int>(limit));
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            tasks.push_back(task_from_result(*res));
        }
    } catch (sql::SQLException &e) {
        recordSqlError("getDueTasks", e);
    }
    return tasks;
}

bool MySQLClient::claimTaskExecution(const std::string& id, const std::string& execution_id,
                                     const std::string& node_id) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "UPDATE tasks SET status=?, current_execution_id=?, running_node=?, started_at=NOW() "
            "WHERE id=? AND status=?"));
        pstmt->setInt(1, TASK_RUNNING);
        pstmt->setString(2, execution_id);
        pstmt->setString(3, node_id);
        pstmt->setString(4, id);
        pstmt->setInt(5, TASK_SCHEDULED);
        return pstmt->executeUpdate() > 0;
    } catch (sql::SQLException &e) {
        recordSqlError("claimTaskExecution", e);
        return false;
    }
}

bool MySQLClient::completeTaskExecution(const std::string& id, const std::string& execution_id,
                                        int status, const std::string& next_run_at,
                                        const std::string& last_run_at, int retry_count) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "UPDATE tasks SET status=?, next_run_at=?, last_run_at=?, retry_count=?, "
            "current_execution_id=NULL, running_node=NULL, started_at=NULL "
            "WHERE id=? AND current_execution_id=? AND status=?"));
        pstmt->setInt(1, status);
        set_datetime_or_null(*pstmt, 2, next_run_at);
        set_datetime_or_null(*pstmt, 3, last_run_at);
        pstmt->setInt(4, retry_count);
        pstmt->setString(5, id);
        pstmt->setString(6, execution_id);
        pstmt->setInt(7, TASK_RUNNING);
        return pstmt->executeUpdate() > 0;
    } catch (sql::SQLException &e) {
        recordSqlError("completeTaskExecution", e);
        return false;
    }
}

bool MySQLClient::updateTaskSchedule(const std::string& id, const std::string& next_run_at,
                                     const std::string& last_run_at, int retry_count) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "UPDATE tasks SET next_run_at=?, last_run_at=?, retry_count=? WHERE id=?"));
        set_datetime_or_null(*pstmt, 1, next_run_at);
        set_datetime_or_null(*pstmt, 2, last_run_at);
        pstmt->setInt(3, retry_count);
        pstmt->setString(4, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("updateTaskSchedule", e);
        return false;
    }
}

bool MySQLClient::updateTaskRuntime(const std::string& id, int status, const std::string& next_run_at,
                                    const std::string& last_run_at, int retry_count) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "UPDATE tasks SET status=?, next_run_at=?, last_run_at=?, retry_count=?, "
            "current_execution_id=NULL, running_node=NULL, started_at=NULL WHERE id=?"));
        pstmt->setInt(1, status);
        set_datetime_or_null(*pstmt, 2, next_run_at);
        set_datetime_or_null(*pstmt, 3, last_run_at);
        pstmt->setInt(4, retry_count);
        pstmt->setString(5, id);
        pstmt->execute();
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("updateTaskRuntime", e);
        return false;
    }
}

bool MySQLClient::cancelTask(const std::string& id) {
    auto lease = acquireConnection();
    if (!lease.valid()) return false;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "UPDATE tasks SET status=0, current_execution_id=NULL, running_node=NULL, started_at=NULL WHERE id=?"));
        pstmt->setString(1, id);
        if (pstmt->executeUpdate() > 0) return true;

        std::unique_ptr<sql::PreparedStatement> exists_stmt(lease.conn->prepareStatement(
            "SELECT COUNT(*) AS cnt FROM tasks WHERE id=?"));
        exists_stmt->setString(1, id);
        std::unique_ptr<sql::ResultSet> res(exists_stmt->executeQuery());
        return res->next() && res->getInt("cnt") > 0;
    } catch (sql::SQLException &e) {
        recordSqlError("cancelTask", e);
        return false;
    }
}

int MySQLClient::historyCount(const std::string& task_id) {
    auto lease = acquireConnection();
    if (!lease.valid()) return 0;
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(lease.conn->prepareStatement(
            "SELECT COUNT(*) AS cnt FROM task_history WHERE task_id=?"));
        pstmt->setString(1, task_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (res->next()) return res->getInt("cnt");
    } catch (sql::SQLException &e) {
        recordSqlError("historyCount", e);
    }
    return 0;
}

bool MySQLClient::ensureSchema(sql::Connection& conn) {
    try {
        executeStatement(conn,
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
            "current_execution_id VARCHAR(128) NULL,"
            "running_node VARCHAR(128) NULL,"
            "started_at DATETIME NULL,"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "INDEX idx_tasks_status_next_run (status, next_run_at)"
            ")");
        executeStatement(conn,
            "CREATE TABLE IF NOT EXISTS task_history ("
            "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            "execution_id VARCHAR(128) NULL,"
            "task_id VARCHAR(64) NOT NULL,"
            "exec_node VARCHAR(128),"
            "success TINYINT NOT NULL DEFAULT 0,"
            "result TEXT,"
            "error TEXT,"
            "start_time DATETIME,"
            "end_time DATETIME,"
            "UNIQUE KEY uk_task_history_execution_id (execution_id),"
            "INDEX idx_task_history_task_id (task_id),"
            "INDEX idx_task_history_start_time (start_time)"
            ")");

        if (!columnExists(conn, "tasks", "next_run_at")) executeStatement(conn, "ALTER TABLE tasks ADD COLUMN next_run_at DATETIME NULL");
        if (!columnExists(conn, "tasks", "last_run_at")) executeStatement(conn, "ALTER TABLE tasks ADD COLUMN last_run_at DATETIME NULL");
        if (!columnExists(conn, "tasks", "retry_count")) executeStatement(conn, "ALTER TABLE tasks ADD COLUMN retry_count INT NOT NULL DEFAULT 0");
        if (!columnExists(conn, "tasks", "max_retries")) executeStatement(conn, "ALTER TABLE tasks ADD COLUMN max_retries INT NOT NULL DEFAULT 3");
        if (!columnExists(conn, "tasks", "current_execution_id")) executeStatement(conn, "ALTER TABLE tasks ADD COLUMN current_execution_id VARCHAR(128) NULL");
        if (!columnExists(conn, "tasks", "running_node")) executeStatement(conn, "ALTER TABLE tasks ADD COLUMN running_node VARCHAR(128) NULL");
        if (!columnExists(conn, "tasks", "started_at")) executeStatement(conn, "ALTER TABLE tasks ADD COLUMN started_at DATETIME NULL");
        if (!columnExists(conn, "task_history", "execution_id")) executeStatement(conn, "ALTER TABLE task_history ADD COLUMN execution_id VARCHAR(128) NULL AFTER id");
        if (!columnExists(conn, "task_history", "success")) executeStatement(conn, "ALTER TABLE task_history ADD COLUMN success TINYINT NOT NULL DEFAULT 0 AFTER exec_node");
        if (!indexExists(conn, "task_history", "uk_task_history_execution_id")) executeStatement(conn, "CREATE UNIQUE INDEX uk_task_history_execution_id ON task_history (execution_id)");
        return true;
    } catch (sql::SQLException &e) {
        recordSqlError("ensureSchema", e);
        return false;
    }
}

bool MySQLClient::columnExists(sql::Connection& conn, const std::string& table, const std::string& column) {
    std::unique_ptr<sql::PreparedStatement> pstmt(conn.prepareStatement(
        "SELECT COUNT(*) AS cnt FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA=? AND TABLE_NAME=? AND COLUMN_NAME=?"));
    pstmt->setString(1, database_);
    pstmt->setString(2, table);
    pstmt->setString(3, column);
    std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
    return res->next() && res->getInt("cnt") > 0;
}

bool MySQLClient::indexExists(sql::Connection& conn, const std::string& table, const std::string& index) {
    std::unique_ptr<sql::PreparedStatement> pstmt(conn.prepareStatement(
        "SELECT COUNT(*) AS cnt FROM information_schema.STATISTICS "
        "WHERE TABLE_SCHEMA=? AND TABLE_NAME=? AND INDEX_NAME=?"));
    pstmt->setString(1, database_);
    pstmt->setString(2, table);
    pstmt->setString(3, index);
    std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
    return res->next() && res->getInt("cnt") > 0;
}

bool MySQLClient::executeStatement(sql::Connection& conn, const std::string& sql_text) {
    std::unique_ptr<sql::Statement> stmt(conn.createStatement());
    stmt->execute(sql_text);
    return true;
}

} // namespace corpcron
