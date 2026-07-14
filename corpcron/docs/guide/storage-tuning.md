# 存储客户端调优

CorpCron 的 Redis/MySQL 客户端已经支持连接池、超时配置和基础错误分类。目标不是替代成熟客户端库的所有能力，而是避免单连接串行阻塞，并让短暂网络抖动、超时、认证错误和 SQL 错误在日志中更容易定位。

## Redis

配置项：

```ini
[redis]
host = 127.0.0.1
port = 6380
pool_size = 4
connect_timeout_ms = 1000
command_timeout_ms = 1000
```

说明：

- `pool_size`：内部维护的 hiredis 连接数。服务注册、心跳、服务发现、分布式锁和锁续约会轮询使用这些连接。
- `connect_timeout_ms`：建立 Redis 连接的超时时间。
- `command_timeout_ms`：单条 Redis 命令读写超时时间。

Redis 命令失败时，客户端会对当前连接执行一次重连和重试，并记录 `lastError()`。日志中的 `kind` 可能是：

```text
connection
timeout
protocol
unknown
```

## MySQL

配置项：

```ini
[mysql]
host = 127.0.0.1
port = 3307
user = corpcron
password = corpcron_dev_password
database = corpcron
pool_size = 4
connect_timeout_sec = 3
read_timeout_sec = 5
write_timeout_sec = 5
reconnect = 1
```

说明：

- `pool_size`：内部维护的 MySQL 连接数。任务查询、状态机更新、执行历史写入等操作会轮询租用连接。
- `connect_timeout_sec`：建立连接超时。
- `read_timeout_sec`：读取结果超时。
- `write_timeout_sec`：写入请求超时。
- `reconnect`：连接断开后允许 Connector/C++ 自动重连。

MySQL 异常会按 SQLState、错误码和错误文本分类，常见 `kind`：

```text
authentication
duplicate_key
connection
timeout
query
```

例如密码错误通常会记录为 `authentication`，唯一键冲突会记录为 `duplicate_key`，连接断开或 MySQL 不可达会记录为 `connection`。

## 推荐值

本地开发：

```ini
redis.pool_size = 4
mysql.pool_size = 4
```

单机演示或轻量部署：

```ini
redis.pool_size = 4
mysql.pool_size = 4
redis.command_timeout_ms = 1000
mysql.read_timeout_sec = 5
mysql.write_timeout_sec = 5
```

多节点压测或较高并发：

```ini
redis.pool_size = 8
mysql.pool_size = 8
redis.command_timeout_ms = 1000
mysql.read_timeout_sec = 5
mysql.write_timeout_sec = 5
```

不要盲目把连接池调得很大。MySQL/Redis 连接数最终会消耗服务端资源，池大小应结合节点数、线程池大小和数据库最大连接数一起设置。

## 排查思路

- `kind=authentication`：检查账号、密码、权限和环境变量覆盖。
- `kind=connection`：检查服务是否运行、端口、防火墙、Docker 映射和 DNS。
- `kind=timeout`：检查 Redis/MySQL 负载、慢查询、网络抖动和超时阈值是否过小。
- `kind=duplicate_key`：通常是幂等写入或唯一约束触发，结合业务日志确认是否符合预期。
- `kind=query`：检查 SQL、表结构、字段类型和 schema bootstrap 日志。
