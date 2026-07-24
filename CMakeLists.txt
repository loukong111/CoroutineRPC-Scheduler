cmake_minimum_required(VERSION 3.20)
project(corpcron LANGUAGES CXX)
enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# 查找 Protobuf
find_package(Protobuf REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 头文件路径
include_directories(include)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/generated)
include_directories(${Protobuf_INCLUDE_DIRS})

# 复制配置文件
file(COPY ${PROJECT_SOURCE_DIR}/config DESTINATION ${PROJECT_BINARY_DIR})

set(CORPCRON_RPC_SERVICE_HEADER ${PROJECT_SOURCE_DIR}/generated/rpc_service.hpp)
add_custom_command(
    OUTPUT ${CORPCRON_RPC_SERVICE_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/generate_rpc_bindings.py
    DEPENDS
        ${PROJECT_SOURCE_DIR}/proto/rpc.proto
        ${PROJECT_SOURCE_DIR}/scripts/generate_rpc_bindings.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generating CorpCron RPC typed bindings"
    VERBATIM
)
add_custom_target(corpcron_rpc_bindings DEPENDS ${CORPCRON_RPC_SERVICE_HEADER})

set(CORPCRON_CORE_SOURCES
    src/common/logger.cpp
    src/common/config.cpp
    src/common/thread_pool.cpp
    src/common/storage_error.cpp
    src/metrics/alert_rules.cpp
    src/metrics/metrics.cpp
    src/metrics/metrics_exporter.cpp
    src/net/epoll_loop.cpp
    src/net/tcp_server.cpp
    src/redis/redis_client.cpp
    src/mysql/mysql_client.cpp
    src/rpc/handler_registry.cpp
    src/rpc/rpc_dispatcher.cpp
    src/rpc/rpc_interceptor.cpp
    src/rpc/rpc_client_pool.cpp
    src/scheduler/cron_parser.cpp
    src/scheduler/task_cancellation_registry.cpp
    src/scheduler/task_scheduler.cpp
    src/worker/builtin_handlers.cpp
    generated/rpc.pb.cc
    src/rpc/rpc_client.cpp
)

add_library(corpcron_core STATIC ${CORPCRON_CORE_SOURCES})
add_dependencies(corpcron_core corpcron_rpc_bindings)
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
add_test(NAME server_cli_invalid_test COMMAND corpcron_server --bad-option)
set_tests_properties(server_cli_invalid_test PROPERTIES WILL_FAIL TRUE)

# 独立任务执行节点
add_executable(corpcron_worker
    src/worker_main.cpp
    src/app/worker_app.cpp
)
target_link_libraries(corpcron_worker corpcron_core)
add_test(NAME worker_cli_invalid_test COMMAND corpcron_worker --bad-option)
set_tests_properties(worker_cli_invalid_test PROPERTIES WILL_FAIL TRUE)

# Qt 客户端（可选，如果 Qt6 未安装则跳过）
find_package(Qt6 QUIET COMPONENTS Core Widgets Network)
if(Qt6_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/client/CMakeLists.txt)
    add_subdirectory(client)
endif()

# 可选：安装
install(TARGETS corpcron_server corpcron_worker DESTINATION bin)

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

add_executable(corpcron_rpc_client_pool_test
    tests/rpc_client_pool_test.cpp
)
target_link_libraries(corpcron_rpc_client_pool_test corpcron_core)
add_test(NAME rpc_client_pool_test COMMAND corpcron_rpc_client_pool_test)
set_tests_properties(rpc_client_pool_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(corpcron_rpc_client_validation_test
    tests/rpc_client_validation_test.cpp
)
target_link_libraries(corpcron_rpc_client_validation_test corpcron_core)
add_test(NAME rpc_client_validation_test COMMAND corpcron_rpc_client_validation_test)

add_executable(corpcron_rpc_dispatcher_test
    tests/rpc_dispatcher_test.cpp
)
target_link_libraries(corpcron_rpc_dispatcher_test corpcron_core)
add_test(NAME rpc_dispatcher_test COMMAND corpcron_rpc_dispatcher_test)

add_executable(corpcron_rpc_interceptor_test
    tests/rpc_interceptor_test.cpp
)
target_link_libraries(corpcron_rpc_interceptor_test corpcron_core)
add_test(NAME rpc_interceptor_test COMMAND corpcron_rpc_interceptor_test)

add_executable(corpcron_rpc_service_bindings_test
    tests/rpc_service_bindings_test.cpp
)
target_link_libraries(corpcron_rpc_service_bindings_test corpcron_core)
add_test(NAME rpc_service_bindings_test COMMAND corpcron_rpc_service_bindings_test)
set_tests_properties(rpc_service_bindings_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(corpcron_metrics_test
    tests/metrics_test.cpp
)
target_link_libraries(corpcron_metrics_test corpcron_core)
add_test(NAME metrics_test COMMAND corpcron_metrics_test)

add_executable(corpcron_alert_rules_test
    tests/alert_rules_test.cpp
)
target_link_libraries(corpcron_alert_rules_test corpcron_core)
add_test(NAME alert_rules_test COMMAND corpcron_alert_rules_test)

add_executable(corpcron_storage_error_test
    tests/storage_error_test.cpp
)
target_link_libraries(corpcron_storage_error_test corpcron_core)
add_test(NAME storage_error_test COMMAND corpcron_storage_error_test)

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

add_executable(corpcron_thread_pool_test
    tests/thread_pool_test.cpp
)
target_link_libraries(corpcron_thread_pool_test corpcron_core)
add_test(NAME thread_pool_test COMMAND corpcron_thread_pool_test)

add_executable(corpcron_epoll_loop_test
    tests/epoll_loop_test.cpp
)
target_link_libraries(corpcron_epoll_loop_test corpcron_core)
add_test(NAME epoll_loop_test COMMAND corpcron_epoll_loop_test)

add_executable(corpcron_config_test
    tests/config_test.cpp
)
target_link_libraries(corpcron_config_test corpcron_core)
add_test(NAME config_test COMMAND corpcron_config_test)

add_executable(corpcron_cron_parser_test
    tests/cron_parser_test.cpp
)
target_link_libraries(corpcron_cron_parser_test corpcron_core)
add_test(NAME cron_parser_test COMMAND corpcron_cron_parser_test)

add_executable(corpcron_task_cancellation_registry_test
    tests/task_cancellation_registry_test.cpp
)
target_link_libraries(corpcron_task_cancellation_registry_test corpcron_core)
add_test(NAME task_cancellation_registry_test COMMAND corpcron_task_cancellation_registry_test)

add_executable(corpcron_tcp_server_test
    tests/tcp_server_test.cpp
)
target_link_libraries(corpcron_tcp_server_test corpcron_core)
add_test(NAME tcp_server_test COMMAND corpcron_tcp_server_test)
set_tests_properties(tcp_server_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(corpcron_metrics_exporter_test
    tests/metrics_exporter_test.cpp
)
target_link_libraries(corpcron_metrics_exporter_test corpcron_core)
add_test(NAME metrics_exporter_test COMMAND corpcron_metrics_exporter_test)
set_tests_properties(metrics_exporter_test PROPERTIES SKIP_RETURN_CODE 77)

# Most tests use assert(). Keep assertions active even for Release and
# RelWithDebInfo builds so a green CTest run always executes the checks.
set(CORPCRON_TEST_TARGETS
    corpcron_protocol_test
    corpcron_rpc_client_test
    corpcron_rpc_client_pool_test
    corpcron_rpc_client_validation_test
    corpcron_rpc_dispatcher_test
    corpcron_rpc_interceptor_test
    corpcron_rpc_service_bindings_test
    corpcron_metrics_test
    corpcron_alert_rules_test
    corpcron_storage_error_test
    corpcron_integration_test
    corpcron_e2e_scheduler_test
    corpcron_thread_pool_test
    corpcron_epoll_loop_test
    corpcron_config_test
    corpcron_cron_parser_test
    corpcron_task_cancellation_registry_test
    corpcron_tcp_server_test
    corpcron_metrics_exporter_test
)
foreach(test_target IN LISTS CORPCRON_TEST_TARGETS)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()
