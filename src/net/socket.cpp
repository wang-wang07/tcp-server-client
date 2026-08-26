#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"

#include <cerrno>
#include <expected>
#include <sys/socket.h>
#include <system_error>
#include <fcntl.h>

namespace net {
  std::expected<UniqueFd, std::error_code>
    create_socket(int domain, int type, int protocol)
  {
    int fd = ::socket(domain, type, protocol);

    if (fd == -1) {
      return std::unexpected(std::error_code(errno, std::generic_category()));
    }

    return UniqueFd{fd};
  }

  [[nodiscard]] std::expected<void, std::error_code> set_nonblocking(int fd) {
    int flags;
    do {
      flags = ::fcntl(fd, F_GETFL);
    } while (flags == -1 && errno == EINTR);

    if (flags == -1) {
      return std::unexpected(std::error_code(errno, std::generic_category()));
    }

    if ((flags & O_NONBLOCK) != 0) {
      return {};
    }

    int result;

    do {
      result = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
      return std::unexpected(std::error_code(errno, std::generic_category()));
    }

    return {};
  }
}
