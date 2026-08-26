#include "tcp/server/connection.hpp"

#include "tcp/net/unique_fd.hpp"

#include <gtest/gtest.h>

#include <sys/socket.h>

#include <type_traits>
#include <utility>

namespace {

static_assert(!std::is_copy_constructible_v<tcp::server::Connection>);
static_assert(!std::is_copy_assignable_v<tcp::server::Connection>);
static_assert(std::is_nothrow_move_constructible_v<tcp::server::Connection>);
static_assert(std::is_nothrow_move_assignable_v<tcp::server::Connection>);

TEST(ConnectionTest, TakesOwnershipOfSocket) {
  int descriptors[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
  UniqueFd socket{descriptors[0]};
  UniqueFd peer{descriptors[1]};
  const int raw_descriptor = socket.get();

  tcp::server::Connection connection{std::move(socket)};

  EXPECT_FALSE(socket.valid());
  EXPECT_TRUE(connection.socket.valid());
  EXPECT_EQ(connection.fd(), raw_descriptor);
}

TEST(ConnectionTest, StartsWithEmptyBuffersAndNoPendingOutput) {
  int descriptors[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
  UniqueFd socket{descriptors[0]};
  UniqueFd peer{descriptors[1]};
  const auto before_construction = tcp::server::Connection::Clock::now();

  tcp::server::Connection connection{std::move(socket)};
  const auto after_construction = tcp::server::Connection::Clock::now();

  EXPECT_TRUE(connection.input_buffer.empty());
  EXPECT_EQ(connection.input_cursor, 0U);
  EXPECT_TRUE(connection.output_buffer.empty());
  EXPECT_EQ(connection.output_cursor, 0U);
  EXPECT_FALSE(connection.has_pending_output());
  EXPECT_GE(connection.last_activity, before_construction);
  EXPECT_LE(connection.last_activity, after_construction);
}

TEST(ConnectionTest, TracksPendingOutputUsingCursor) {
  int descriptors[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
  UniqueFd socket{descriptors[0]};
  UniqueFd peer{descriptors[1]};
  tcp::server::Connection connection{std::move(socket)};
  connection.output_buffer = "OK\nAda\n";

  EXPECT_TRUE(connection.has_pending_output());

  connection.output_cursor = 3;
  EXPECT_TRUE(connection.has_pending_output());

  connection.output_cursor = connection.output_buffer.size();
  EXPECT_FALSE(connection.has_pending_output());
}

}  // namespace
