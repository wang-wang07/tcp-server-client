#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"

#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <system_error>
#include <sys/epoll.h>
#include <utility>
#include <string_view>

namespace {
  void report_error(std::string_view operation, int error_number) {
    const std::error_code error {
      error_number,
      std::generic_category(),
    };

    std::cerr << operation << " failed: " << error.message() << '\n';
  }
}
int main() {
  auto socket_result = net::create_socket(AF_INET, SOCK_STREAM, 0);

  if (!socket_result) {
    std::cerr << "socket failed: " << socket_result.error().message() << '\n';
    return 1;
  }
  UniqueFd listener = std::move(*socket_result);

  const int reuse_address = 1;

  if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) == -1) {
    report_error("setsockopt", errno);
    return 1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(8080);

  if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) == -1) {
    report_error("bind", errno);
    return 1;
  }

  if (::listen(listener.get(), 16)  == -1) {
    report_error("listen", errno);
    return 1;
  }

  const auto nonblocking = net::set_nonblocking(listener.get());

  if (!nonblocking) {
    std::cerr << "could not make listerner nonblocking" << nonblocking.error().message();
    return 1;
  }

  UniqueFd epoll_instance {
    ::epoll_create1(EPOLL_CLOEXEC)
  };

  if (!epoll_instance.valid()) {
    report_error("epoll_create1", errno);
    return 1;
  }

  epoll_event listener_event{};
  listener_event.events = EPOLLIN;
  listener_event.data.fd = listener.get();

  if (::epoll_ctl(epoll_instance.get(),
    EPOLL_CTL_ADD,
    listener.get(),
    &listener_event) == -1) {
      report_error("epoll_ctl", errno);
      return 1;
    }

  std::cout << "listening on port 8080\n";

  while (true) {
    epoll_event ready_event{};
    int ready_count{};

    do  {
      ready_count = ::epoll_wait(epoll_instance.get(), &ready_event, 1, -1);
    } while (ready_count == -1 && errno == EINTR);

    if (ready_count == -1) {
      report_error("epoll_wait", errno);
      return 1;
    }

    if (ready_event.data.fd != listener.get() || (ready_event.events & EPOLLIN) == 0) {
      std::cerr << "listener reported an unexpected event\n";
      return 1;
    }

    int accepted_fd {};
    do {
      accepted_fd = ::accept(listener.get(), nullptr, nullptr);
    } while (accepted_fd == -1 && errno == EINTR);

    if (accepted_fd == -1) {
      const int error = errno;
      if (error == EAGAIN || error == EWOULDBLOCK) {
        continue;
      }

      report_error("accept", error);
      return 1;
    }

    UniqueFd client{accepted_fd};

    std::cout << "accepted client on fd " << client.get() << '\n';
    return 0;
  }
}
