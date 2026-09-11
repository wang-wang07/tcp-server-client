#include "tcp/net/event_poller.hpp"
#include "tcp/net/unique_fd.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <utility>

// An empty optional means timeout; an unexpected result means an actual error.
TEST(EventPollerTest, DistinguishesTimeoutFromError) {
  auto created = net::EventPoller::create();
  ASSERT_TRUE(created.has_value()); // Do not move/dereference a failed result.
  auto poller = std::move(*created);
  const auto waited = poller.wait_one(0);
  ASSERT_TRUE(waited.has_value()); // The syscall itself must have succeeded.
  EXPECT_FALSE(waited->has_value()); // No registered socket can produce an event.
  EXPECT_FALSE(poller.add(-1, {.read = true}).has_value()); // Invalid fd is error.
}

// Changing interest must enable writable notifications only while requested;
// removing interest must leave ownership (and the socket itself) intact.
TEST(EventPollerTest, ModifiesInterestWithoutOwningSocket) {
  int fds[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
  UniqueFd socket{fds[0]};
  UniqueFd peer{fds[1]}; // Adopt both immediately so assertions cannot leak them.
  auto created = net::EventPoller::create();
  ASSERT_TRUE(created.has_value());
  auto poller = std::move(*created);

  ASSERT_TRUE(poller.add(socket.get(), {.read = true}).has_value());
  EXPECT_FALSE(poller.add(socket.get(), {}).has_value()); // Duplicate ADD fails.
  auto waited = poller.wait_one(0);
  ASSERT_TRUE(waited.has_value());
  EXPECT_FALSE(waited->has_value()); // Idle read interest must not spin.

  ASSERT_TRUE(poller.modify(socket.get(), {.write = true}).has_value());
  waited = poller.wait_one(100);
  ASSERT_TRUE(waited.has_value());
  ASSERT_TRUE(waited->has_value()); // Only now is dereferencing the event safe.
  EXPECT_EQ((**waited).fd, socket.get());
  EXPECT_TRUE((**waited).writable);

  ASSERT_TRUE(poller.modify(socket.get(), {}).has_value());
  waited = poller.wait_one(0);
  ASSERT_TRUE(waited.has_value());
  EXPECT_FALSE(waited->has_value()); // Removing write interest stops wakeups.

  ASSERT_TRUE(poller.remove(socket.get()).has_value());
  // The socket remains usable after DEL: EventPoller must not close it.
  ASSERT_EQ(::send(peer.get(), "x", 1, MSG_NOSIGNAL), 1);
  char byte{};
  ASSERT_EQ(::recv(socket.get(), &byte, 1, 0), 1);
  EXPECT_EQ(byte, 'x');
}

// Half-close means the read stream ends after queued bytes; it must not be
// translated into a full hangup that discards a client's pending responses.
TEST(EventPollerTest, HalfClosePreservesReadableBytes) {
  int fds[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
  UniqueFd socket{fds[0]};
  UniqueFd peer{fds[1]};
  auto created = net::EventPoller::create();
  ASSERT_TRUE(created.has_value());
  auto poller = std::move(*created);
  ASSERT_TRUE(poller.add(socket.get(), {.read = true}).has_value());
  ASSERT_EQ(::send(peer.get(), "abc", 3, MSG_NOSIGNAL), 3);
  ASSERT_EQ(::shutdown(peer.get(), SHUT_WR), 0);

  const auto waited = poller.wait_one(100);
  ASSERT_TRUE(waited.has_value());
  ASSERT_TRUE(waited->has_value());
  EXPECT_TRUE((**waited).readable);
  EXPECT_FALSE((**waited).hangup);
  char bytes[3]{};
  ASSERT_EQ(::recv(socket.get(), bytes, sizeof(bytes), 0), 3);
  EXPECT_EQ(::recv(socket.get(), bytes, sizeof(bytes), 0), 0); // Actual read EOF.

  ASSERT_TRUE(poller.modify(socket.get(), {}).has_value());
  const auto paused = poller.wait_one(0);
  ASSERT_TRUE(paused.has_value());
  EXPECT_FALSE(paused->has_value()); // Suppress the persistent RDHUP notification.
}

// Remove a registration while it has queued readiness, then re-add it. This
// checks epoll bookkeeping without closing or reusing descriptor identities.
TEST(EventPollerTest, RemovalClearsQueuedReadiness) {
  int fds[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);
  UniqueFd socket{fds[0]};
  UniqueFd peer{fds[1]};
  auto created = net::EventPoller::create();
  ASSERT_TRUE(created.has_value());
  auto poller = std::move(*created);
  ASSERT_TRUE(poller.add(socket.get(), {.read = true}).has_value());
  ASSERT_EQ(::send(peer.get(), "x", 1, MSG_NOSIGNAL), 1);
  ASSERT_TRUE(poller.remove(socket.get()).has_value());
  auto waited = poller.wait_one(0);
  ASSERT_TRUE(waited.has_value());
  EXPECT_FALSE(waited->has_value()); // Removed registration cannot return an event.
  ASSERT_TRUE(poller.add(socket.get(), {.read = true}).has_value());
  waited = poller.wait_one(100);
  ASSERT_TRUE(waited.has_value());
  ASSERT_TRUE(waited->has_value());
  EXPECT_TRUE((**waited).readable); // Re-adding discovers still-unread bytes.
}
