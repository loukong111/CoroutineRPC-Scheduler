# 文档入口

这里放项目的补充文档。README 只保留快速介绍和常用命令，细节说明放在下面几个文件里。

## 运行和交付

- [演示流程](guide/demo.md)：从启动依赖到 Qt 演示、测试和压测的完整流程。
- [部署说明](guide/deploy.md)：本地运行、Docker 镜像和 systemd 部署方式。
- [生产安全部署](guide/production-security.md)：Nginx stream TLS、metrics 访问控制、systemd 加固和生产检查清单。
- [存储客户端调优](guide/storage-tuning.md)：Redis/MySQL 连接池、超时配置和错误分类。
- [监控栈说明](guide/monitoring.md)：Prometheus、Alertmanager 和 Grafana 本地示例。
- [压测说明](guide/benchmark.md)：压测脚本参数、结果文件、资源快照和 `/metrics` 指标说明。
- [多节点压测](guide/multinode-benchmark.md)：双节点吞吐、锁竞争、故障接管和重复执行保护验证。
- [告警说明](guide/alerting.md)：失败告警规则、阈值配置和 `/alerts` 查询方式。

## 设计和协议

- [架构设计](reference/design.md)：核心组件、调度流程、故障处理和工程边界。
- [RPC 协议](reference/protocol.md)：二进制帧格式、消息类型和协议错误码。

## 项目说明

- [项目能力摘要](project-summary.md)：技术栈、核心能力、亮点和当前边界。
- [项目讲解提纲](walkthrough.md)：按主链路讲清项目设计，适合演示前快速过一遍。
