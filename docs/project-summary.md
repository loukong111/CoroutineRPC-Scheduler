# 项目能力摘要

## 项目名称

协程化 RPC 框架与分布式定时任务调度系统

## 一句话介绍

基于 C++20 协程和 epoll 实现轻量级 RPC 框架，并在其上构建控制节点与 Worker 分离的分布式定时任务调度系统，覆盖网络通信、协议编解码、任务持久化、服务发现、分布式锁、失败重试、可观测性、自动化测试和工程部署。

## 技术栈

C++20、epoll、协程、Protobuf、MySQL、Redis、CMake、Docker Compose、systemd、Nginx、Prometheus、Alertmanager、Grafana、Qt6、CTest、clang-format、clang-tidy

## 核心能力

- 基于 C++20 协程与 epoll 实现 Reactor 网络模型，封装 `Task` / `SocketAwaitable`，将异步 TCP 读写改造为同步化编码方式，并支持长连接多帧请求处理；RPC Handler 投递到有界动态线程池，慢任务不会阻塞 epoll，过载时返回 `RESOURCE_EXHAUSTED`。
- 设计自定义二进制 RPC 协议，协议头包含包长和 `serial_id`，结合 Protobuf 完成请求响应编解码，并基于 `proto service` 生成 C++ typed stub / skeleton 绑定；服务端支持 interceptor 链和简化 server-side streaming，客户端支持 deadline、cancellation、健康检查和熔断半开探测。
- 基于 MySQL 实现任务元数据和执行历史持久化，使用 `next_run_at` 驱动调度，支持 scheduled/running/disabled 状态流转、`execution_id` 幂等、stale running 恢复、失败重试、指数退避、任务取消、立即执行和 misfire 策略。
- 将控制节点与执行节点拆成 `corpcron_server` 和 `corpcron_worker` 两类进程；Worker 按 `worker:<handler>` 注册执行能力，调度器基于 Redis 能力发现、RPC 连接池、round-robin 和熔断恢复分发任务，并通过 owner 校验、锁续约和失锁取消保证多节点单次执行。
- 完善工程化交付能力，提供 Docker Compose、Dockerfile、systemd、Nginx TLS 代理示例、生产配置样例、Prometheus/Alertmanager/Grafana 监控示例、支持分页筛选和执行详情的 Qt 管理端、自动化测试、压测脚本、Prometheus 风格指标导出、内置失败告警和存储客户端调优能力，覆盖协议单测、RPC 测试、Redis/MySQL 集成测试及端到端调度链路验证。

## 技术亮点

- 网络层：epoll + 协程，避免回调式状态机扩散。
- 协议层：自定义二进制协议，处理半包、坏包、超大包和统一错误码。
- IDL 层：`proto/rpc.proto` 声明 `CorpCronRpc` service，构建期生成 typed stub / skeleton，减少调用方手写 serial id 和响应解析逻辑。
- 调度层：`next_run_at` 持久化调度配合调度版本 CAS 与 `execution_id` 状态机，避免多节点使用旧扫描快照重复认领。
- 分布式：Redis 锁释放和续约都校验 owner，避免误删其他节点的锁。
- 进程边界：控制节点负责管理与调度，Worker 负责 Handler 执行；角色错误的 RPC 方法会被拒绝。
- 过载保护：网络事件循环与 Handler 执行池隔离，有界队列避免请求无限堆积。
- RPC 调用：调度器复用到各 endpoint 的长连接，并对连续失败节点做短暂冷却。
- 可靠性：支持优雅退出、锁续约、任务超时、Redis/MySQL 连接池、存储超时分类、基础重连、失败重试和 misfire。
- 可观测性：关键链路日志带 `request_id`、`task_id`、协议号和耗时字段，并通过 `GetMetrics`、`/metrics`、`/alerts` 和 Grafana 面板查看连接数、请求数、错误数、任务执行耗时 p95/p99、调度延迟 p95/p99、锁竞争指标和失败告警状态。

## 当前边界

- RPC 鉴权是基础 Token；项目提供 Nginx TLS 终止示例，但服务端协议本身还没有内置 TLS、签名或权限模型。
- MySQL/Redis 存储客户端已支持连接池、基础重连和超时分类；尚未实现熔断和更细粒度的慢查询统计。
- Worker 内置 Handler 仍是进程内函数，生产环境还需要任务沙箱、权限隔离和更严格的超时终止策略。
- cancellation 可以停止客户端等待并向 Worker 传播 deadline，但尚未通过独立控制帧强制中止已经运行的远端 Handler。
- 项目提供 Prometheus/Alertmanager/Grafana 示例；生产环境还需要按实际环境补充通知渠道、权限控制、数据保留和容量规划。

## 推荐讲法

先用 30 秒说明项目整体链路，再围绕三个点展开：

1. 网络和协议：为什么用 epoll + 协程，RPC 协议如何处理半包和错误。
2. 分布式调度：MySQL `next_run_at`、Redis 服务发现、分布式锁和锁续约如何协作。
3. 工程化：Docker/测试/Qt/压测/指标如何保证项目可运行、可验证、可演示。
