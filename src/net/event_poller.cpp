#include "tcp/net/event_poller.hpp"

#include <cerrno>
#include <sys/epoll.h>
#include <utility>

namespace net {

EventPoller::EventPoller(UniqueFd descriptor)
    : descriptor_(std::move(descriptor)) {}

std::expected<EventPoller, std::error_code> EventPoller::create() {
  // CLOEXEC closes the epoll descriptor if this process successfully execs
  // another program. It does not affect the separately owned client sockets.
  UniqueFd descriptor{::epoll_create1(EPOLL_CLOEXEC)};

  // A negative descriptor means creation failed; preserve errno as an error.
  if (!descriptor.valid()) {
    return std::unexpected(std::error_code{errno, std::generic_category()});
  }

  // Move the unique owner into the result; no descriptor is copied or duplicated.
  return EventPoller{std::move(descriptor)};
}

EventPoller::Status EventPoller::change(
    int operation, int fd, Interest interest) {
  epoll_event event{};
  event.data.fd = fd; // Borrowed identity, copied by the kernel during epoll_ctl.

  // Read interest includes half-close notifications so recv can discover EOF.
  // Pausing reads suppresses BOTH flags to avoid repeated RDHUP notifications.
  if (interest.read) {
    event.events |= EPOLLIN | EPOLLRDHUP;
  }

  // Only pending output warrants level-triggered writable notifications.
  if (interest.write) {
    event.events |= EPOLLOUT;
  }

  // ADD creates a registration; MOD replaces its interest mask. Neither owns
  // fd. This implementation intentionally omits EPOLLET and EPOLLONESHOT.
  if (::epoll_ctl(descriptor_.get(), operation, fd, &event) == -1) {
    return std::unexpected(std::error_code{errno, std::generic_category()});
  }
  return {};
}

EventPoller::Status EventPoller::add(int fd, Interest interest) {
  return change(EPOLL_CTL_ADD, fd, interest);
}

EventPoller::Status EventPoller::modify(int fd, Interest interest) {
  return change(EPOLL_CTL_MOD, fd, interest);
}

EventPoller::Status EventPoller::remove(int fd) {
  // DEL removes only the registration. Closing the socket remains the owner's
  // responsibility. The final argument is unused for DEL on supported Linux.
  if (::epoll_ctl(descriptor_.get(), EPOLL_CTL_DEL, fd, nullptr) == -1) {
    return std::unexpected(std::error_code{errno, std::generic_category()});
  }
  return {};
}

EventPoller::WaitResult EventPoller::wait_one(int timeout_ms) {
  epoll_event event{};
  const int count = ::epoll_wait(descriptor_.get(), &event, 1, timeout_ms);

  // -1 means no event was returned. EINTR is returned too, not retried here.
  if (count == -1) {
    return std::unexpected(std::error_code{errno, std::generic_category()});
  }

  // Zero means timeout. Do not interpret the zero-initialized event as fd zero.
  if (count == 0) {
    return std::optional<ReadyEvent>{};
  }

  // Test each bit independently because readability, EOF hints, and errors
  // can arrive together. Read EOF is confirmed later by recv() returning zero.
  return std::optional<ReadyEvent>{ReadyEvent{
      .fd = event.data.fd,
      .readable = (event.events & (EPOLLIN | EPOLLRDHUP)) != 0,
      .writable = (event.events & EPOLLOUT) != 0,
      .error = (event.events & EPOLLERR) != 0,
      .hangup = (event.events & EPOLLHUP) != 0,
  }};
}

}
