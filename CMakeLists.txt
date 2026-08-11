cmake_minimum_required(VERSION 3.20)

project(tcp_server_client)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

execute_process(
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          ${CMAKE_BINARY_DIR}/compile_commands.json
          ${CMAKE_SOURCE_DIR}/compile_commands.json
)

add_library(tcp_common
  src/UniqueFd.cpp
  include/UniqueFd.hpp
  include/socket.hpp
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
