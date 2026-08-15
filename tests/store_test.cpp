#include "tcp/store.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(StoreTest, StartsEmpty) {
  const KeyValueStore store;

  EXPECT_EQ(store.count(), 0U);
  EXPECT_FALSE(store.exists("missing"));
  EXPECT_EQ(store.get("missing"), std::nullopt);
}

TEST(StoreTest, SetsAndGetsValues) {
  KeyValueStore store;

  store.set("name", "Ada");

  EXPECT_TRUE(store.exists("name"));
  EXPECT_EQ(store.get("name"), std::optional<std::string>{"Ada"});
  EXPECT_EQ(store.count(), 1U);
}

TEST(StoreTest, OverwritesWithoutAddingAnotherEntry) {
  KeyValueStore store;
  store.set("name", "Ada");

  store.set("name", "Grace");

  EXPECT_EQ(store.get("name"), std::optional<std::string>{"Grace"});
  EXPECT_EQ(store.count(), 1U);
}

TEST(StoreTest, ErasesOnlyExistingEntries) {
  KeyValueStore store;
  store.set("first", "1");
  store.set("second", "2");

  EXPECT_TRUE(store.erase("first"));
  EXPECT_FALSE(store.erase("first"));
  EXPECT_FALSE(store.exists("first"));
  EXPECT_EQ(store.get("first"), std::nullopt);
  EXPECT_EQ(store.count(), 1U);
}

TEST(StoreTest, SupportsEmptyKeysAndValues) {
  KeyValueStore store;

  store.set("", "");

  EXPECT_TRUE(store.exists(""));
  EXPECT_EQ(store.get(""), std::optional<std::string>{""});
  EXPECT_EQ(store.count(), 1U);
}
