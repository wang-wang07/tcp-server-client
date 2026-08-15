add_executable(tcp_tests
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
