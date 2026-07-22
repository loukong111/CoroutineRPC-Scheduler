#include "corpcron/rpc/rpc_client.hpp"
#include "rpc_service.hpp"
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
    std::string auth_token;
    if (const char* token = std::getenv("CORPCRON_RPC_AUTH_TOKEN")) auth_token = token;

    corpcron::RpcClient client(host, port);
    corpcron::rpc::CorpCronRpcStub stub(client, auth_token);
    corpcron::rpc::SubmitTaskResponse resp;
    std::string error;
    if (!stub.SubmitTask(req, resp, &error, 3000)) {
        std::cerr << "SubmitTask RPC call failed: " << error << std::endl;
        return 1;
    }

    std::cout << "Success=" << resp.success()
              << ", TaskID=" << resp.task_id()
              << ", Error=" << resp.error() << std::endl;
    return resp.success() ? 0 : 1;
}
