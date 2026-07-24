# 多节点压测说明

`scripts/multinode_benchmark.sh` 会启动两个控制节点和两个 Worker，用于验证
CorpCron 在多节点场景下的三个关键点：

- 两个控制节点都能独立承载管理 RPC，两个 Worker 都能注册 Handler 能力。
- 两个调度器同时扫描同一个到期任务时，Redis 锁和 MySQL 状态机能保证只产生一条执行历史。
- 停掉一组控制节点与 Worker 后，剩余节点仍能发现任务并完成接管执行。

## 运行方式

先准备依赖和构建产物：

```bash
docker compose up -d
cmake -S . -B build
cmake --build build -j
```

运行多节点压测：

```bash
./scripts/multinode_benchmark.sh 127.0.0.1 16 1000 reuse
```

参数含义：

```text
host concurrency requests mode
```

默认会：

1. 清理演示数据。
2. 启动 `config/worker.conf` 与 `config/worker2.conf` 对应的两个 Worker。
3. 启动 `config/server.conf` 与 `config/server2.conf` 对应的两个控制节点。
4. 分别对控制节点 `8081` 和 `8082` 执行 `bench_client`。
5. 检查 `worker` 和 `worker:Echo` 能力注册。
6. 插入一个到期任务，验证同一任务只执行一次。
7. 停掉 node1 和 worker1，再插入到期任务，验证 node2 与 worker2 完成接管。
8. 抓取控制节点和 Worker 的健康状态、核心指标及 Redis 服务发现状态。

如果服务端已经手动启动，可以复用已有进程：

```bash
CORPCRON_MULTINODE_USE_EXISTING=1 ./scripts/multinode_benchmark.sh
```

如果希望脚本顺手启动 Docker 依赖：

```bash
CORPCRON_MULTINODE_START_COMPOSE=1 ./scripts/multinode_benchmark.sh
```

如果不想清理已有演示数据：

```bash
CORPCRON_MULTINODE_CLEAN=0 ./scripts/multinode_benchmark.sh
```

## 输出文件

脚本会生成：

```text
docs/assets/benchmark/multinode-YYYYMMDD-HHMMSS.md
docs/assets/benchmark/multinode-latest.md
docs/assets/benchmark/logs/server1-YYYYMMDD-HHMMSS.log
docs/assets/benchmark/logs/server2-YYYYMMDD-HHMMSS.log
docs/assets/benchmark/logs/worker1-YYYYMMDD-HHMMSS.log
docs/assets/benchmark/logs/worker2-YYYYMMDD-HHMMSS.log
```

最近一次报告见 [multinode-latest.md](../assets/benchmark/multinode-latest.md)。

## 如何看结果

重点看报告中的两段：

```text
## 3. 多节点锁竞争：同一任务只执行一次
history_count: 1
result: PASS
```

这说明两个调度器同时扫描同一个 due task 时，只有一个节点完成了 Redis 抢锁和 MySQL `running` 状态认领。

```text
## 4. 故障接管：停掉 node1 和 worker1 后备用节点执行新任务
history_count: 1
result: PASS
```

这说明 node1 和 worker1 停止后，node2 仍然能调度任务，并通过
`worker:Echo` 发现 worker2 完成远程执行。

同时检查两个节点的 `/alerts`：

```text
status=ok
alerts_firing=0
```

如果出现 `lock_failure_rate_high`，不一定代表错误。在双调度器抢同一批任务时，锁获取失败可能是正常竞争；如果失败率长期很高，再结合 Redis 日志、服务发现 TTL 和任务历史判断是否有抖动。
