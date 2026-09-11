#pragma once

#include "tcp/net/unique_fd.hpp"

#include <expected>
#include <optional>
#include <sys/socket.h>
#include <system_error>

namespace net {
  struct Interest {
    bool read = false;
    bool write = false;
  };

  struct ReadyEvent {
    int fd = -1;
    bool readable = false;
    bool writeable = false;
    bool error = false;
    bool hangup = false;
  };

  // owns linux epoll desciprtor
  class EventPoller {
    public:
      using Status = std::expected<void, std::error_code>;

      using WaitResult =
            std::expected<std::optional<ReadyEvent>, std::error_code>;

      [[nodiscard]] static std::expected<EventPoller, std::error_code> create();

      EventPoller(const EventPoller&) = delete;
      EventPoller& operator=(const EventPoller&) = delete;
      EventPoller(EventPoller&&) noexcept = default;
      EventPoller& operator=(EventPoller&&) noexcept = default;

      [[nodiscard]] Status add(int fd, Interest interest);
      [[nodiscard]] Status modify(int fd, Interest interest);
      [[nodiscard]] Status remove(int fd);

      [[nodiscard]] WaitResult wait_one(int timeout_ms);
    private:
      explicit EventPoller(UniqueFd descriptor);
      [[nodiscard]] Status change(int operation, int fd, Interest interest);
      UniqueFd descriptor_;
  };
}
