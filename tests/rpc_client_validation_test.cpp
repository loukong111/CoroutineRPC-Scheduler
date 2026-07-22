#include "corpcron/rpc/protocol.hpp"
#include "corpcron/rpc/rpc_client.hpp"
#include "corpcron/rpc/rpc_client_pool.hpp"
#include <cassert>
#include <string>

int main() {
    uint32_t response_serial_id = 12345;
    std::string response_payload = "stale response";
    corpcron::RpcClient invalid_client("", 0);
    assert(!invalid_client.call(corpcron::rpc::kEchoRequestSerialId, "",
                                response_serial_id, response_payload, 100));
    assert(response_serial_id == 0);
    assert(response_payload.empty());
    assert(invalid_client.lastStatus() == corpcron::RpcCallStatus::ConnectFailed);

    corpcron::RpcClientPool empty_pool;
    response_serial_id = 12345;
    response_payload = "stale response";
    std::string selected_endpoint = "stale endpoint";
    std::string error;
    assert(!empty_pool.call({}, corpcron::rpc::kEchoRequestSerialId, "",
                            response_serial_id, response_payload, 100,
                            &selected_endpoint, &error));
    assert(response_serial_id == 0);
    assert(response_payload.empty());
    assert(selected_endpoint.empty());
    assert(error == "no endpoint available");
    return 0;
}
