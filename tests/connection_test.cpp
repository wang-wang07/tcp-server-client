#include "tcp/server/connection.hpp"
#include "tcp/store.hpp"

#include <gtest/gtest.h>

#include <sys/socket.h>

#include <string>
#include <utility>
#include <cerrno>

using tcp::server::Connection;
using tcp::server::InputResult;

TEST(ConnectionTest, TakesSocketOwnership) {
  int fds[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  UniqueFd socket{fds[0]};
  UniqueFd peer{fds[1]};
  const int fd = socket.get();

  Connection connection{std::move(socket)};

  EXPECT_FALSE(socket.valid());
  EXPECT_EQ(connection.socket.get(), fd);
}

TEST(ConnectionTest, HandlesFragmentsBatchesErrorsAndIncompleteTails) {
  KeyValueStore store;
  Connection connection{UniqueFd{}};

  ASSERT_EQ(connection.process_input("SET fruit ap", store),
            InputResult::Ok);
  EXPECT_FALSE(store.exists("fruit"));
  EXPECT_TRUE(connection.output_buffer.empty());

  ASSERT_EQ(connection.process_input(
                "ple\nGET fruit\nDELETE fruit\nDELETE fruit\nBAD\nGET tail",
                store),
            InputResult::Ok);

  EXPECT_EQ(connection.output_buffer,
            "OK\napple\nOK\nNOT_FOUND\nINVALID COMMAND MESSAGE\n");
  EXPECT_FALSE(store.exists("fruit"));
  EXPECT_EQ(connection.input_buffer.substr(connection.input_cursor),
            "GET tail");

  // A later call must preserve the tail without replaying completed frames.
  const auto previous_output = connection.output_buffer;
  ASSERT_EQ(connection.process_input("", store), InputResult::Ok);
  EXPECT_EQ(connection.output_buffer, previous_output);

  ASSERT_EQ(connection.process_input("\n", store), InputResult::Ok);
  EXPECT_EQ(connection.output_buffer, previous_output + "NOT_FOUND\n");
  EXPECT_TRUE(connection.input_buffer.empty());
  EXPECT_EQ(connection.input_cursor, 0U);
}

TEST(ConnectionTest, ConnectionsUseTheSameStore) {
  KeyValueStore store;
  Connection first{UniqueFd{}};
  Connection second{UniqueFd{}};

  ASSERT_EQ(first.process_input("SET shared value\n", store),
            InputResult::Ok);
  ASSERT_EQ(second.process_input("GET shared\n", store),
            InputResult::Ok);

  EXPECT_EQ(first.output_buffer, "OK\n");
  EXPECT_EQ(second.output_buffer, "value\n");
}

// A newline must fit inside the 64 KiB allowance. One byte below the cap
// remains repairable; filling that last byte with non-newline data is fatal.
TEST(ConnectionTest, BoundsIncompleteInput) {
  KeyValueStore store;
  Connection connection{UniqueFd{}};

  ASSERT_EQ(connection.process_input(
                std::string(Connection::kMaxInputBytes - 1, 'x'), store),
            InputResult::Ok);

  EXPECT_EQ(connection.process_input("x", store),
            InputResult::InputLimit);
  EXPECT_EQ(connection.input_buffer.size(), Connection::kMaxInputBytes);
  EXPECT_TRUE(connection.output_buffer.empty());
  EXPECT_EQ(store.count(), 0U);
}


// Model a prefix already accepted by send(), then append another response.
// This checks accounting and ordering without depending on socket timing.
TEST(ConnectionTest, ReclaimsSentPrefixBeforeAppendingReplies) {
  KeyValueStore store;
  Connection connection{UniqueFd{}};

  ASSERT_EQ(connection.process_input("COUNT\nCOUNT\n", store),
            InputResult::Ok);
  ASSERT_EQ(connection.output_buffer, "0\n0\n");

  // Only the second response remains pending.
  connection.output_cursor = 2;

  ASSERT_EQ(connection.process_input("COUNT\n", store), InputResult::Ok);
  EXPECT_EQ(connection.output_cursor, 0U);
  EXPECT_EQ(connection.output_buffer, "0\n0\n");
  EXPECT_TRUE(connection.has_pending_output());
}

// Exercise actual nonblocking sends against a peer that initially reads
// nothing, then resumes. Exact byte comparison detects loss or replay.
TEST(ConnectionTest, PreservesOutputAcrossWouldBlockAndResumes) {
  int fds[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);

  // Adopt both descriptors immediately so assertions cannot leak them.
  Connection connection{UniqueFd{fds[0]}};
  UniqueFd peer{fds[1]};

  // Restrict the sender's kernel queue so this payload cannot fit.
  // setsockopt returns zero on success, -1 with errno on failure.
  const int send_buffer_bytes = 4096;
  ASSERT_EQ(::setsockopt(connection.socket.get(), SOL_SOCKET, SO_SNDBUF,
                         &send_buffer_bytes, sizeof(send_buffer_bytes)),
            0);

  // A nonuniform payload, including NUL bytes, detects offset mistakes.
  // We test the output transport directly, independently of the parser.
  std::string expected;
  for (std::size_t i = 0; i < Connection::kMaxOutputBytes; ++i) {
    expected.push_back(static_cast<char>(i % 251));
  }
  connection.output_buffer = expected;

  const auto first = connection.flush_output();
  ASSERT_TRUE(first.has_value());
  EXPECT_FALSE(*first);
  ASSERT_GT(connection.output_cursor, 0U);
  ASSERT_LT(connection.output_cursor, expected.size());

  const auto blocked_cursor = connection.output_cursor;
  const auto blocked = connection.flush_output();
  ASSERT_TRUE(blocked.has_value());
  EXPECT_FALSE(*blocked);

  // No peer reads occurred: another attempt must preserve the cursor.
  EXPECT_EQ(connection.output_cursor, blocked_cursor);

  std::string received;
  for (int pass = 0; pass < 256 && received.size() < expected.size(); ++pass) {
    const auto result = connection.flush_output();
    ASSERT_TRUE(result.has_value());

    // Drain only currently available bytes. Neither side may block this
    // test, and a finite outer bound turns lost progress into a failure.
    while (true) {
      char buffer[4096];
      const ssize_t count = ::recv(peer.get(), buffer, sizeof(buffer), 0);

      if (count > 0) {
        received.append(buffer, static_cast<std::size_t>(count));
        continue;
      }

      if (count == -1 && errno == EINTR) {
        continue;
      }

      ASSERT_EQ(count, -1); // EOF is unexpected while the owner is alive.
      ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
      break;
    }
  }

  EXPECT_EQ(received, expected);
  EXPECT_FALSE(connection.has_pending_output());
  EXPECT_TRUE(connection.output_buffer.empty());
  EXPECT_EQ(connection.output_cursor, 0U);
}

// Writing after peer closure must return an error without killing the test
// process through SIGPIPE. Socket ownership remains with Connection.
TEST(ConnectionTest, ReportsBrokenPeerWithoutSigpipe) {
  int fds[2]{};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0);

  Connection connection{UniqueFd{fds[0]}};
  {
    UniqueFd peer{fds[1]};
  } // Destroying the peer closes its descriptor before sending.

  connection.output_buffer = "reply\n";
  const auto result = connection.flush_output();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().value(), EPIPE);
  EXPECT_EQ(connection.output_cursor, 0U);
  EXPECT_TRUE(connection.socket.valid());
}
// Backpressure must defer a mutation, retain its frame, and resume only after
// the low-water threshold is reached. Cursor changes model successful sends.
TEST(ConnectionTest, BackpressureDefersMutationUntilLowWater) {
  KeyValueStore store;
  Connection connection{UniqueFd{}};
  connection.output_buffer.assign(Connection::kOutputHighWater, 'x');
  ASSERT_EQ(connection.process_input("SET delayed value\n", store), InputResult::Ok);
  // Accepted input is deferred: no mutation, runnable work, or new reads yet.
  EXPECT_TRUE(connection.output_paused);
  EXPECT_FALSE(store.exists("delayed"));
  EXPECT_TRUE(connection.has_complete_frame());
  EXPECT_FALSE(connection.has_runnable_input());
  EXPECT_FALSE(connection.can_read());
  // Leave low-water + 1 bytes pending: hysteresis must preserve the pause.
  connection.output_cursor = Connection::kOutputHighWater - Connection::kOutputLowWater - 1;
  ASSERT_EQ(connection.process_input("", store), InputResult::Ok);
  EXPECT_TRUE(connection.output_paused);
  EXPECT_FALSE(store.exists("delayed"));
  // process_input compacted the prefix and reset the cursor. One more sent
  // byte reaches low-water exactly and permits the retained SET to execute.
  connection.output_cursor = 1;
  ASSERT_EQ(connection.process_input("", store), InputResult::Ok);
  EXPECT_FALSE(connection.output_paused);
  EXPECT_TRUE(store.exists("delayed"));
  EXPECT_EQ(connection.output_buffer.substr(Connection::kOutputLowWater), "OK\n");
  EXPECT_FALSE(connection.has_complete_frame());
}

// A budget may defer complete frames, but another empty-input visit must
// execute them without waiting for a new client send or replaying old frames.
TEST(ConnectionTest, ResumesBufferedCommandsWithoutNewInput) {
  KeyValueStore store;
  Connection connection{UniqueFd{}};
  std::string batch;
  // Five extra COUNT frames force a second visit; each reply is two bytes.
  for (std::size_t i = 0; i < Connection::kCommandBudget + 5; ++i) {
    batch += "COUNT\n";
  }
  ASSERT_EQ(connection.process_input(batch, store), InputResult::Ok);
  EXPECT_EQ(connection.pending_output_bytes(), Connection::kCommandBudget * 2);
  // The remaining complete frames are runnable; reads yield to this backlog.
  EXPECT_TRUE(connection.has_runnable_input());
  EXPECT_FALSE(connection.can_read());
  ASSERT_EQ(connection.process_input("", store), InputResult::Ok);
  EXPECT_EQ(connection.pending_output_bytes(), (Connection::kCommandBudget + 5) * 2);
  EXPECT_EQ(connection.pending_input_bytes(), 0U);
  EXPECT_TRUE(connection.can_read());
}

// Direct store insertion bypasses wire-frame bounds. The defensive response
// check must reject a value whose reply newline exceeds the reserved size.
TEST(ConnectionTest, RejectsOversizedResponse) {
  KeyValueStore store;
  Connection connection{UniqueFd{}};
  store.set("large", std::string(Connection::kMaxResponseBytes, 'x'));
  EXPECT_EQ(connection.process_input("GET large\n", store), InputResult::OutputLimit);
  EXPECT_TRUE(connection.output_buffer.empty());
}

// Exactly 64 KiB is valid when the last byte IS the required newline.
TEST(ConnectionTest, AcceptsFrameAtExactInputLimit) {
  KeyValueStore store;
  Connection connection{UniqueFd{}};
  std::string frame = "SET edge ";
  // Reserve the command prefix and one delimiter byte before filling the value.
  const auto value_bytes = Connection::kMaxInputBytes - frame.size() - 1;
  frame.append(value_bytes, 'x');
  frame.push_back('\n');
  ASSERT_EQ(connection.process_input(frame, store), InputResult::Ok);
  EXPECT_EQ(connection.output_buffer, "OK\n");
  const auto value = store.get("edge");
  // Check presence before dereferencing the optional store result.
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->size(), value_bytes);
}
