#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"

#include <cerrno>
#include <sys/socket.h>
#include <system_error>

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
}
