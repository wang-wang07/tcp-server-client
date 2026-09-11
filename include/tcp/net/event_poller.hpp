#pragma once

#include "tcp/net/unique_fd.hpp"

#include <expected>
#include <optional>
#include <system_error>

namespace net {

// Interests request notifications; they do not establish that I/O will succeed.
struct Interest {
  bool read = false;
  bool write = false;
};

// Values copied out of epoll. This object does not own the identified socket.
struct ReadyEvent {
  int fd = -1;
  bool readable = false; // Includes a peer write-half-close: recv may return EOF.
  bool writable = false;
  bool error = false;
  bool hangup = false;   // Full hangup, distinct from a write-half-close.
};

// A Linux epoll owner, not a family of interchangeable platform backends.
// The server owns client sockets; this class owns only the epoll descriptor.
class EventPoller {
public:
  using Status = std::expected<void, std::error_code>;
  using WaitResult = std::expected<std::optional<ReadyEvent>, std::error_code>;

  [[nodiscard]] static std::expected<EventPoller, std::error_code> create();

  EventPoller(const EventPoller&) = delete;
  EventPoller& operator=(const EventPoller&) = delete;
  EventPoller(EventPoller&&) noexcept = default;
  EventPoller& operator=(EventPoller&&) noexcept = default;

  [[nodiscard]] Status add(int fd, Interest interest);
  [[nodiscard]] Status modify(int fd, Interest interest);
  [[nodiscard]] Status remove(int fd);

  // Returns an event, an empty optional on timeout, or an error (including
  // EINTR). The caller handles EINTR so it can revisit timers between waits.
  [[nodiscard]] WaitResult wait_one(int timeout_ms);

private:
  explicit EventPoller(UniqueFd descriptor);
  [[nodiscard]] Status change(int operation, int fd, Interest interest);
  UniqueFd descriptor_;
};

}
