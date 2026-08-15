#include "tcp/protocol/executor.hpp"

#include "tcp/protocol/command.hpp"
#include "tcp/store.hpp"

#include <gtest/gtest.h>

TEST(ExecutorTest, SetStoresValueAndReturnsOk) {
  KeyValueStore store;
  const Command command{CommandType::Set, "name", "Ada"};

  EXPECT_EQ(execute_command(command, store), "OK");
  EXPECT_EQ(store.get("name"), std::optional<std::string>{"Ada"});
}

TEST(ExecutorTest, GetReturnsValueOrNotFound) {
  KeyValueStore store;
  store.set("name", "Ada");

  EXPECT_EQ(execute_command({CommandType::Get, "name", ""}, store), "Ada");
  EXPECT_EQ(execute_command({CommandType::Get, "missing", ""}, store),
            "NOT_FOUND");
}

TEST(ExecutorTest, EraseReportsWhetherKeyExisted) {
  KeyValueStore store;
  store.set("name", "Ada");

  EXPECT_EQ(execute_command({CommandType::Erase, "name", ""}, store), "OK");
  EXPECT_EQ(execute_command({CommandType::Erase, "name", ""}, store),
            "NOT_FOUND");
  EXPECT_FALSE(store.exists("name"));
}

TEST(ExecutorTest, ExistsReturnsNumericBoolean) {
  KeyValueStore store;
  store.set("name", "Ada");

  EXPECT_EQ(execute_command({CommandType::Exists, "name", ""}, store), "1");
  EXPECT_EQ(execute_command({CommandType::Exists, "missing", ""}, store),
            "0");
}

TEST(ExecutorTest, CountTracksEntriesRatherThanAssignments) {
  KeyValueStore store;

  EXPECT_EQ(execute_command({CommandType::Count, "", ""}, store), "0");
  EXPECT_EQ(execute_command({CommandType::Set, "name", "Ada"}, store), "OK");
  EXPECT_EQ(execute_command({CommandType::Set, "name", "Grace"}, store),
            "OK");
  EXPECT_EQ(execute_command({CommandType::Count, "", ""}, store), "1");
}
