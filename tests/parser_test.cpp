#include "tcp/protocol/parser.hpp"

#include "tcp/protocol/command.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

void expect_command(const std::string& input, CommandType type,
                    const std::string& key, const std::string& value) {
  const auto command = parse_command(input);
  ASSERT_TRUE(command.has_value()) << input;
  EXPECT_EQ(command->type, type);
  EXPECT_EQ(command->key, key);
  EXPECT_EQ(command->value, value);
}

}  // namespace

TEST(ParserTest, ParsesEverySupportedCommand) {
  expect_command("SET language cpp", CommandType::Set, "language", "cpp");
  expect_command("GET language", CommandType::Get, "language", "");
  expect_command("DELETE language", CommandType::Erase, "language", "");
  expect_command("EXISTS language", CommandType::Exists, "language", "");
  expect_command("COUNT", CommandType::Count, "", "");
}

TEST(ParserTest, AcceptsFlexibleWhitespace) {
  expect_command("  SET   language   cpp  ", CommandType::Set, "language",
                 "cpp");
  expect_command("\tGET\tlanguage\t", CommandType::Get, "language", "");
  expect_command("  COUNT  ", CommandType::Count, "", "");
}

TEST(ParserTest, RejectsMalformedCommands) {
  const std::vector<std::string> invalid_commands{
      "",
      "   ",
      "UNKNOWN",
      "set key value",
      "SET",
      "SET key",
      "SET key value extra",
      "GET",
      "GET key extra",
      "DELETE",
      "DELETE key extra",
      "EXISTS",
      "EXISTS key extra",
      "COUNT extra",
  };

  for (const auto& input : invalid_commands) {
    SCOPED_TRACE(input);
    EXPECT_EQ(parse_command(input), std::nullopt);
  }
}
