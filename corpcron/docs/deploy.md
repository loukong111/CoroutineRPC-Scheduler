# Deployment

## Local Dependencies

```bash
docker compose up -d
cmake -S . -B build
cmake --build build -j
./build/corpcron_server --config config/server.conf
```

## Container Image

```bash
docker build -t corpcron:local .
docker run --rm -p 8081:8081 \
  -e CORPCRON_REDIS_HOST=host.docker.internal \
  -e CORPCRON_MYSQL_HOST=host.docker.internal \
  -e CORPCRON_MYSQL_PASSWORD=corpcron_dev_password \
  corpcron:local
```

On Linux, replace `host.docker.internal` with a reachable host address or run the service in the same Compose network.

## systemd

Install binary and config:

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin corpcron
sudo mkdir -p /opt/corpcron/bin /etc/corpcron
sudo cp build/corpcron_server /opt/corpcron/bin/
sudo cp config/server.example.conf /etc/corpcron/server.conf
sudo cp systemd/corpcron.service /etc/systemd/system/
```

Put secrets in `/etc/corpcron/corpcron.env`:

```bash
CORPCRON_MYSQL_PASSWORD=your_password
CORPCRON_SERVER_ADVERTISE_HOST=10.0.0.12
```

Start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now corpcron
sudo journalctl -u corpcron -f
```
