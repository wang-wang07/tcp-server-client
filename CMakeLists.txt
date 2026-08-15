cmake_minimum_required(VERSION 3.20)

project(tcp_server_client LANGUAGES CXX)

include(CTest)

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
  src/net/socket.cpp
  src/net/unique_fd.cpp
  src/protocol/executor.cpp
  src/protocol/framing.cpp
  src/protocol/parser.cpp
  src/store.cpp
  include/tcp/net/socket.hpp
  include/tcp/net/unique_fd.hpp
  include/tcp/protocol/command.hpp
  include/tcp/protocol/executor.hpp
  include/tcp/protocol/framing.hpp
  include/tcp/protocol/parser.hpp
  include/tcp/protocol/protocol.hpp
  include/tcp/store.hpp
)

target_compile_features(tcp_common PUBLIC cxx_std_23)

target_include_directories(tcp_common
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_enable_warnings(tcp_common)

add_executable(server
  apps/server.cpp
)

target_link_libraries(server
  PRIVATE
    tcp_common
)

target_enable_warnings(server)

add_executable(client
  apps/client.cpp
)

target_link_libraries(client
  PRIVATE
    tcp_common
)

target_enable_warnings(client)

add_executable(playground
  apps/playground.cpp
)

target_link_libraries(playground
  PRIVATE
    tcp_common
)

target_enable_warnings(playground)

if(BUILD_TESTING)
  include(FetchContent)

  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )

  FetchContent_MakeAvailable(googletest)
  add_subdirectory(tests)
endif()
