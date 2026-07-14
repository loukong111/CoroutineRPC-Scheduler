# 生产部署安全示例

CorpCron 的 RPC 协议是自定义 TCP 帧，不是 HTTP。生产部署时建议采用下面的边界：

- `corpcron_server` 只监听 `127.0.0.1:8081`，不直接暴露公网。
- Nginx `stream` 模块监听公网 TLS 端口，例如 `8443`，再转发到本机 `127.0.0.1:8081`。
- RPC 仍然启用 `CORPCRON_RPC_AUTH_TOKEN`，TLS 只解决链路加密，不替代业务鉴权。
- `/metrics`、`/alerts`、`/health` 默认只监听 `127.0.0.1:9091`，如果要给监控系统访问，通过 Nginx HTTPS + IP allowlist 或内网抓取。
- MySQL/Redis 不暴露公网，只允许 CorpCron 节点所在内网访问。

## 1. 服务端配置

生产示例配置见 [server.production.example.conf](../../config/server.production.example.conf)。核心差异是：

```ini
[server]
bind_host = 127.0.0.1
listen_port = 8081
max_connections = 2048

[metrics]
host = 127.0.0.1
port = 9091
```

敏感信息不要写进配置文件，放到 `/etc/corpcron/corpcron.env`：

```bash
sudo install -d -m 750 -o root -g corpcron /etc/corpcron
sudo cp deploy/corpcron.env.example /etc/corpcron/corpcron.env
sudo chmod 600 /etc/corpcron/corpcron.env
sudo editor /etc/corpcron/corpcron.env
```

至少修改：

```bash
CORPCRON_RPC_AUTH_TOKEN=change_me_to_a_long_random_token
CORPCRON_MYSQL_PASSWORD=change_me
CORPCRON_SERVER_ADVERTISE_HOST=127.0.0.1
```

如果 Nginx 和 CorpCron 在同一台机器上，`advertise_host` 可以继续用 `127.0.0.1`。如果多个 CorpCron 节点需要互相调用，应设置成节点间可达的内网地址，并用防火墙限制访问来源。

## 2. systemd

安装二进制和配置：

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin corpcron
sudo mkdir -p /opt/corpcron/bin /etc/corpcron
sudo cp build/corpcron_server /opt/corpcron/bin/
sudo cp config/server.production.example.conf /etc/corpcron/server.conf
sudo cp systemd/corpcron.service /etc/systemd/system/
```

启动：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now corpcron
sudo systemctl status corpcron
```

本机验证：

```bash
curl http://127.0.0.1:9091/health
curl http://127.0.0.1:9091/alerts
```

## 3. Nginx RPC TLS 代理

安装 Nginx 并确认支持 stream 模块：

```bash
nginx -V 2>&1 | grep -- --with-stream
```

将 [corpcron-stream.conf](../../deploy/nginx/corpcron-stream.conf) 合并到 Nginx 配置。注意很多发行版的 `/etc/nginx/conf.d/*.conf` 默认只在 `http {}` 中 include，`stream {}` 需要放在顶层，或者在 `/etc/nginx/nginx.conf` 顶层增加：

```nginx
include /etc/nginx/stream-conf.d/*.conf;
```

然后：

```bash
sudo mkdir -p /etc/nginx/stream-conf.d
sudo cp deploy/nginx/corpcron-stream.conf /etc/nginx/stream-conf.d/corpcron-stream.conf
sudo nginx -t
sudo systemctl reload nginx
```

证书路径默认使用 Let’s Encrypt：

```text
/etc/letsencrypt/live/corpcron.example.com/fullchain.pem
/etc/letsencrypt/live/corpcron.example.com/privkey.pem
```

本地自测可以先用自签证书，但生产环境应使用可信 CA 证书。

## 4. Metrics/Alerts HTTPS 代理

如果监控系统不在同机，可以使用 [corpcron-metrics.conf](../../deploy/nginx/corpcron-metrics.conf)：

```bash
sudo cp deploy/nginx/corpcron-metrics.conf /etc/nginx/conf.d/corpcron-metrics.conf
sudo nginx -t
sudo systemctl reload nginx
```

这个示例默认只允许私有网段访问：

```nginx
allow 10.0.0.0/8;
allow 172.16.0.0/12;
allow 192.168.0.0/16;
deny all;
```

如果暴露给 Prometheus，建议固定 Prometheus 出口 IP，并开启 `auth_basic`。不要把 `/metrics` 和 `/alerts` 无保护地暴露到公网。

## 5. 防火墙建议

单机部署建议：

```text
允许入站: 8443/tcp    Nginx RPC TLS
允许入站: 9443/tcp    可选，仅监控网络访问
拒绝入站: 8081/tcp    CorpCron 后端 RPC 只给本机
拒绝入站: 9091/tcp    Metrics 后端只给本机
拒绝入站: 3306/tcp    MySQL
拒绝入站: 6379/tcp    Redis
```

多节点部署时，节点之间如果需要直接互相调用 RPC，应只在内网安全组中放通节点间端口，不要对全网开放。

## 6. 生产检查清单

- `server.bind_host` 使用 `127.0.0.1` 或内网地址，避免 `0.0.0.0` 裸露。
- 设置强随机 `CORPCRON_RPC_AUTH_TOKEN`。
- MySQL/Redis 使用独立账号和强密码，不使用默认演示密码。
- Nginx TLS 证书有效，禁用过旧 TLS 协议。
- `/metrics` 和 `/alerts` 只允许监控网络访问。
- systemd 使用 `corpcron` 非 root 用户运行。
- 防火墙或安全组只开放必要端口。
- 日志中不要输出真实 token 和数据库密码。
