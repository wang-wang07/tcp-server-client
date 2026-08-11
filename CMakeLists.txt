cmake_minimum_required(VERSION 3.20)

project(tcp_server_client)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tcp_common
  src/UniqueFd.cpp
)

target_include_directories(tcp_common
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

add_executable(server
  src/server.cpp
)

target_link_libraries(server
  PRIVATE
    tcp_common
)

add_executable(client
  src/client.cpp
)

target_link_libraries(client
  PRIVATE
    tcp_common
)
