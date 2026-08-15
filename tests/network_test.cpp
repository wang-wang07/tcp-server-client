#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>

TEST(UniqueFdTest, DefaultInstanceIsInvalid) {
  const UniqueFd descriptor;

  EXPECT_FALSE(descriptor.valid());
  EXPECT_EQ(descriptor.get(), -1);
}

TEST(UniqueFdTest, ClosesOwnedDescriptorOnDestruction) {
  int pipe_descriptors[2]{};
  ASSERT_EQ(::pipe(pipe_descriptors), 0);
  const int owned_descriptor = pipe_descriptors[0];

  {
    const UniqueFd descriptor{owned_descriptor};
    ASSERT_TRUE(descriptor.valid());
  }

  errno = 0;
  EXPECT_EQ(::fcntl(owned_descriptor, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  EXPECT_EQ(::close(pipe_descriptors[1]), 0);
}

TEST(UniqueFdTest, MoveConstructionTransfersOwnership) {
  int pipe_descriptors[2]{};
  ASSERT_EQ(::pipe(pipe_descriptors), 0);
  UniqueFd original{pipe_descriptors[0]};

  UniqueFd moved{std::move(original)};

  EXPECT_FALSE(original.valid());
  EXPECT_TRUE(moved.valid());
  EXPECT_EQ(moved.get(), pipe_descriptors[0]);
  EXPECT_EQ(::close(pipe_descriptors[1]), 0);
}

TEST(UniqueFdTest, MoveAssignmentClosesOldDescriptor) {
  int first_pipe[2]{};
  int second_pipe[2]{};
  ASSERT_EQ(::pipe(first_pipe), 0);
  ASSERT_EQ(::pipe(second_pipe), 0);
  UniqueFd source{first_pipe[0]};
  UniqueFd destination{second_pipe[0]};
  const int replaced_descriptor = destination.get();

  destination = std::move(source);

  EXPECT_FALSE(source.valid());
  EXPECT_EQ(destination.get(), first_pipe[0]);
  errno = 0;
  EXPECT_EQ(::fcntl(replaced_descriptor, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  EXPECT_EQ(::close(first_pipe[1]), 0);
  EXPECT_EQ(::close(second_pipe[1]), 0);
}

TEST(SocketTest, CreatesOwnedSocket) {
  auto result = net::create_socket(AF_UNIX, SOCK_STREAM, 0);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->valid());
}

TEST(SocketTest, ReportsSocketCreationFailure) {
  const auto result = net::create_socket(-1, SOCK_STREAM, 0);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().value(), 0);
}
