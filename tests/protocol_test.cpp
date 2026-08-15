#include "tcp/protocol/executor.hpp"
#include "tcp/protocol/parser.hpp"

#include "tcp/store.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string parse_and_execute(const std::string& input, KeyValueStore& store) {
  const auto command = parse_command(input);
  if (!command) {
    return "INVALID_COMMAND";
  }

  return execute_command(*command, store);
}

}  // namespace

TEST(ProtocolIntegrationTest, ExecutesCommandSequenceAgainstSharedStore) {
  KeyValueStore store;

  EXPECT_EQ(parse_and_execute("COUNT", store), "0");
  EXPECT_EQ(parse_and_execute("SET language cpp", store), "OK");
  EXPECT_EQ(parse_and_execute("GET language", store), "cpp");
  EXPECT_EQ(parse_and_execute("EXISTS language", store), "1");
  EXPECT_EQ(parse_and_execute("COUNT", store), "1");
  EXPECT_EQ(parse_and_execute("DELETE language", store), "OK");
  EXPECT_EQ(parse_and_execute("GET language", store), "NOT_FOUND");
  EXPECT_EQ(parse_and_execute("invalid", store), "INVALID_COMMAND");
}
