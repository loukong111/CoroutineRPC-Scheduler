# 部署说明

## 本地依赖环境

```bash
docker compose up -d
cmake -S . -B build
cmake --build build -j
./build/corpcron_server --config config/server.conf
```

本地 Compose 会将 Redis 映射到宿主机 `6380`、MySQL 映射到宿主机 `3307`，避免占用已有的 `6379` 和 `3306` 服务。

## 容器镜像

```bash
docker build -t corpcron:local .
docker run --rm -p 8081:8081 \
  -e CORPCRON_REDIS_HOST=host.docker.internal \
  -e CORPCRON_MYSQL_HOST=host.docker.internal \
  -e CORPCRON_MYSQL_PASSWORD=corpcron_dev_password \
  corpcron:local
```

在 Linux 环境下，如果 `host.docker.internal` 不可用，可以替换成宿主机可访问的地址，或者让服务和 MySQL/Redis 运行在同一个 Compose 网络中。

## systemd

安装二进制文件和配置：

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin corpcron
sudo mkdir -p /opt/corpcron/bin /etc/corpcron
sudo cp build/corpcron_server /opt/corpcron/bin/
sudo cp config/server.example.conf /etc/corpcron/server.conf
sudo cp systemd/corpcron.service /etc/systemd/system/
```

将敏感配置放到 `/etc/corpcron/corpcron.env`：

```bash
CORPCRON_MYSQL_PASSWORD=your_password
CORPCRON_SERVER_ADVERTISE_HOST=10.0.0.12
```

启动服务：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now corpcron
sudo journalctl -u corpcron -f
```
