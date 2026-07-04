# 压测说明

`bench_client` 用来并发发送 Echo RPC 请求，并输出 QPS、p50/p95/p99 延迟等基础指标。压测脚本会把结果保存到 `docs/assets/benchmark/`，其中 `latest.txt` 始终指向最近一次结果。

脚本还会尝试自动发现 `corpcron_server` 进程，并在压测前后记录 CPU、内存、RSS、VSZ 和运行时长，便于把吞吐和资源占用放在一起分析。如果服务端不是以 `corpcron_server` 进程名运行，可以通过 `CORPCRON_SERVER_PID` 指定进程 ID。

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

## client_result
requests=1000 concurrency=16 success=1000 failure=0 elapsed_sec=... qps=... p50_ms=... p95_ms=... p99_ms=...

## server_after
PID %CPU %MEM RSS VSZ ELAPSED CMD
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
