# RPC 协议

Corpcron RPC 基于 TCP，自定义了一个简单的二进制帧格式：

```text
4 bytes total_len | 4 bytes serial_id | protobuf payload
```

- `total_len` 使用大端序，表示 `serial_id + payload` 的总长度。
- 服务端会拒绝格式错误的帧，以及超过 4 MiB 的超大帧。
- TCP 半包通过连接级缓冲区处理，直到收齐完整帧后再进入业务解析。

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
| 100 | `RpcError` |

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
}

message RpcError {
    ErrorCode code = 1;
    string message = 2;
}
```

业务响应仍然可以携带业务级失败信息，例如 `SubmitTaskResponse.success=false`。

## 鉴权

当 `rpc.auth_token` 为空时，鉴权关闭。配置该字段后，客户端必须在请求消息中设置同样的 `auth_token`。协议级鉴权失败会返回 `RpcError{UNAUTHORIZED}`。
