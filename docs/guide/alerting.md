# 告警说明

CorpCron 内置了一组轻量级失败告警规则。服务端启动后，除了 `/metrics` 和 `/health`，还会在同一个指标端口暴露 `/alerts`：

```bash
curl http://127.0.0.1:9091/alerts
```

健康时返回类似：

```text
# CorpCron Alert Status
status=ok
alerts_firing=0
```

触发告警时返回当前触发的规则名、级别、观测值和阈值：

```text
# CorpCron Alert Status
status=firing
alerts_firing=1
alert name="task_failure_rate_high" severity="warning" observed=40 threshold=20 message="Task failure rate is above threshold"
```

## 默认规则

| 规则名 | 含义 | 默认阈值 |
| --- | --- | --- |
| `rpc_error_rate_high` | RPC 错误率过高 | RPC 请求数不少于 100，错误率 >= 5% |
| `task_failure_rate_high` | 任务执行失败率过高 | 任务执行数不少于 10，失败率 >= 20% |
| `lock_failure_rate_high` | Redis 分布式锁竞争或 Redis 异常偏高 | 锁请求数不少于 10，失败率 >= 50% |
| `schedule_delay_p99_high` | 调度延迟 p99 过高 | 最近样本 p99 >= 5000 ms |
| `task_duration_p99_high` | 任务执行耗时 p99 过高 | 最近样本 p99 >= 10000 ms |

这些规则主要用于演示和本地排查：它们不会替代生产级告警治理，但能把“任务失败、锁竞争、调度堆积、任务耗时异常”这几类核心风险直接暴露出来。

## 配置阈值

阈值在配置文件的 `[alerts]` 段中调整：

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

本地演示时可以把 `min_*` 调小，更容易看到告警触发；压测或长时间运行时建议保留最小样本门槛，避免少量请求造成误报。

## 和 Prometheus / Alertmanager 配合

`/alerts` 适合快速查看当前告警状态；长期趋势仍建议抓取 `/metrics`：

```bash
curl http://127.0.0.1:9091/metrics
```

项目已经提供 Prometheus、Alertmanager 和 Grafana 本地示例，见 [监控栈说明](monitoring.md)。Prometheus 告警规则位于 [deploy/monitoring/prometheus/rules/corpcron-alerts.yml](../../deploy/monitoring/prometheus/rules/corpcron-alerts.yml)，可以直接基于这些指标调整阈值，例如：

```text
corpcron_rpc_error_total / corpcron_rpc_requests_total > 0.05
corpcron_task_failure_total / (corpcron_task_success_total + corpcron_task_failure_total) > 0.2
corpcron_schedule_delay_p99_ms > 5000
corpcron_task_duration_p99_ms > 10000
```

内置 `/alerts` 的价值是项目演示和单机排障时不用先部署完整监控栈；Prometheus/Alertmanager/Grafana 示例则用于展示长期采集、趋势面板和外部告警链路的接入方式。
