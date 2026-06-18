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
                    RpcClient -> TcpServer
```

## RPC

网络层基于 `epoll` 和协程 awaitable 实现。每个请求使用如下帧格式：

```text
4 bytes total_len | 4 bytes serial_id | protobuf payload
```

服务端会校验帧长度、处理 TCP 半包、拒绝异常帧，并通过 `RpcError` 返回协议级错误。

如果配置了 `rpc.auth_token`，每个 RPC 请求都必须携带相同 token。这里采用的是基础 token 鉴权，适合内网演示和项目展示；如果要暴露到生产公网，还需要 TLS、更完善的认证授权和限流机制。

## 调度流程

任务持久化在 MySQL 中。调度器通过 `next_run_at` 查询到期任务，尝试获取 Redis 分布式锁，抢锁成功后通过 RPC 分发执行。

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

`task_history` 表保存每次执行的审计记录，包括执行节点、结果、错误信息、开始时间和结束时间。

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

- 暂未支持 TLS。
- Token 鉴权较基础，更适合内网或项目演示。
- Handler 是进程内注册函数，不是独立沙箱任务。
- 除提交和取消外，还没有完整的任务查询/编辑 API。
- 暂未接入生产级指标系统。
