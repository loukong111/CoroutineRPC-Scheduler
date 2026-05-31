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
        return true;
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
            "INSERT INTO tasks (id, cron_expr, params, handler, status) VALUES (?, ?, ?, ?, ?)"));
        pstmt->setString(1, task.id);
        pstmt->setString(2, task.cron_expr);
        pstmt->setString(3, task.params);
        pstmt->setString(4, task.handler);
        pstmt->setInt(5, task.status);
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
            "INSERT INTO task_history (task_id, exec_node, result, error, start_time, end_time) VALUES (?, ?, ?, ?, ?, ?)"));
        pstmt->setString(1, history.task_id);
        pstmt->setString(2, history.exec_node);
        pstmt->setString(3, history.result);
        pstmt->setString(4, history.error);
        pstmt->setString(5, history.start_time);
        pstmt->setString(6, history.end_time);
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
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT id, cron_expr, params, handler, status FROM tasks"));
        while (res->next()) {
            TaskMeta t;
            t.id = res->getString("id");
            t.cron_expr = res->getString("cron_expr");
            t.params = res->getString("params");
            t.handler = res->getString("handler");
            t.status = res->getInt("status");
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
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT id, cron_expr, params, handler, status FROM tasks WHERE status=1"));
        while (res->next()) {
            TaskMeta t;
            t.id = res->getString("id");
            t.cron_expr = res->getString("cron_expr");
            t.params = res->getString("params");
            t.handler = res->getString("handler");
            t.status = res->getInt("status");
            tasks.push_back(t);
        }
    } catch (sql::SQLException &e) {
        std::cerr << "getEnabledTasks error: " << e.what() << std::endl;
    }
    return tasks;
}

} // namespace corpcron