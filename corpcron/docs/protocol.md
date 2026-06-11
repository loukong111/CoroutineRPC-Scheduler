# RPC Protocol

Corpcron RPC uses a simple frame format over TCP:

```text
4 bytes total_len | 4 bytes serial_id | protobuf payload
```

- `total_len` is big-endian and includes `serial_id + payload`.
- The server rejects malformed frames and frames larger than 4 MiB.
- TCP fragmentation is handled by buffering until a full frame is available.

## Serial IDs

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

## Error Response

When the server cannot parse or dispatch a request, it returns serial id `100` with `RpcError`.

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

Business responses may still carry business-level failure fields, such as `SubmitTaskResponse.success=false`.

## Auth

When `rpc.auth_token` is empty, auth is disabled. When configured, clients must set `auth_token` on request messages. Protocol-level auth failure returns `RpcError{UNAUTHORIZED}`.
