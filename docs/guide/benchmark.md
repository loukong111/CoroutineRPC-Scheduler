# 压测说明

`bench_client` 用来并发发送 Echo RPC 请求，并输出 QPS、p50/p95/p99 延迟等基础指标。压测脚本会把结果保存到 `docs/assets/benchmark/`，其中 `latest.txt` 始终指向最近一次结果。

脚本还会尝试自动发现 `corpcron_server` 进程，并在压测前后记录 CPU、内存、RSS、VSZ 和运行时长，便于把吞吐和资源占用放在一起分析。如果服务端不是以 `corpcron_server` 进程名运行，可以通过 `CORPCRON_SERVER_PID` 指定进程 ID。

服务端默认在 `127.0.0.1:9091` 暴露 Prometheus 风格指标和轻量告警状态，压测脚本会在压测前后抓取 `/metrics` 和 `/alerts`。如果端口改过，可以通过 `CORPCRON_METRICS_URL` 和 `CORPCRON_ALERTS_URL` 指定：

```bash
CORPCRON_METRICS_URL=http://127.0.0.1:9092/metrics \
CORPCRON_ALERTS_URL=http://127.0.0.1:9092/alerts \
./scripts/benchmark.sh 127.0.0.1 8082 16 1000 reuse
```

## 构建

```bash
cmake -S . -B build
cmake --build build -j
```

## 运行

先启动服务端：

```bash
CORPCRON_MYSQL_PASSWORD=your_password ./build/corpcron_server --config config/server.conf
```

再运行压测脚本：

```bash
./scripts/benchmark.sh 127.0.0.1 8081 16 1000
```

参数含义：

```text
host port concurrency requests mode
```

如果启用了 RPC 鉴权，需要通过环境变量传入 token：

```bash
CORPCRON_RPC_AUTH_TOKEN=your_token ./scripts/benchmark.sh 127.0.0.1 8081 16 1000
```

如果需要手动指定服务端进程：

```bash
CORPCRON_SERVER_PID=$(pgrep -n -x corpcron_server) ./scripts/benchmark.sh 127.0.0.1 8081 32 5000
```

## 输出示例

```text
## server_before
PID %CPU %MEM RSS VSZ ELAPSED CMD

## metrics_before
metrics_url=http://127.0.0.1:9091/metrics
corpcron_rpc_requests_total ...

## alerts_before
alerts_url=http://127.0.0.1:9091/alerts
status=ok
alerts_firing=0

## client_result
requests=1000 concurrency=16 success=1000 failure=0 elapsed_sec=... qps=... p50_ms=... p95_ms=... p99_ms=...

## server_after
PID %CPU %MEM RSS VSZ ELAPSED CMD

## metrics_after
metrics_url=http://127.0.0.1:9091/metrics
corpcron_rpc_requests_total ...

## alerts_after
alerts_url=http://127.0.0.1:9091/alerts
status=ok
alerts_firing=0
```

脚本会生成两类文件：

```text
docs/assets/benchmark/bench-YYYYMMDD-HHMMSS.txt
docs/assets/benchmark/latest.txt
```

当前工作区保留的最近一次结果见 [latest.txt](assets/benchmark/latest.txt)。

## 说明

压测客户端支持两种模式：

```text
short  每个请求创建一个 TCP 连接，适合观察短连接建连成本。
reuse  每个工作线程复用一个 TCP 连接，更接近长连接 RPC 的常见使用方式。
```

示例：

```bash
./scripts/benchmark.sh 127.0.0.1 8081 32 5000 reuse
```

正式写报告时建议至少记录 1、8、16、32、64 并发下的 QPS、p95、p99、失败数和服务端 RSS，并分别给出 `short` 和 `reuse` 两组结果。两组差异可以用于分析瓶颈来自短连接建连成本、单进程事件循环还是后端存储访问。

双节点场景建议使用 [多节点压测脚本](multinode-benchmark.md)，它会额外验证 Redis 锁竞争、节点故障接管和重复执行保护。

常用指标：

```text
corpcron_rpc_requests_total
corpcron_rpc_success_total
corpcron_rpc_error_total
corpcron_active_connections
corpcron_bytes_in_total
corpcron_bytes_out_total
corpcron_task_success_total
corpcron_task_failure_total
corpcron_lock_acquire_success_total
corpcron_lock_acquire_failure_total
corpcron_max_task_duration_ms
corpcron_task_duration_p95_ms
corpcron_task_duration_p99_ms
corpcron_task_duration_samples_total
corpcron_schedule_delay_max_ms
corpcron_schedule_delay_p95_ms
corpcron_schedule_delay_p99_ms
corpcron_schedule_delay_samples_total
```

`task_duration_*` 表示任务 RPC 执行耗时，`schedule_delay_*` 表示任务到期时间与实际被调度扫描到之间的延迟。p95/p99 基于最近 1024 个样本计算，适合观察压测或演示期间的短期状态。

如果 `alerts_after` 显示 `status=firing`，优先看触发的规则名：RPC 错误率通常对应鉴权、协议或连接问题；任务失败率对应 handler 执行错误；锁失败率对应多节点竞争或 Redis 抖动；两个 p99 告警分别对应调度堆积和任务执行耗时异常。
