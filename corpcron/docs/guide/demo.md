# 演示流程

这份文档用于从零演示项目功能，适合自测、答辩或给别人快速介绍项目时按步骤操作。

## 1. 启动依赖

```bash
docker compose up -d
./scripts/demo_check.sh
```

预期端口：

```text
Redis: 127.0.0.1:6380
MySQL: 127.0.0.1:3307
RPC:   127.0.0.1:8081
```

## 2. 编译和测试

```bash
./scripts/check.sh
```

如果要跑 Redis/MySQL 集成和端到端调度测试：

```bash
./scripts/check.sh --integration
```

如果希望脚本自动启动 Docker 依赖：

```bash
./scripts/check.sh --compose
```

## 3. 清理演示数据

```bash
./scripts/clean_demo_data.sh
```

该脚本会清空 `tasks`、`task_history`，并清理 Redis 当前 DB。

## 4. 启动服务端

```bash
./build/corpcron_server --config config/server.conf
```

启动后应看到类似日志：

```text
TcpServer listening on 0.0.0.0:8081
```

## 5. 打开 Qt 管理端

```bash
./build/client/corpcron_client
```

连接参数：

```text
Host: 127.0.0.1
Port: 8081
Token: 留空，除非配置了 rpc.auth_token
```

连接成功后，Qt 会刷新：

- 服务发现
- 任务列表
- 执行历史
- 运行指标

也可以直接使用 Qt 的“演示控制台”页执行：

- 启动依赖：等价于 `docker compose up -d`
- 停止依赖：等价于 `docker compose down`
- 重置依赖：等价于 `docker compose down -v`
- 启动服务端：等价于 `./build/corpcron_server --config config/server.conf`
- 启动二节点：等价于 `./build/corpcron_server --config config/server2.conf`
- 环境检查：等价于 `./scripts/demo_check.sh`
- 清理数据：等价于 `./scripts/clean_demo_data.sh`
- 构建测试：等价于 `./scripts/check.sh`
- 集成/E2E：等价于 `./scripts/check.sh --compose`
- 构建镜像：等价于 `docker build -t corpcron:local .`
- 查看压测结果：显示 `docs/assets/benchmark/latest.txt`
- 查看部署文档：显示 `docs/guide/deploy.md` 与 `systemd/corpcron.service`
- 短连接/长连接压测：等价于 `./scripts/benchmark.sh ... short/reuse`
- 协议异常演示：鉴权失败、未知方法、坏包断连

如果 Docker 当前需要 sudo 权限，Qt 无法自动输入 sudo 密码。建议提前把当前用户加入 docker 组，或者仍然用终端启动依赖。

## 6. 演示 RPC 和任务链路

### Echo

在“RPC 操作”页输入文本，点击“发送 Echo”，日志应显示 Echo 响应。

### 提交任务

使用默认任务：

```text
Cron: * * * * * ?
Handler: Echo
Params: demo
```

点击“提交任务”，任务列表会出现新任务。等待调度器执行后，执行历史会出现记录。

### 任务操作

在“任务列表”页选择任务后，可演示：

- 按状态、关键字分页筛选任务
- 保存修改
- 禁用 / 启用
- 立即执行
- 删除

### 执行历史

在“执行历史”页可演示：

- 按任务 ID、执行结果和关键字分页筛选历史
- 点击历史记录查看完整 `execution_id`、执行节点、结果、错误和开始/结束时间

### 服务发现

在“服务发现”页点击刷新，应看到当前 RPC 节点，例如：

```text
127.0.0.1:8081
```

### 运行指标

在“运行指标”页点击刷新，应看到：

- `rpc_requests_total`
- `rpc_success_total`
- `rpc_error_total`
- `active_connections`
- `bytes_in_total`
- `bytes_out_total`
- `task_success_total`
- `task_failure_total`
- `lock_acquire_success_total`
- `lock_acquire_failure_total`

可以先发送几次 Echo 或刷新任务列表，再观察指标递增。

## 7. 查看 MySQL 数据

```bash
docker exec -it corpcron-mysql mysql -ucorpcron -pcorpcron_dev_password corpcron
```

查看任务：

```sql
SELECT id, handler, status, current_execution_id, running_node, next_run_at, last_run_at, retry_count
FROM tasks
ORDER BY created_at DESC
LIMIT 10;
```

查看执行历史：

```sql
SELECT execution_id, task_id, exec_node, success, result, error, start_time, end_time
FROM task_history
ORDER BY id DESC
LIMIT 10;
```

## 8. 查看 Redis 服务发现

```bash
docker exec -it corpcron-redis redis-cli
```

```redis
SMEMBERS services:rpc
KEYS services:rpc:*
TTL services:rpc:127.0.0.1:8081
```

## 9. 运行压测

短连接模式：

```bash
./scripts/benchmark.sh 127.0.0.1 8081 16 1000 short
```

长连接复用模式：

```bash
./scripts/benchmark.sh 127.0.0.1 8081 16 1000 reuse
```

结果会保存到：

```text
docs/assets/benchmark/latest.txt
```

上述压测也可以在 Qt “演示控制台”页点击按钮完成。

## 10. 演示结束

停止服务端：

```bash
Ctrl+C
```

停止依赖：

```bash
docker compose down
```

如果要清空卷并重置数据库：

```bash
docker compose down -v
```
