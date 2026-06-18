#include "corpcron/rpc/rpc_client.hpp"
#include "corpcron/rpc/protocol.hpp"
#include "rpc.pb.h"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? std::stoi(argv[2]) : 8081;

    corpcron::rpc::SubmitTaskRequest req;
    req.set_cron_expr("* * * * * ?");
    req.set_params("Hello from test client");
    req.set_handler("Echo");
    if (const char* token = std::getenv("CORPCRON_RPC_AUTH_TOKEN")) {
        req.set_auth_token(token);
    }

    std::string payload;
    req.SerializeToString(&payload);

    corpcron::RpcClient client(host, port);
    uint32_t response_serial_id = 0;
    std::string resp_payload;
    if (!client.call(corpcron::rpc::kSubmitTaskRequestSerialId, payload, response_serial_id, resp_payload, 3000)) {
        std::cerr << "SubmitTask RPC call failed" << std::endl;
        return 1;
    }

    if (response_serial_id == corpcron::rpc::kRpcErrorSerialId) {
        corpcron::rpc::RpcError error;
        if (error.ParseFromString(resp_payload)) {
            std::cerr << "RPC error " << error.code() << ": " << error.message() << std::endl;
        }
        return 1;
    }

    corpcron::rpc::SubmitTaskResponse resp;
    if (!resp.ParseFromString(resp_payload)) {
        std::cerr << "Failed to parse SubmitTaskResponse" << std::endl;
        return 1;
    }

    std::cout << "Success=" << resp.success()
              << ", TaskID=" << resp.task_id()
              << ", Error=" << resp.error() << std::endl;
    return resp.success() ? 0 : 1;
}
