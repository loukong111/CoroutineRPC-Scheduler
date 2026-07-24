# RPC 协议

Corpcron RPC 基于 TCP，自定义了一个简单的二进制帧格式：

```text
4 bytes total_len | 4 bytes serial_id | protobuf payload
```

- `total_len` 使用大端序，表示 `serial_id + payload` 的总长度。
- 服务端会拒绝格式错误的帧，以及超过 4 MiB 的超大帧。
- TCP 半包通过连接级缓冲区处理，直到收齐完整帧后再进入业务解析。

## IDL 和生成绑定

`proto/rpc.proto` 除了定义请求/响应消息，也声明了 `CorpCronRpc` service：

```protobuf
service CorpCronRpc {
    rpc Echo(EchoRequest) returns (EchoResponse);
    rpc SubmitTask(SubmitTaskRequest) returns (SubmitTaskResponse);
    rpc ExecuteTask(ExecuteTaskRequest) returns (ExecuteTaskResponse);
    // ...
}
```

构建时会执行 [scripts/generate_rpc_bindings.py](../../scripts/generate_rpc_bindings.py)，根据 service 定义生成 [generated/rpc_service.hpp](../../generated/rpc_service.hpp)，包含：

- `RpcMethodDescriptor`：方法名、请求/响应 serial id、请求/响应类型元数据。
- `CorpCronRpcStub`：客户端 typed stub，封装序列化、`RpcClient::call`、响应类型校验和 `RpcError` 解析。
- `CorpCronRpcSkeleton`：服务端 typed skeleton，用于把 unary handler 包装成 `dispatch()`，并把 server-streaming handler 包装成返回完整 `RpcStreamResult` 的 `dispatchStream()`。

现有 `test_submit_client` 和 `bench_client` 已经使用生成的 `CorpCronRpcStub` 发起调用；主服务端仍保留 `RpcDispatcher` 中的业务处理逻辑，后续可以继续把 dispatcher 内部迁移到生成 skeleton 上。

## 消息类型

| Serial ID | Message |
| --- | --- |
| 1 | `EchoRequest` |
| 2 | `EchoResponse` |
| 3 | `SubmitTaskRequest` |
| 4 | `SubmitTaskResponse` |
| 5 | `ExecuteTaskRequest` |
| 6 | `ExecuteTaskResponse` |
| 7 | `CancelTaskRequest` |
| 8 | `CancelTaskResponse` |
| 9 | `ListTasksRequest` |
| 10 | `ListTasksResponse` |
| 11 | `ListHistoryRequest` |
| 12 | `ListHistoryResponse` |
| 13 | `ListServicesRequest` |
| 14 | `ListServicesResponse` |
| 15 | `UpdateTaskRequest` |
| 16 | `UpdateTaskResponse` |
| 17 | `EnableTaskRequest` |
| 18 | `EnableTaskResponse` |
| 19 | `DeleteTaskRequest` |
| 20 | `DeleteTaskResponse` |
| 21 | `RunTaskNowRequest` |
| 22 | `RunTaskNowResponse` |
| 23 | `GetMetricsRequest` |
| 24 | `GetMetricsResponse` |
| 25 | `HealthCheckRequest` |
| 26 | `HealthCheckResponse` |
| 27 | `StreamMetricsRequest` |
| 28 | `StreamMetricsResponse` |
| 100 | `RpcError` |

`ListTasksResponse.TaskInfo` 会返回任务状态、下一次执行时间、重试计数，以及运行中的 `current_execution_id`、`running_node` 和 `started_at`。`ListHistoryResponse.TaskHistoryInfo` 会返回 `execution_id`，用于定位一次具体执行并避免重复历史记录。

`ExecuteTaskRequest` 除任务和 Handler 参数外，还携带 `execution_id` 与 `deadline_unix_ms`。Worker 会拒绝已经过期的请求，并把这两个字段传入 Handler 执行上下文。

`GetMetricsResponse` 会返回 RPC、连接、任务、锁、任务执行耗时和调度延迟指标，其中执行耗时和调度延迟包含 max、p95、p99 与样本总数。

`HealthCheck` 用作标准健康探针，返回当前 RPC 进程的 serving 状态、节点标识和服务端时间。连接池在熔断半开恢复时会先调用该接口，探测通过后再恢复真实业务流量。

`StreamMetrics` 是简化版 server-side streaming 示例。客户端发送一次 `StreamMetricsRequest` 后，服务端会在同一条 TCP 连接上连续返回多个 `StreamMetricsResponse` 帧，最后一帧带 `end_of_stream=true`。生成的 typed stub 使用回调接收每一帧。

## 错误响应

当服务端无法解析或分发请求时，会返回 serial id `100`，payload 为 `RpcError`。

```protobuf
enum ErrorCode {
    OK = 0;
    BAD_REQUEST = 1;
    UNKNOWN_METHOD = 2;
    PAYLOAD_TOO_LARGE = 3;
    INTERNAL_ERROR = 4;
    DB_ERROR = 5;
    HANDLER_NOT_FOUND = 6;
    UNAUTHORIZED = 7;
    DEADLINE_EXCEEDED = 8;
    CANCELED = 9;
    RESOURCE_EXHAUSTED = 10;
    UNAVAILABLE = 11;
}

message RpcError {
    ErrorCode code = 1;
    string message = 2;
}
```

业务响应仍然可以携带业务级失败信息，例如 `SubmitTaskResponse.success=false`。

控制节点和 Worker 会校验方法角色。控制节点不接受 `ExecuteTask`，Worker 不接受任务 CRUD；调用错误角色的方法返回 `UNKNOWN_METHOD`。RPC 执行队列达到上限时返回 `RESOURCE_EXHAUSTED`，节点停机过程中无法继续分发时返回 `UNAVAILABLE`。

## 鉴权

当 `rpc.auth_token` 为空时，鉴权关闭。配置该字段后，客户端必须在请求消息中设置同样的 `auth_token`。协议级鉴权失败会返回 `RpcError{UNAUTHORIZED}`。
