#include "tcp/protocol/framing.hpp"

#include "tcp/net/unique_fd.hpp"

#include <gtest/gtest.h>

#include <sys/socket.h>

#include <string>

TEST(FramingTest, LeavesIncompleteMessageInBuffer) {
  std::string buffer = "partial message";

  EXPECT_EQ(next_message(buffer), std::nullopt);
  EXPECT_EQ(buffer, "partial message");
}

TEST(FramingTest, ExtractsMessagesAndPreservesRemainder) {
  std::string buffer = "first\nsecond\npartial";

  EXPECT_EQ(next_message(buffer), std::optional<std::string>{"first"});
  EXPECT_EQ(buffer, "second\npartial");
  EXPECT_EQ(next_message(buffer), std::optional<std::string>{"second"});
  EXPECT_EQ(buffer, "partial");
}

TEST(FramingTest, SupportsEmptyAndConsecutiveMessages) {
  std::string buffer = "\n\nremaining";

  EXPECT_EQ(next_message(buffer), std::optional<std::string>{""});
  EXPECT_EQ(next_message(buffer), std::optional<std::string>{""});
  EXPECT_EQ(buffer, "remaining");
}

TEST(FramingSocketTest, ReadsBufferedMessageWithoutUsingSocket) {
  std::string buffer = "ready\nremainder";

  EXPECT_EQ(read_message(-1, buffer), std::optional<std::string>{"ready"});
  EXPECT_EQ(buffer, "remainder");
}

TEST(FramingSocketTest, SendsAndReadsMultipleMessages) {
  int descriptors[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
  UniqueFd sender{descriptors[0]};
  UniqueFd receiver{descriptors[1]};
  std::string buffer;

  ASSERT_TRUE(send_all(sender.get(), "first\nsecond\n"));

  EXPECT_EQ(read_message(receiver.get(), buffer),
            std::optional<std::string>{"first"});
  EXPECT_EQ(read_message(receiver.get(), buffer),
            std::optional<std::string>{"second"});
}

TEST(FramingSocketTest, CombinesExistingPartialDataWithSocketData) {
  int descriptors[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
  UniqueFd sender{descriptors[0]};
  UniqueFd receiver{descriptors[1]};
  std::string buffer = "hel";

  ASSERT_TRUE(send_all(sender.get(), "lo\nnext\n"));

  EXPECT_EQ(read_message(receiver.get(), buffer),
            std::optional<std::string>{"hello"});
  EXPECT_EQ(read_message(receiver.get(), buffer),
            std::optional<std::string>{"next"});
}

TEST(FramingSocketTest, ReturnsNoMessageAndKeepsPartialDataAtEof) {
  int descriptors[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
  UniqueFd sender{descriptors[0]};
  UniqueFd receiver{descriptors[1]};
  std::string buffer;

  ASSERT_TRUE(send_all(sender.get(), "partial"));
  ASSERT_EQ(::shutdown(sender.get(), SHUT_WR), 0);

  EXPECT_EQ(read_message(receiver.get(), buffer), std::nullopt);
  EXPECT_EQ(buffer, "partial");
}

TEST(FramingSocketTest, SendingEmptyMessageSucceeds) {
  EXPECT_TRUE(send_all(-1, ""));
}
