# Corpcron - 协程化RPC + 分布式定时任务调度器
## 项目简介
基于 C++20 协程的高性能 RPC 框架，内置分布式定时任务调度系统。集成服务注册与发现（Redis）、任务持久化（MySQL）、动态线程池、分布式锁等微服务关键组件。
## 核心特性
- **协程 RPC**：利用 C++20 协程实现异步 RPC 调用，支持 `co_await` 等待读写事件。
- **分布式定时调度**：Cron 表达式解析 + MySQL 任务存储 + Redis 分布式锁，保证集群中任务唯一执行。
- **服务注册与发现**：基于 Redis 的自动注册、心跳保活（5秒心跳，30秒 TTL）。
- **动态线程池**：任务积压时自动扩容，空闲超时自动回收。
- **自定义 RPC 协议**：长度 + 序列化 ID + Protobuf 数据，支持三种消息（Echo、提交任务、远程执行）。
- **跨节点 RPC 调用**：调度器通过服务发现获取其他节点，随机负载均衡，实现真正的分布式执行。
- **基础组件**：内存池、配置管理、日志、守护进程、信号处理。
## 技术栈
- C++20（协程、智能指针、原子操作）
- Protobuf 3.21
- hiredis (Redis)
- MySQL Connector/C++
- Qt6 (客户端)
- CMake 3.20+



### 依赖安装（Ubuntu 22.04/24.04）
```bash
sudo apt update
sudo apt install -y g++ cmake libprotobuf-dev protobuf-compiler libhiredis-dev libmysqlcppconn-dev redis-server mysql-server qt6-base-dev qt6-tools-dev

启动 Redis 和 MySQL：
bash
sudo systemctl start redis
sudo systemctl start mysql

创建数据库表
sql
CREATE DATABASE IF NOT EXISTS corpcron;
USE corpcron;
CREATE TABLE IF NOT EXISTS tasks (
    id VARCHAR(64) PRIMARY KEY,
    cron_expr VARCHAR(100) NOT NULL,
    params TEXT,
    handler VARCHAR(100) NOT NULL,
    status TINYINT DEFAULT 1,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS task_history (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    task_id VARCHAR(64) NOT NULL,
    exec_node VARCHAR(50),
    result TEXT,
    error TEXT,
    start_time TIMESTAMP,
    end_time TIMESTAMP,
    INDEX(task_id)
);

配置
编辑 config/server.conf，设置 MySQL 密码等。
ini
[server]
listen_port = 8081
daemon = false

[redis]
host = 127.0.0.1
port = 6379

[mysql]
host = 127.0.0.1
port = 3306
user = root
password = your_password
database = corpcron

编译
bash
mkdir build && cd build
cmake ..
make -j4

运行服务器
bash
./corpcron_server --config ../config/server.conf
提交任务（使用测试客户端）

bash
./test_submit_client
输出示例：Success=1, TaskID=xxxx-...

Qt 客户端（可选）
bash
./corpcron_client
分布式演示
启动两个节点（端口 8081、8082），提交一个每分钟执行的任务，观察只有一个节点执行任务，另一个节点因锁失败。停止执行节点后，另一节点自动接管（锁过期时间 10 秒）。

项目结构
text
corpcron/
├── client/          # Qt 客户端
├── config/          # 配置文件
├── include/         # 头文件
├── proto/           # Protobuf 定义
├── src/             # 源文件
│   ├── common/      # 内存池、线程池、日志、配置
│   ├── net/         # Epoll 事件循环、TCP 服务器
│   ├── redis/       # Redis 客户端（含分布式锁）
│   ├── mysql/       # MySQL 客户端
│   ├── rpc/         # RPC 协议、处理器注册、RPC 客户端
│   └── scheduler/   # Cron 解析、任务调度器
└── CMakeLists.txt
技术亮点
协程栈动态增长：相比异步回调，协程避免栈溢出，支持高并发。
分布式锁：Lua 脚本实现 SETNX+EXPIRE 原子操作。
故障转移：锁超时后其他节点自动接管。
动态线程池：任务积压自动扩容，空闲超时自动缩容。

后续优化方向
节点心跳检测实现主动故障转移
RPC 调用集成协程超时（co_await TimeoutAwaitable）
MySQL 连接池
一致性哈希任务分片
