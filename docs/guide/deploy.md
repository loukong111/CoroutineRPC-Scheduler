# 部署说明

## 本地依赖环境

```bash
docker compose up -d
cmake -S . -B build
cmake --build build -j
./build/corpcron_worker --config config/worker.conf
./build/corpcron_server --config config/server.conf
```

本地 Compose 会将 Redis 映射到宿主机 `6380`、MySQL 映射到宿主机 `3307`，避免占用已有的 `6379` 和 `3306` 服务。

控制节点默认在 `127.0.0.1:9091` 暴露运行指标，Worker 使用
`127.0.0.1:9191`：

```bash
curl http://127.0.0.1:9091/metrics
curl http://127.0.0.1:9091/health
curl http://127.0.0.1:9091/alerts
curl http://127.0.0.1:9191/metrics
curl http://127.0.0.1:9191/health
```

可以通过配置或环境变量修改指标端口：

```ini
[metrics]
enabled = 1
host = 127.0.0.1
port = 9091
```

失败告警阈值可以通过 `[alerts]` 调整：

```ini
[alerts]
min_rpc_requests = 100
rpc_error_rate_percent = 5
min_task_executions = 10
task_failure_rate_percent = 20
min_lock_attempts = 10
lock_failure_rate_percent = 50
schedule_delay_p99_ms = 5000
task_duration_p99_ms = 10000
```

```bash
CORPCRON_METRICS_PORT=9092 ./build/corpcron_server --config config/server.conf
```

调度器的 RPC 连接池也可以通过配置调整：

```ini
[scheduler]
task_timeout_ms = 5000
running_stale_timeout_sec = 120
rpc_pool_max_idle_per_endpoint = 4
rpc_pool_failure_threshold = 2
rpc_pool_cooldown_ms = 3000
```

`running_stale_timeout_sec` 不应小于任务锁 TTL 的两倍。即使配置得更小，服务端也会按 120 秒的安全下限处理，避免把仍在执行的任务误判为崩溃残留。

Redis/MySQL 客户端连接池和超时也可以通过配置调整：

```ini
[redis]
pool_size = 4
connect_timeout_ms = 1000
command_timeout_ms = 1000

[mysql]
pool_size = 4
connect_timeout_sec = 3
read_timeout_sec = 5
write_timeout_sec = 5
reconnect = 1
```

更多说明见 [存储客户端调优](storage-tuning.md)。

如需长期采集指标和查看 Grafana 面板，可以启动本地监控栈：

```bash
docker compose -f deploy/monitoring/docker-compose.monitoring.yml up -d
```

更多说明见 [监控栈说明](monitoring.md)。

## 容器镜像

```bash
docker build -t corpcron:local .
docker run --rm --name corpcron-worker -p 8181:8181 \
  -e CORPCRON_REDIS_HOST=host.docker.internal \
  corpcron:local \
  /app/corpcron_worker --config /app/config/worker.conf

docker run --rm --name corpcron-server -p 8081:8081 \
  -e CORPCRON_REDIS_HOST=host.docker.internal \
  -e CORPCRON_MYSQL_HOST=host.docker.internal \
  -e CORPCRON_MYSQL_PASSWORD=corpcron_dev_password \
  corpcron:local
```

在 Linux 环境下，如果 `host.docker.internal` 不可用，可以替换成宿主机可访问的地址，或者让服务和 MySQL/Redis 运行在同一个 Compose 网络中。
控制节点提交任务前，Redis 中必须存在匹配的 `worker:<handler>` 能力。

## systemd

安装二进制文件和配置：

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin corpcron
sudo mkdir -p /opt/corpcron/bin /etc/corpcron
sudo cp build/corpcron_server build/corpcron_worker /opt/corpcron/bin/
sudo cp config/server.production.example.conf /etc/corpcron/server.conf
sudo cp config/worker.production.example.conf /etc/corpcron/worker.conf
sudo cp systemd/corpcron.service systemd/corpcron-worker.service /etc/systemd/system/
```

分别参考 [控制节点环境变量样例](../../deploy/corpcron.env.example) 和
[Worker 环境变量样例](../../deploy/corpcron-worker.env.example)，将敏感配置放到
`/etc/corpcron/corpcron.env` 与 `/etc/corpcron/worker.env`。两个进程必须使用相同的
Token：

```bash
CORPCRON_MYSQL_PASSWORD=your_password
CORPCRON_RPC_AUTH_TOKEN=your_long_random_token
CORPCRON_SERVER_ADVERTISE_HOST=127.0.0.1
```

先启动 Worker，再启动控制节点：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now corpcron-worker corpcron
sudo journalctl -u corpcron-worker -f
sudo journalctl -u corpcron -f
```

## 生产安全部署

生产环境不建议把 CorpCron RPC 直接暴露到公网。推荐拓扑：

```text
Client -> Nginx stream TLS :8443 -> 127.0.0.1:8081 Control Plane RPC
Control Plane -> private network :8181 -> Worker RPC
Prometheus/VPN -> Nginx HTTPS :9443 -> 127.0.0.1:9091 /metrics /alerts
```

相关模板：

- [生产配置样例](../../config/server.production.example.conf)
- [Worker 生产配置样例](../../config/worker.production.example.conf)
- [环境变量样例](../../deploy/corpcron.env.example)
- [Worker 环境变量样例](../../deploy/corpcron-worker.env.example)
- [Nginx RPC stream TLS 示例](../../deploy/nginx/corpcron-stream.conf)
- [Nginx metrics HTTPS 示例](../../deploy/nginx/corpcron-metrics.conf)
- [生产部署安全说明](production-security.md)

最小安全要求：

- `server.bind_host` 使用 `127.0.0.1` 或内网地址。
- `worker.bind_host` 只使用控制节点可访问的内网地址，不暴露公网。
- 设置强随机 `CORPCRON_RPC_AUTH_TOKEN`。
- MySQL/Redis 不暴露公网。
- `/metrics` 和 `/alerts` 只允许监控网络或 VPN 访问。
