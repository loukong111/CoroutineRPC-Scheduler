# 压测说明

`bench_client` 用来并发发送 Echo RPC 请求，并输出 QPS、p50/p95/p99 延迟等基础指标。压测脚本会把结果保存到 `docs/assets/benchmark/`，其中 `latest.txt` 始终指向最近一次结果。

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
host port concurrency requests
```

如果启用了 RPC 鉴权，需要通过环境变量传入 token：

```bash
CORPCRON_RPC_AUTH_TOKEN=your_token ./scripts/benchmark.sh 127.0.0.1 8081 16 1000
```

## 输出示例

```text
requests=1000 concurrency=16 success=1000 failure=0 elapsed_sec=... qps=... p50_ms=... p95_ms=... p99_ms=...
```

脚本会生成两类文件：

```text
docs/assets/benchmark/bench-YYYYMMDD-HHMMSS.txt
docs/assets/benchmark/latest.txt
```

当前工作区保留的最近一次结果见 [latest.txt](assets/benchmark/latest.txt)。

## 说明

当前压测客户端是“每个请求创建一个 TCP 连接”的模型，适合做功能烟测和回归对比，但不能代表长连接 RPC 服务的极限吞吐。后续如果要做更正式的性能测试，可以增加连接复用版本的压测客户端。
