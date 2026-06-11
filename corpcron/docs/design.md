# Design

## Goals

Corpcron is designed as a C++ backend practice project for distributed scheduled tasks. The focus is not to replace mature schedulers, but to show clear engineering decisions around networking, persistence, service discovery, failure handling, testing, and deployment.

## Components

```text
Client
  |
  | TCP + custom RPC frame
  v
TcpServer
  |
  | SubmitTask / CancelTask / ExecuteTask
  v
MySQL <---- TaskScheduler ----> Redis
                         |
                         | discover rpc endpoints
                         v
                    RpcClient -> TcpServer
```

## RPC

The network layer uses epoll and coroutine awaitables. Each request is framed as:

```text
4 bytes total_len | 4 bytes serial_id | protobuf payload
```

The server validates frame length, handles partial frames, rejects malformed frames, and returns `RpcError` for protocol-level errors.

If `rpc.auth_token` is configured, every RPC request must carry the same token in the request message. This is intentionally simple: it is enough for private-network demos, but production exposure would still require TLS, stronger auth, and rate limiting.

## Scheduling

Tasks are persisted in MySQL. The scheduler queries due tasks by `next_run_at`, attempts to acquire a Redis lock, then dispatches execution through RPC.

Execution result is recorded in `task_history`. After each execution:

- Success resets `retry_count` and computes the next cron time.
- Failure increments `retry_count` and schedules a retry with exponential backoff.
- If `retry_count >= max_retries`, the task is disabled.

## Locking

Redis locks use an owner value and Lua-safe unlock. During task execution the scheduler renews the lock periodically, reducing duplicate execution risk for long-running tasks.

Tasks should still be implemented as idempotent handlers. Lock renewal reduces risk, but cannot eliminate all distributed failure cases, such as network partitions or process pauses.

## Misfire

When a task is overdue after downtime:

- `once`: run it once as soon as possible.
- `skip`: if it is overdue by more than `scheduler.misfire_grace_seconds`, skip the missed run and advance to the next cron time.

The default is `once`, which is easier to reason about for demos.

## Persistence

`tasks` stores task definition and runtime metadata:

- `status`
- `next_run_at`
- `last_run_at`
- `retry_count`
- `max_retries`

`task_history` stores execution audit records.

The service also performs lightweight schema bootstrap on startup to make local demos resilient to older tables.

## Testing

Test layers:

- Protocol unit test: frame parsing edge cases.
- Integration test: Redis service discovery, lock renewal, MySQL task/history operations.
- E2E scheduler test: starts a local TCP server and scheduler, inserts a due task, then waits for execution history.

Integration and E2E tests are skipped by default and enabled with:

```bash
CORPCRON_RUN_INTEGRATION_TESTS=1 ctest --test-dir build --output-on-failure
```

## Known Limits

- No TLS.
- Token auth is basic and suitable only for private/internal demos.
- Handlers are in-process registered functions, not sandboxed jobs.
- No dashboard or task editing API beyond submit/cancel.
- No production-grade metrics yet.
