# 项目能力摘要

## 项目名称

协程化 RPC 框架与分布式定时任务调度系统

## 一句话介绍

基于 C++20 协程和 epoll 实现轻量级 RPC 框架，并在其上构建支持多节点部署的分布式定时任务调度系统，覆盖网络通信、协议编解码、任务持久化、服务发现、分布式锁、失败重试、可观测性、自动化测试和工程部署。

## 技术栈

C++20、epoll、协程、Protobuf、MySQL、Redis、CMake、Docker Compose、systemd、Nginx、Prometheus、Alertmanager、Grafana、Qt6、CTest、clang-format、clang-tidy

## 核心能力

- 基于 C++20 协程与 epoll 实现 Reactor 网络模型，封装 `Task` / `SocketAwaitable`，将异步 TCP 读写改造为同步化编码方式，并支持长连接多帧请求处理。
- 设计自定义二进制 RPC 协议，协议头包含包长和 `serial_id`，结合 Protobuf 完成请求响应编解码，支持半包处理、包长保护、统一错误码、Token 鉴权、连接限流与路由分发。
- 基于 MySQL 实现任务元数据和执行历史持久化，使用 `next_run_at` 驱动调度，支持 scheduled/running/disabled 状态流转、`execution_id` 幂等、失败重试、指数退避、任务取消、立即执行和 misfire 策略。
- 基于 Redis 实现服务注册发现、心跳保活、分布式锁、owner 校验和锁续约，调度侧通过 RPC 连接池和 round-robin 在服务节点间分发执行，并支持节点异常后的任务接管。
- 完善工程化交付能力，提供 Docker Compose、Dockerfile、systemd、Nginx TLS 代理示例、生产配置样例、Prometheus/Alertmanager/Grafana 监控示例、支持分页筛选和执行详情的 Qt 管理端、自动化测试、压测脚本、Prometheus 风格指标导出、内置失败告警和存储客户端调优能力，覆盖协议单测、RPC 测试、Redis/MySQL 集成测试及端到端调度链路验证。

## 技术亮点

- 网络层：epoll + 协程，避免回调式状态机扩散。
- 协议层：自定义二进制协议，处理半包、坏包、超大包和统一错误码。
- 调度层：`next_run_at` 持久化调度配合 `execution_id` 状态机，比单纯内存定时器更适合重启恢复和多节点协同。
- 分布式：Redis 锁释放和续约都校验 owner，避免误删其他节点的锁。
- RPC 调用：调度器复用到各 endpoint 的长连接，并对连续失败节点做短暂冷却。
- 可靠性：支持优雅退出、锁续约、任务超时、Redis/MySQL 连接池、存储超时分类、基础重连、失败重试和 misfire。
- 可观测性：关键链路日志带 `request_id`、`task_id`、协议号和耗时字段，并通过 `GetMetrics`、`/metrics`、`/alerts` 和 Grafana 面板查看连接数、请求数、错误数、任务执行耗时 p95/p99、调度延迟 p95/p99、锁竞争指标和失败告警状态。

## 当前边界

- RPC 鉴权是基础 Token；项目提供 Nginx TLS 终止示例，但服务端协议本身还没有内置 TLS、签名或权限模型。
- MySQL/Redis 存储客户端已支持连接池、基础重连和超时分类；尚未实现熔断和更细粒度的慢查询统计。
- 调度任务以字符串 handler 演示为主，生产环境还需要任务沙箱、权限隔离和更严格的超时终止策略。
- 项目提供 Prometheus/Alertmanager/Grafana 示例；生产环境还需要按实际环境补充通知渠道、权限控制、数据保留和容量规划。

## 推荐讲法

先用 30 秒说明项目整体链路，再围绕三个点展开：

1. 网络和协议：为什么用 epoll + 协程，RPC 协议如何处理半包和错误。
2. 分布式调度：MySQL `next_run_at`、Redis 服务发现、分布式锁和锁续约如何协作。
3. 工程化：Docker/测试/Qt/压测/指标如何保证项目可运行、可验证、可演示。
