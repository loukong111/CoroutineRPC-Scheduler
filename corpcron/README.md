# Corpcron

Corpcron 是一个 C++20 实现的轻量级分布式定时任务调度项目，包含自定义 RPC 协议、Redis 服务发现/分布式锁、MySQL 任务持久化、动态线程池和 Qt 客户端示例。

## 功能

- 自定义 TCP RPC 协议，使用 Protobuf 编解码业务消息。
- RPC 包长校验，支持半包处理，并限制最大帧大小。
- Redis 服务注册、心跳续约、服务发现和分布式锁。
- MySQL 持久化任务、执行历史和下一次调度时间。
- 调度器按 `next_run_at` 查询到期任务，抢锁后执行并写入历史。
- 任务失败后按指数退避重试，超过 `max_retries` 后自动禁用。
- 执行期间会续约 Redis 锁，降低长任务重复执行风险。
- RPC 支持统一错误响应 `RpcError`，便于客户端识别协议级错误。
- 支持基础 RPC token 鉴权、连接数上限和取消任务接口。
- 支持环境变量覆盖配置，避免在配置文件中提交真实密码。
- Docker Compose 提供 Redis/MySQL 开发环境。
- 提供 Dockerfile、systemd unit 和基础部署文档。
- CTest 接入基础协议单测和可选 Redis/MySQL 集成测试。

## 依赖

Ubuntu 22.04/24.04 可安装：

```bash
sudo apt update
sudo apt install -y g++ cmake libprotobuf-dev protobuf-compiler \
  libhiredis-dev libmysqlcppconn-dev qt6-base-dev qt6-tools-dev
```

如果只运行服务端和测试客户端，Qt 不是必需的；未安装 Qt 时 CMake 会跳过客户端。

## 快速启动

启动依赖：

```bash
docker compose up -d
```

编译：

```bash
cmake -S . -B build
cmake --build build -j
```

运行服务端：

```bash
./build/corpcron_server --config config/server.conf
```

提交测试任务：

```bash
./build/test_submit_client 127.0.0.1 8081
```

运行测试：

```bash
ctest --test-dir build --output-on-failure
```

运行 Redis/MySQL 集成测试：

```bash
CORPCRON_RUN_INTEGRATION_TESTS=1 ctest --test-dir build --output-on-failure
```

如果使用本机已有数据库，可以通过环境变量覆盖密码：

```bash
CORPCRON_RUN_INTEGRATION_TESTS=1 \
CORPCRON_MYSQL_PASSWORD=your_password \
ctest --test-dir build --output-on-failure
```

运行端到端调度测试也使用同一个开关。该测试会启动本地 TCP 服务，插入一个到期任务，并等待执行历史落库。

简单压测：

```bash
./scripts/benchmark.sh 127.0.0.1 8081 16 1000
```

压测结果会保存到 [docs/assets/benchmark/latest.txt](docs/assets/benchmark/latest.txt)，历史结果按时间戳保存在同一目录。

## 配置

参考 [config/server.example.conf](config/server.example.conf)。

常用环境变量覆盖：

```bash
CORPCRON_SERVER_LISTEN_PORT=8082
CORPCRON_SERVER_ADVERTISE_HOST=192.168.1.10
CORPCRON_MYSQL_PASSWORD=your_password
CORPCRON_RPC_AUTH_TOKEN=your_token
```

多节点部署时，每个节点应配置不同端口或不同机器地址，并设置可被其他节点访问的 `server.advertise_host`。

## 数据库

初始化 SQL 位于 [sql/init.sql](sql/init.sql)。服务启动时也会对缺失的基础表和新增列做一次轻量补齐，方便从旧版本本地库平滑启动。

核心表：

- `tasks`：任务定义、状态、`next_run_at`、`last_run_at`、重试计数。
- `task_history`：每次执行的节点、结果、错误、开始/结束时间。

## 项目结构

```text
client/          Qt 客户端示例
config/          配置文件和样例
generated/       Protobuf 生成代码
include/         公共头文件
proto/           Protobuf 定义
sql/             数据库初始化脚本
scripts/         启动和压测脚本
src/             服务端源码
tests/           自动化测试
tools/           测试客户端和压测工具
```

## 当前限制

- RPC 已支持基础 Token 鉴权和连接数限制，但还没有 TLS，不建议直接暴露到公网。
- Redis 锁已支持续约，但任务执行仍应尽量设计为幂等。
- 调度策略已使用 `next_run_at`，并支持 misfire 和任务取消；后续可继续补任务查询/编辑 API。
- 自动化测试已覆盖协议、Redis/MySQL 集成和端到端调度链路；后续可继续补多节点压测。

## 文档

- [部署说明](docs/deploy.md)
- [架构设计](docs/design.md)
- [RPC 协议](docs/protocol.md)
- [压测说明](docs/benchmark.md)

## 后续路线

1. 增加任务查询/编辑 API 和基础管理页面。
2. 增加失败告警、指标导出和结构化日志。
3. 增加持久连接压测和多节点压测报告。
4. 加强鉴权模型，支持 TLS 或反向代理部署示例。
