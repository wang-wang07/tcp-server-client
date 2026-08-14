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

function(target_enable_warnings target)
  target_compile_options(${target}
    PRIVATE
      $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic -Werror>
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
  )
endfunction()

add_library(tcp_common
  src/UniqueFd.cpp
  src/parser.cpp
  src/socket.cpp
  src/store.cpp
  src/executor.cpp
  include/UniqueFd.hpp
  include/protocol/command.hpp
  include/protocol/parser.hpp
  include/protocol/executor.hpp
  include/socket.hpp
  include/store.hpp
)

target_include_directories(tcp_common
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_enable_warnings(tcp_common)

add_executable(server
  src/server.cpp
)

target_link_libraries(server
  PRIVATE
    tcp_common
)

target_enable_warnings(server)

add_executable(client
  src/client.cpp
)

target_link_libraries(client
  PRIVATE
    tcp_common
)

target_enable_warnings(client)

add_executable(main
  main.cpp
)

target_link_libraries(main
  PRIVATE
    tcp_common
)

target_enable_warnings(main)
