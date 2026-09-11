add_executable(tcp_tests
  connection_test.cpp
  event_poller_test.cpp
  executor_test.cpp
  framing_test.cpp
  network_test.cpp
  parser_test.cpp
  protocol_test.cpp
  store_test.cpp
)

target_link_libraries(tcp_tests
  PRIVATE
    tcp_common
    GTest::gtest_main
)

target_enable_warnings(tcp_tests)

include(GoogleTest)
gtest_discover_tests(tcp_tests DISCOVERY_TIMEOUT 30)

# This Linux workload owns a server on port 8080; do not run it alongside
# another CTest workload that could contend for the same listening address.
find_package(Perl REQUIRED)
add_test(NAME ServerResourceCheck
  COMMAND ${PERL_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/server_resource_check.pl
          $<TARGET_FILE:server>)
set_tests_properties(ServerResourceCheck PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
