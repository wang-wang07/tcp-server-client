#include "tcp/net/event_poller.hpp"
#include "tcp/net/unique_fd.hpp"

#include <cerrno>
#include <expected>
#include <sys/epoll.h>
#include <system_error>
#include <utility>

namespace net {
  EventPoller::EventPoller(UniqueFd descriptor)
    : descriptor_(std::move(descriptor)) {}

  std::expected<EventPoller, std::error_code> EventPoller::create() {
    UniqueFd descriptor{::epoll_create1(EPOLL_CLOEXEC)};

    if (!descriptor.valid()) {
      return std::unexpected(std::error_code{errno, std::generic_category()});
    }

    return EventPoller{std::move(descriptor)};
  }

  EventPoller::Status EventPoller::change(
    int operation, int fd, Interest interest
  ) {
    epoll_event event{};
    event.data.fd = fd;

    if (interest.read) {
      event.events |= EPOLLIN | EPOLLRDHUP
    }

    if (interest.write) {
      event.events |= EPOLLOUT;
    }

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
    if (::epoll_ctl(descriptor_.get(), EPOLL_CTL_DEL, fd, nullptr) == -1) {
      return std::unexpected(std::error_code{errno, std::generic_category()});
    }

    return {};
  }

  EventPoller::WaitResult EventPoller::wait_one(int timeout_ms) {
    epoll_event event{};
    const int count = ::epoll_wait(descriptor_.get(), &event, 1, timeout_ms);

    if (count == -1) {
      return std::unexpected(std::error_code{errno, std::generic_category()});
    }

    if (count == 0) {
      return std::optional<ReadyEvent>{};
    }

    return std::optional<ReadyEvent>{ReadyEvent{
      .fd = event.data.fd,
      .readable = (event.events & (EPOLLIN | EPOLLRDHUP)) != 0,
      .writeable = (event.events & EPOLLOUT) != 0,
      .error = (event.events & EPOLLERR) != 0,
      .hangup = (event.events & EPOLLHUP) != 0,
    }};
  }
}
