# Benchmark

`bench_client` sends concurrent Echo RPC requests and reports QPS plus p50/p95/p99 latency.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

Start the server:

```bash
CORPCRON_MYSQL_PASSWORD=your_password ./build/corpcron_server --config config/server.conf
```

Run benchmark:

```bash
./build/bench_client 127.0.0.1 8081 16 1000
```

Arguments:

```text
host port concurrency requests
```

If RPC auth is enabled:

```bash
CORPCRON_RPC_AUTH_TOKEN=your_token ./build/bench_client 127.0.0.1 8081 16 1000
```

## Example Output

```text
requests=1000 concurrency=16 success=1000 failure=0 elapsed_sec=... qps=... p50_ms=... p95_ms=... p99_ms=...
```

Local smoke test on this workspace:

```text
requests=40 concurrency=4 success=40 failure=0 elapsed_sec=0.008932 qps=4478.28 p50_ms=0 p95_ms=1 p99_ms=2
```

## Notes

This benchmark creates one TCP connection per request. It is useful for smoke testing and regression comparison, but it does not represent the maximum throughput of a persistent-connection RPC server.
