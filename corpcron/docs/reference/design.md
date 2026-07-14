# 架构设计

## 目标

Corpcron 是一个面向 C++ 后端实践的分布式定时任务调度项目。它的目标不是替代成熟调度平台，而是完整展示网络通信、任务持久化、服务发现、故障处理、自动化测试和部署工程化等后端能力。

## 组件

```text
Client
  |
  | TCP + 自定义 RPC 帧
  v
TcpServer
  |
  | SubmitTask / CancelTask / ExecuteTask
  v
MySQL <---- TaskScheduler ----> Redis
                         |
                         | 发现 RPC 节点
                         v
                    RpcClientPool -> TcpServer
```

## 可观测性

服务端维护一组进程内运行指标，既可以通过 RPC `GetMetrics` 给 Qt 控制台展示，也可以通过 HTTP `/metrics` 暴露为 Prometheus 文本格式。默认监听 `127.0.0.1:9091`，双节点演示中的第二个节点使用 `9092`，避免端口冲突。同一端口还提供 `/alerts`，基于当前指标快照评估 RPC 错误率、任务失败率、锁失败率、调度延迟 p99 和任务耗时 p99。项目提供 Prometheus、Alertmanager 和 Grafana 示例，用于演示长期采集、告警规则和可视化面板。

当前指标覆盖 RPC 请求成功/失败、活跃连接、拒绝连接、坏帧、收发字节数、任务成功/失败、Redis 锁获取结果、任务执行耗时和调度延迟。执行耗时和调度延迟会保留最近 1024 个样本，用于计算 p95/p99；压测脚本会在压测前后抓取这些指标，方便把吞吐、延迟、告警状态和服务端状态放在同一份结果里分析。

关键链路日志使用 `event="..." key="value"` 的结构化格式。RPC 请求会记录 `request_id`、`serial_id`、响应协议号和 payload 大小；调度执行会记录 `task_id`、`node_id`、执行结果、耗时、锁竞争和失败原因，方便按请求或任务回溯问题。

## RPC

网络层基于 `epoll` 和协程 awaitable 实现。每个请求使用如下帧格式：

```text
4 bytes total_len | 4 bytes serial_id | protobuf payload
```

服务端会校验帧长度、处理 TCP 半包、拒绝异常帧，并通过 `RpcError` 返回协议级错误。

如果配置了 `rpc.auth_token`，每个 RPC 请求都必须携带相同 token。这里采用的是基础 token 鉴权，适合内网演示和项目展示；如果要暴露到生产公网，推荐把服务端绑定到本机或内网地址，并通过 Nginx stream 做 TLS 终止，同时继续补充更完善的认证授权和限流机制。

## 调度流程

任务持久化在 MySQL 中。调度器通过 `next_run_at` 查询到期任务，尝试获取 Redis 分布式锁，抢锁成功后通过 RPC 分发执行。

调度器不会每次执行任务都临时创建短连接，而是通过 `RpcClientPool` 维护到各个 endpoint 的可复用长连接。Redis 服务发现返回多个节点时，客户端池按 round-robin 选择 endpoint；某个 endpoint 连续失败后会进入短暂冷却，后续请求优先打到其他健康节点，降低坏节点对任务执行的影响。

任务状态由 MySQL 持久化，当前使用三个核心状态：

- `0 disabled`：任务被停用、取消或超过最大重试次数。
- `1 scheduled`：任务可调度，`next_run_at` 到期后会被扫描。
- `2 running`：任务已经被某个节点认领并正在执行。

调度器抢到 Redis 锁后，会先通过 `claimTaskExecution` 把任务从 `scheduled` CAS 更新为 `running`，同时写入 `execution_id`、`running_node` 和 `started_at`。执行结束后，只有相同的 `execution_id` 才能通过 `completeTaskExecution` 推进状态。若任务执行中被取消，完成阶段不会把它重新改回 scheduled。

执行结果会写入 `task_history`。每次执行结束后：

- 成功：重置 `retry_count`，并根据 Cron 表达式计算下一次执行时间。
- 失败：递增 `retry_count`，并按照指数退避安排下一次重试。
- 如果 `retry_count >= max_retries`，任务会被自动禁用。

## 分布式锁

Redis 锁使用 owner value 标识持有者，解锁时通过 Lua 脚本校验 value，避免误删其他节点的锁。任务执行过程中，调度器会周期性续约锁，降低长任务重复执行的风险。

任务处理函数仍然应该尽量设计成幂等。锁续约可以降低重复执行风险，但不能完全消除网络分区、进程长时间暂停等分布式异常场景。

## Misfire

当服务宕机或暂停后恢复，任务已经错过原定执行时间时，称为 misfire。当前支持两种策略：

- `once`：恢复后尽快补执行一次。
- `skip`：如果超过 `scheduler.misfire_grace_seconds` 宽限时间，则跳过这次错过的执行，并推进到下一次 Cron 时间。

默认策略是 `once`，更适合演示和理解。

## 持久化

`tasks` 表保存任务定义和运行时元数据：

- `status`
- `next_run_at`
- `last_run_at`
- `retry_count`
- `max_retries`
- `current_execution_id`
- `running_node`
- `started_at`

`task_history` 表保存每次执行的审计记录，包括 `execution_id`、执行节点、结果、错误信息、开始时间和结束时间。`execution_id` 有唯一索引，同一次执行重复写历史时会更新同一条记录，避免产生重复执行历史。

服务启动时会执行轻量 schema bootstrap：如果基础表不存在会创建，旧表缺少新增列时会补齐，方便本地演示和从旧版本平滑启动。

## 测试

当前测试分为三层：

- 协议单元测试：覆盖帧解析边界场景。
- 集成测试：覆盖 Redis 服务发现、锁续约、MySQL 任务和历史记录操作。
- 端到端调度测试：启动本地 TCP 服务和调度器，插入一个到期任务，并等待执行历史落库。

集成测试和端到端测试默认跳过，需要通过环境变量显式开启：

```bash
CORPCRON_RUN_INTEGRATION_TESTS=1 ctest --test-dir build --output-on-failure
```

## 已知限制

- 服务端协议本身暂未内置 TLS，生产部署示例通过 Nginx stream 做 TLS 终止。
- Token 鉴权较基础，更适合内网或项目演示。
- Handler 是进程内注册函数，不是独立沙箱任务。
- Redis/MySQL 客户端已经支持连接池、基础重连和超时分类，但还没有熔断和慢查询统计。
- 项目提供 Prometheus/Alertmanager/Grafana 示例；生产环境仍需要按实际团队规范接入真实通知渠道、权限控制和数据保留策略。
