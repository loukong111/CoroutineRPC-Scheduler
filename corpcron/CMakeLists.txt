cmake_minimum_required(VERSION 3.20)
project(corpcron)
enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -O2 -g -fcoroutines")

# 查找 Protobuf
find_package(Protobuf REQUIRED)

# 头文件路径
include_directories(include)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/generated)
include_directories(${Protobuf_INCLUDE_DIRS})

# 复制配置文件
file(COPY ${PROJECT_SOURCE_DIR}/config DESTINATION ${PROJECT_BINARY_DIR})

set(CORPCRON_CORE_SOURCES
    src/common/logger.cpp
    src/common/config.cpp
    src/common/thread_pool.cpp
    src/metrics/metrics.cpp
    src/net/epoll_loop.cpp
    src/net/tcp_server.cpp
    src/redis/redis_client.cpp
    src/mysql/mysql_client.cpp
    src/rpc/handler_registry.cpp
    src/rpc/rpc_dispatcher.cpp
    src/scheduler/cron_parser.cpp
    src/scheduler/task_scheduler.cpp
    generated/rpc.pb.cc
    src/rpc/rpc_client.cpp
)

add_library(corpcron_core STATIC ${CORPCRON_CORE_SOURCES})
target_include_directories(corpcron_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/generated
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(corpcron_core PUBLIC
    ${Protobuf_LIBRARIES}
    pthread
    hiredis
    mysqlcppconn
)

# 主服务器程序
add_executable(corpcron_server
    src/main.cpp
    src/app/server_app.cpp
)

target_link_libraries(corpcron_server
    corpcron_core
)

# Qt 客户端（可选，如果 Qt6 未安装则跳过）
find_package(Qt6 QUIET COMPONENTS Core Widgets Network)
if(Qt6_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/client/CMakeLists.txt)
    add_subdirectory(client)
endif()

# 可选：安装
install(TARGETS corpcron_server DESTINATION bin)

add_executable(test_submit_client
    tools/test_submit_client.cpp
)
target_link_libraries(test_submit_client corpcron_core)

add_executable(bench_client
    tools/bench_client.cpp
)
target_link_libraries(bench_client corpcron_core)

add_executable(corpcron_protocol_test
    tests/protocol_test.cpp
)
target_include_directories(corpcron_protocol_test PRIVATE include)
add_test(NAME protocol_test COMMAND corpcron_protocol_test)

add_executable(corpcron_rpc_client_test
    tests/rpc_client_test.cpp
)
target_link_libraries(corpcron_rpc_client_test corpcron_core)
add_test(NAME rpc_client_test COMMAND corpcron_rpc_client_test)
set_tests_properties(rpc_client_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(corpcron_rpc_dispatcher_test
    tests/rpc_dispatcher_test.cpp
)
target_link_libraries(corpcron_rpc_dispatcher_test corpcron_core)
add_test(NAME rpc_dispatcher_test COMMAND corpcron_rpc_dispatcher_test)

add_executable(corpcron_integration_test
    tests/integration_test.cpp
)
target_link_libraries(corpcron_integration_test corpcron_core)
add_test(NAME integration_test COMMAND corpcron_integration_test)
set_tests_properties(integration_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(corpcron_e2e_scheduler_test
    tests/e2e_scheduler_test.cpp
)
target_link_libraries(corpcron_e2e_scheduler_test corpcron_core)
add_test(NAME e2e_scheduler_test COMMAND corpcron_e2e_scheduler_test)
set_tests_properties(e2e_scheduler_test PROPERTIES SKIP_RETURN_CODE 77)
