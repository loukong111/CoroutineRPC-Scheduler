# Corpcron：基于 C++20 协程的 RPC 与分布式定时任务调度系统

## 项目简介

Corpcron 是一个基于 C++20 实现的协程化 RPC 与分布式定时任务调度系统，主要用于学习和实践 C++ 后端开发中的网络编程、RPC 编解码、任务调度、服务注册发现、分布式锁和后台线程池等技术点。

项目包含 RPC 服务端、任务提交客户端、可选 Qt 客户端、Cron 任务调度器、Redis 服务注册发现、MySQL 任务持久化和动态线程池。多个节点启动后，可通过 Redis 分布式锁保证同一任务在同一时间窗口内只由一个节点执行。

## 核心功能

- **协程化网络处理**：基于 `epoll` 与 C++20 协程封装 Socket 读写事件。
- **自定义 RPC 协议**：协议格式为 `总长度 + 消息类型/序列号 + Protobuf Payload`，用于解决 TCP 粘包/拆包问题。
- **Protobuf 编解码**：定义 Echo、提交任务、执行任务等请求与响应结构。
- **任务持久化**：使用 MySQL 存储定时任务元信息和任务执行历史表结构。
- **Cron 调度**：支持 Cron 表达式解析，调度线程周期性扫描可执行任务。
- **服务注册发现**：服务节点启动后注册到 Redis，并通过心跳刷新 TTL。
- **分布式锁**：执行任务前通过 Redis 锁竞争，避免多个节点重复执行同一任务。
- **动态线程池**：调度任务交由线程池处理，根据任务积压情况扩缩容。
- **可选 Qt 客户端**：提供简单图形客户端入口，便于演示任务提交。

## 技术栈

| 模块 | 实现 |
| --- | --- |
| 开发语言 | C++20 |
| 构建工具 | CMake |
| 网络模型 | Socket + epoll + C++20 coroutine |
| RPC 协议 | 自定义二进制协议 + Protobuf |
| 任务调度 | Cron 表达式 + 后台扫描线程 |
| 服务发现 | Redis SETEX + 心跳续期 |
| 分布式锁 | Redis Lua 脚本封装 SETNX + EXPIRE |
| 数据存储 | MySQL Connector/C++ |
| 客户端 | 命令行测试客户端 + 可选 Qt6 客户端 |

## 项目结构

```text
corpcron/
├── CMakeLists.txt
├── client/                         # Qt 客户端，可选编译
├── config/
│   ├── server.conf                 # 节点 1 配置
│   └── server2.conf                # 节点 2 配置示例
├── include/corpcron/
│   ├── common/                     # 配置、日志、线程池、内存池
│   ├── coroutine/                  # Task 与协程调度相关封装
│   ├── mysql/                      # MySQL 客户端封装
│   ├── net/                        # epoll 事件循环与 TCP Server
│   ├── redis/                      # Redis 客户端、服务发现、分布式锁
│   ├── rpc/                        # RPC 协议、客户端、处理器注册
│   └── scheduler/                  # Cron 解析与任务调度器
├── proto/
│   └── rpc.proto                   # Protobuf 协议定义
├── scripts/
│   └── start.sh
├── src/
│   ├── common/
│   ├── coroutine/
│   ├── mysql/
│   ├── net/
│   ├── redis/
│   ├── rpc/
│   ├── scheduler/
│   ├── main.cpp                    # 服务端入口
│   └── test_submit_client.cpp      # 命令行任务提交客户端
└── README.md
```

## 环境依赖

建议环境：

- Ubuntu 22.04 / 24.04 或 WSL2
- g++ 11+，建议 g++ 13+
- CMake 3.20+
- Protobuf
- Redis
- MySQL
- hiredis
- MySQL Connector/C++
- Qt6，可选，仅用于图形客户端

安装示例：

```bash
sudo apt update
sudo apt install -y g++ cmake \
    libprotobuf-dev protobuf-compiler \
    libhiredis-dev redis-server \
    mysql-server libmysqlcppconn-dev \
    qt6-base-dev qt6-tools-dev
```

如果不需要 Qt 客户端，也可以不安装 Qt6；CMake 会在未找到 Qt6 时跳过客户端编译。

## 初始化 Redis 和 MySQL

启动服务：

```bash
sudo systemctl start redis-server || sudo systemctl start redis
sudo systemctl start mysql
```

创建数据库和表：

```sql
CREATE DATABASE IF NOT EXISTS corpcron DEFAULT CHARSET utf8mb4;
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
    start_time TIMESTAMP NULL,
    end_time TIMESTAMP NULL,
    INDEX(task_id)
);
```

## 配置文件

示例配置位于 `config/server.conf`：

```ini
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
```

多节点测试时，可复制一份配置文件并修改端口，例如 `config/server2.conf`：

```ini
[server]
listen_port = 8082
daemon = false
```

## 编译

```bash
git clone https://github.com/loukong111/CoroutineRPC-Scheduler.git
cd CoroutineRPC-Scheduler
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

编译完成后主要生成：

```text
corpcron_server       # RPC 与调度服务端
test_submit_client    # 命令行测试客户端
client/corpcron_client # Qt 客户端，可选
```

## 运行

### 1. 启动单个服务节点

```bash
./corpcron_server --config ../config/server.conf
```

服务启动后会完成：

1. 加载配置文件。
2. 连接 Redis。
3. 向 Redis 注册 RPC 服务地址。
4. 启动心跳线程，定期刷新服务 TTL。
5. 连接 MySQL。
6. 启动任务调度器。
7. 启动 TCP RPC 服务端。

### 2. 提交测试任务

在另一个终端执行：

```bash
./test_submit_client
```

输出示例：

```text
Success=1, TaskID=xxxx-xxxx-xxxx
```

任务会写入 MySQL 的 `tasks` 表，调度器扫描到可执行任务后，会尝试获取 Redis 分布式锁，并通过 RPC 调用节点执行任务。

### 3. 多节点演示

打开两个终端，分别运行：

```bash
./corpcron_server --config ../config/server.conf
```

```bash
./corpcron_server --config ../config/server2.conf
```

再提交一个定时任务。两个节点都会扫描任务，但同一时间窗口内只有抢到 Redis 锁的节点会执行任务；另一个节点会因获取锁失败而跳过。

## RPC 协议说明

项目使用自定义二进制协议解决 TCP 流式传输中的粘包/拆包问题。

协议格式：

```text
| total_len: 4 bytes | serial_id: 4 bytes | protobuf_payload: N bytes |
```

其中：

- `total_len` 表示 `serial_id + payload` 的长度。
- `serial_id` 表示消息类型。
- `protobuf_payload` 是序列化后的 Protobuf 数据。

当前主要消息类型：

| serial_id | 含义 |
| --- | --- |
| 1 | EchoRequest |
| 2 | EchoResponse |
| 3 | SubmitTaskRequest |
| 4 | SubmitTaskResponse |
| 5 | ExecuteTaskRequest |
| 6 | ExecuteTaskResponse |

## 实现要点

### 1. 协程网络模型

TCP 服务端基于 `epoll` 监听连接和读事件，客户端连接由协程处理。协程在等待 I/O 时挂起，事件就绪后由事件循环恢复。

### 2. 服务注册发现

节点启动时将自身地址写入 Redis，并使用心跳线程定期刷新 TTL。调度器执行任务时从 Redis 获取可用 RPC 服务节点。

### 3. 分布式锁

任务执行前，节点通过 Redis Lua 脚本封装 `SETNX + EXPIRE`，在限定时间内竞争任务锁。任务执行完成后使用 value 校验释放锁，避免误删其他节点的锁。

### 4. 定时任务调度

调度器周期性扫描 MySQL 中启用的任务，根据 Cron 表达式计算下一次执行时间。满足执行条件时提交到动态线程池，并通过 RPC 调用实际任务处理器。

### 5. 动态线程池

线程池根据任务队列积压情况增加工作线程，空闲线程在超时后回收，用于处理调度执行等后台任务。

## 当前限制

- Redis 服务发现当前以单个 key 存储服务地址，适合演示流程；若要支持完整多节点列表，可改为 Set / ZSet 结构。
- 当前任务执行逻辑主要用于演示，默认注册了 `Echo` 处理器，实际业务处理器需要继续扩展。
- 分布式锁未实现自动续约，长任务可能需要 WatchDog 机制。
- 故障转移依赖锁过期和后续扫描，不是实时主动故障转移。
- `task_history` 表结构已提供，执行历史记录逻辑可继续完善。
- 当前未提供完整压测报告，后续可补充并发连接数、RPC QPS、平均延迟和 P99 延迟。

## 后续优化方向

- 使用 Redis Set / ZSet 维护多服务实例，完善服务发现能力。
- 增加 RPC 调用超时协程封装，例如 `TimeoutAwaitable`。
- 增加 MySQL 连接池。
- 增加任务执行历史写入与失败重试机制。
- 为分布式锁增加续约机制。
- 补充单元测试、集成测试和压测报告。
- 提供 Docker Compose，一键启动 Redis、MySQL 和多节点服务。
