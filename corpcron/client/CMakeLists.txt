cmake_minimum_required(VERSION 3.20)
project(corpcron_client)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)  
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(Protobuf REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/../include)
include_directories(${CMAKE_SOURCE_DIR}/../src)
include_directories(${Protobuf_INCLUDE_DIRS})


add_executable(corpcron_client
    main.cpp
    mainwindow.h
    mainwindow.cpp
    ../src/rpc.pb.cc
)

target_link_libraries(corpcron_client
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    ${Protobuf_LIBRARIES}
)