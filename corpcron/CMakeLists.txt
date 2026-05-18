cmake_minimum_required(VERSION 3.20)
project(corpcron)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -O2 -g -fcoroutines")

# 查找 Protobuf
find_package(Protobuf REQUIRED)

# 头文件路径
include_directories(include)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${Protobuf_INCLUDE_DIRS})

# 复制配置文件
file(COPY ${PROJECT_SOURCE_DIR}/config DESTINATION ${PROJECT_BINARY_DIR})

# 主服务器程序
add_executable(corpcron_server
    src/main.cpp
    src/common/logger.cpp
    src/common/config.cpp
    src/common/memory_pool.cpp
    src/common/thread_pool.cpp
    src/net/epoll_loop.cpp
    src/net/tcp_server.cpp
    src/redis/redis_client.cpp
    src/mysql/mysql_client.cpp
    src/rpc/handler_registry.cpp
    src/scheduler/cron_parser.cpp
    src/scheduler/task_scheduler.cpp
    src/rpc.pb.cc
    src/rpc/rpc_client.cpp
)

target_link_libraries(corpcron_server
    ${Protobuf_LIBRARIES}
    pthread
    hiredis
    mysqlcppconn
)

# Qt 客户端（可选，如果 Qt6 未安装则跳过）
find_package(Qt6 QUIET COMPONENTS Core Widgets Network)
if(Qt6_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/client/CMakeLists.txt)
    add_subdirectory(client)
endif()

# 可选：安装
install(TARGETS corpcron_server DESTINATION bin)

add_executable(test_submit_client
    src/test_submit_client.cpp
    src/rpc.pb.cc
)
target_link_libraries(test_submit_client ${Protobuf_LIBRARIES} pthread)