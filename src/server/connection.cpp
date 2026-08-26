#include "tcp/server/connection.hpp"
#include <utility>

namespace tcp::server {
  Connection::Connection(UniqueFd socket)
    : socket(std::move(socket)),
      last_activity(Clock::now())
  {}

  int Connection::fd() const noexcept {
    return socket.get();
  }

  bool Connection::has_pending_output() const noexcept {
    return output_cursor < output_buffer.size();
  }
}
