# 监控栈说明

CorpCron 已经在指标端口暴露 `/metrics` 和 `/alerts`。本目录提供一套本地 Prometheus、Alertmanager 和 Grafana 示例，适合演示长期指标采集、告警规则和可视化面板。

## 前置条件

先启动控制节点和 Worker，并确认指标端口可访问：

```bash
curl http://127.0.0.1:9091/metrics
curl http://127.0.0.1:9091/alerts
curl http://127.0.0.1:9191/metrics
```

双节点演示时，第二个控制节点和 Worker 默认使用 `9092`、`9192`：

```bash
curl http://127.0.0.1:9092/metrics
curl http://127.0.0.1:9192/metrics
```

## 启动监控栈

```bash
docker compose -f deploy/monitoring/docker-compose.monitoring.yml up -d
```

启动后访问：

- Prometheus: <http://127.0.0.1:9090>
- Alertmanager: <http://127.0.0.1:9093>
- Grafana: <http://127.0.0.1:3000>，默认账号密码 `admin/admin`

Grafana 会自动加载 Prometheus 数据源和 `CorpCron Overview` 面板，不需要手工导入 JSON。

## 抓取目标

Prometheus 默认抓取：

- `host.docker.internal:9091`：本机第一个控制节点。
- `host.docker.internal:9191`：本机第一个 Worker。

双节点演示时，可以在 [deploy/monitoring/prometheus/prometheus.yml](../../deploy/monitoring/prometheus/prometheus.yml) 中打开 `9092` 和 `9192` 的抓取配置。Linux Docker 通过 `host-gateway` 把 `host.docker.internal` 映射到宿主机。开发配置将 Metrics 监听在 `0.0.0.0`，生产样例仍限制为 `127.0.0.1`；如果节点部署在其他机器，把 target 和防火墙规则改成实际内网地址即可。

查看 Prometheus 抓取状态：

```text
Prometheus -> Status -> Targets
```

如果 target 显示 down，优先检查：

- 控制节点和 Worker 是否已经启动。
- `metrics.host` 是否允许 Docker 容器访问。生产环境建议仍放在内网或反向代理后面。
- 防火墙是否放行控制节点 `9091/9092` 与 Worker `9191/9192`。

## 告警规则

Prometheus 规则文件位于 [deploy/monitoring/prometheus/rules/corpcron-alerts.yml](../../deploy/monitoring/prometheus/rules/corpcron-alerts.yml)，默认覆盖：

- RPC 错误率过高。
- 任务失败率过高。
- Redis 锁失败率过高。
- 调度延迟 p99 过高。
- 任务执行耗时 p99 过高。
- 坏包和连接限流事件。

Alertmanager 示例默认只在本地页面展示告警，不投递外部通知。生产环境可以在 [deploy/monitoring/alertmanager/alertmanager.yml](../../deploy/monitoring/alertmanager/alertmanager.yml) 中接入 Webhook、企业微信、钉钉或邮件等渠道。

## Grafana 面板

面板文件位于 [deploy/monitoring/grafana/dashboards/corpcron-overview.json](../../deploy/monitoring/grafana/dashboards/corpcron-overview.json)，包含：

- RPC QPS 和错误 QPS。
- 活跃连接数和拒绝连接。
- 任务成功/失败速率。
- Redis 锁获取成功/失败速率。
- 任务执行耗时 p95/p99。
- 调度延迟 p95/p99。
- 网络收发流量。
- 坏包和限流事件。

压测时建议同时打开 Grafana 和压测结果文件，把吞吐、错误、调度延迟和锁竞争放在一起看。

## 停止和清理

停止监控栈：

```bash
docker compose -f deploy/monitoring/docker-compose.monitoring.yml down
```

连同 Prometheus、Alertmanager、Grafana 的本地数据一起清理：

```bash
docker compose -f deploy/monitoring/docker-compose.monitoring.yml down -v
```
